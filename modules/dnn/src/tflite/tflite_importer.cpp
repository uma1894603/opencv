// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#include "../precomp.hpp"

#ifdef HAVE_FLATBUFFERS
#include "schema_generated.h"
#include "builtin_op_data.h"
#endif

#include <opencv2/core/utils/logger.defines.hpp>
#undef CV_LOG_STRIP_LEVEL
#define CV_LOG_STRIP_LEVEL CV_LOG_LEVEL_VERBOSE + 1
#include <opencv2/core/utils/logger.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

#ifdef HAVE_FLATBUFFERS

using namespace opencv_tflite;

class TFLiteImporter {
public:
    TFLiteImporter(Net& net, const char* modelBuffer, size_t bufSize);

private:
    const opencv_tflite::Model* model;
    const flatbuffers::Vector<flatbuffers::Offset<opencv_tflite::Tensor> >* modelTensors;
    std::map<int, Mat> allTensors;
    Net& dstNet;

    // This is a vector of pairs (layerId, outputId) where we iterate over
    // indices from TFLite notation and get created OpenCV layers.
    std::map<int, std::pair<int, int> > layerIds;

    // Tracking of layouts for layers outputs.
    std::vector<DataLayout> layouts;

    void populateNet();

    // Wrap TFLite Tensor to OpenCV Mat without data copying
    Mat parseTensor(const Tensor& tensor);

    typedef void (TFLiteImporter::*TFLiteImporterNodeParser)(const Operator&, const std::string&, LayerParams&, LayerParams&);
    typedef std::map<std::string, TFLiteImporterNodeParser> DispatchMap;

    const DispatchMap dispatch;
    static DispatchMap buildDispatchMap();

    void parseConvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseDWConvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parsePadding(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseEltwise(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parsePooling(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parsePoolingWithArgmax(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseUnpooling(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseReshape(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseConcat(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseResize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseDeconvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseQuantize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseDequantize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);
    void parseDetectionPostProcess(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams);

    int addPermuteLayer(const std::vector<int>& order, const std::string& permName, const std::pair<int, int>& inpId, int dtype);
    inline void parseActivation(const Operator& op, ActivationFunctionType type, LayerParams& activParams);
    void parseActivation(const Operator& op, const std::string& opcode, LayerParams& activParams, bool isFused = true);
    inline bool isInt8(const Operator& op);
    inline void getQuantParams(const Operator& op, float& inpScale, int& inpZero, float& outScale, int& outZero);
};

Mat TFLiteImporter::parseTensor(const Tensor& tensor)
{
    const auto tensor_shape = tensor.shape();
    CV_Assert(tensor_shape);
    std::vector<int> shape(tensor_shape->begin(), tensor_shape->end());
    int bufferIdx = tensor.buffer();
    CV_Assert(bufferIdx != 0);  // 0th buffer is a no-data buffer
    const Buffer* buffer = model->buffers()->Get(bufferIdx);
    CV_Assert(buffer);
    const auto buffer_data = buffer->data();
    if (!buffer_data)
        return Mat();

    CV_Assert(buffer_data);
    const void* data = buffer_data->data();

    int dtype = -1;
    switch (tensor.type()) {
    case TensorType_FLOAT32:
        dtype = CV_32F;
        break;
    case TensorType_INT32:
        dtype = CV_32S;
        break;
    case TensorType_FLOAT16:
        dtype = CV_16S;
        break;
    case TensorType_INT8:
        dtype = CV_8S;
        break;
    default:
        CV_Error(Error::StsNotImplemented, format("Parse tensor with type %s", EnumNameTensorType(tensor.type())));
    }
    return Mat(shape, dtype, const_cast<void*>(data));
}

TFLiteImporter::TFLiteImporter(Net& dstNet, const char* modelBuffer, size_t bufSize)
    : dstNet(dstNet), dispatch(buildDispatchMap())
{
    flatbuffers::Verifier verifier((const uint8_t*)modelBuffer, bufSize);
    if (!VerifyModelBuffer(verifier)) {
        CV_Error(Error::StsError, "DNN/TFLite: model is incorrect");
    }

    model = GetModel(modelBuffer);
    CV_Assert(model);
    CV_Assert(model->subgraphs());
    CV_Assert(model->buffers());
    CV_CheckEQ((size_t)model->subgraphs()->size(), 1u, "");

    modelTensors = model->subgraphs()->Get(0)->tensors();
    CV_Assert(modelTensors);
    for (int i = 0; i < modelTensors->size(); ++i) {
        const Tensor* tensor = modelTensors->Get(i);
        CV_Assert(tensor);
        if (tensor->buffer() != 0) {
            allTensors[i] = parseTensor(*tensor);
        }
    }

    populateNet();
}

DataLayout estimateLayout(const Tensor& t)
{
    const auto t_shape = t.shape();
    CV_Assert(t_shape);
    switch (t_shape->size()) {
    case 5: return DNN_LAYOUT_NDHWC;
    case 4: return DNN_LAYOUT_NHWC;
    case 2: return DNN_LAYOUT_PLANAR;
    default: return DNN_LAYOUT_UNKNOWN;
    }
}

void TFLiteImporter::populateNet()
{
    CV_Assert(model);
    const auto model_subgraphs = model->subgraphs();
    CV_Assert(model_subgraphs);
    const SubGraph* subgraph = model_subgraphs->Get(0);
    CV_Assert(subgraph);
    const auto subgraph_inputs = subgraph->inputs();
    CV_Assert(subgraph_inputs);
    const auto subgraph_operators = subgraph->operators();
    CV_Assert(subgraph_operators);
    const auto opCodes = model->operator_codes();
    CV_Assert(opCodes);

    CV_Assert(modelTensors);
    layouts.resize(modelTensors->size(), DNN_LAYOUT_UNKNOWN);
    size_t subgraph_inputs_size = subgraph_inputs->size();
    std::vector<std::string> inputsNames(subgraph_inputs_size);
    std::vector<MatShape> inputsShapes(subgraph_inputs_size);
    for (size_t i = 0; i < subgraph_inputs_size; ++i)
    {
        int idx = subgraph_inputs->Get(i);
        layerIds[idx] = std::make_pair(0, i);
        const auto tensor = modelTensors->Get(idx);
        if (!tensor)
            CV_Error(Error::StsError, cv::format("DNN/TFLite: subgraph input %d (%d) is NULL", (int)i, idx));
        layouts[idx] = estimateLayout(*tensor);

        // Keep info about origin inputs names and shapes
        inputsNames[i] = tensor->name()->str();
        std::vector<int> shape(tensor->shape()->begin(), tensor->shape()->end());
        if (layouts[idx] == DNN_LAYOUT_NHWC) {
            CV_CheckEQ(shape.size(), (size_t)4, "");
            std::swap(shape[2], shape[3]);
            std::swap(shape[1], shape[2]);
        }
        inputsShapes[i] = shape;
    }

    dstNet.setInputsNames(inputsNames);
    for (size_t i = 0; i < subgraph_inputs_size; ++i)
    {
        dstNet.setInputShape(inputsNames[i], inputsShapes[i]);
    }

        // For INT8 models with INT8 input tensor, insert a dummy Quantize layer because
        // dnn converts INT8 from net.setInput() to FP32
        // if (tensor->type() == TensorType_INT8) {
        //     LayerParams params;
        //     params.type = "Quantize";
        //     // params.dtype = CV_8S;
        //     int layerId = dstNet.addLayer(params.name, params.type, params);
        //     dstNet.connect(0, i, layerId, 0);
        //     layerIds[idx] = std::make_pair(layerId, 0);
        //     layerParams.set("depth", CV_8S);
        // }

    const auto& all_operators = *subgraph_operators;
    const size_t all_operators_size = all_operators.size();
    for (size_t op_idx = 0; op_idx < all_operators_size; ++op_idx)
    {
        const auto op = all_operators[op_idx];
        CV_Assert(op);
        const auto op_inputs = op->inputs();
        CV_Assert(op_inputs);
        const auto op_outputs = op->outputs();
        CV_Assert(op_outputs);
        int idx = op->opcode_index();

        LayerParams layerParams, activParams;
        layerParams.name = modelTensors->Get(op_outputs->Get(0))->name()->str();

        std::string type = EnumNameBuiltinOperator(BuiltinOperator(opCodes->Get(idx)->deprecated_builtin_code()));
        if (type == "CUSTOM") {
            type = opCodes->Get(idx)->custom_code()->str();
        }

        CV_LOG_DEBUG(NULL, "DNN/TFLite: processing operator (" << op_idx << "/" << all_operators_size << ") with " << op_inputs->size() << " inputs: "
                           << cv::format("[%s]:(%s)", type.c_str(), layerParams.name.c_str()));

        try
        {
            if (type == "DEQUANTIZE") {
                // Convert from FP16 to FP32
                Mat data = allTensors[op_inputs->Get(0)];
                if (!data.empty()) {
                    // Dequantize a buffer
                    Mat dataFP32;
                    convertFp16(data, dataFP32);
                    allTensors[op_outputs->Get(0)] = dataFP32;
                    continue;
                }
            }

            DispatchMap::const_iterator iter = dispatch.find(type);
            if (iter == dispatch.end())
                CV_Error(Error::StsNotImplemented, "Unsupported operator type " + type);

            CALL_MEMBER_FN(*this, iter->second)(*op, type, layerParams, activParams);

            // Collect input blobs
            if (layerParams.blobs.empty()) {
                for (int idx : *op_inputs) {
                    if (layerIds.find(idx) != layerIds.end()) {
                        continue;  // Output from a different layer
                    }
                    Mat blob = allTensors[idx];
                    layerParams.blobs.push_back(blob.u ? blob : blob.clone());  // some tensors are owned by OpenCV
                }
            }

            int dtype = CV_32F;
            if (isInt8(*op)) {
                dtype = CV_8S;
                if (layerParams.type != "Quantize")
                    layerParams.type += "Int8";

                if (!layerParams.has("zeropoints")) {
                    float inpScale, outScale;
                    int inpZero, outZero;
                    getQuantParams(*op, inpScale, inpZero, outScale, outZero);

                    layerParams.set("input_scale", inpScale);
                    layerParams.set("input_zeropoint", inpZero);
                    layerParams.set("scales", outScale);
                    layerParams.set("zeropoints", outZero);
                }
            }
            int layerId = dstNet.addLayer(layerParams.name, layerParams.type, dtype, layerParams);

            // Connect layer to inputs
            int i = 0;
            std::vector<DataLayout> inpLayouts;
            for (int idx : *op_inputs) {
                if (layerIds.find(idx) == layerIds.end()) {
                    continue;  // Const input
                }
                inpLayouts.push_back(layouts[idx]);

                auto it = layerIds.find(idx);
                CV_Assert(it != layerIds.end());
                dstNet.connect(it->second.first, it->second.second, layerId, i++);
            }

            if (!activParams.type.empty()) {
                activParams.name = layerParams.name + "/activ";
                layerId = dstNet.addLayerToPrev(activParams.name, activParams.type, CV_8S, activParams);
            }

            // Predict output layout. Some layer-specific parsers may set them explicitly.
            // Otherwise, propagate input layout.
            if (layouts[op_outputs->Get(0)] == DNN_LAYOUT_UNKNOWN) {
                DataLayout predictedLayout = DNN_LAYOUT_UNKNOWN;
                for (auto layout : inpLayouts) {
                    if (layout != DNN_LAYOUT_UNKNOWN) {
                        if (predictedLayout == DNN_LAYOUT_UNKNOWN)
                            predictedLayout = layout;
                        else if (predictedLayout != layout) {
                            predictedLayout = DNN_LAYOUT_UNKNOWN;
                            break;
                        }
                    }
                }
                layouts[op_outputs->Get(0)] = predictedLayout;
            }

            // Register outputs
            i = 0;
            for (int idx : *op_outputs) {
                layerIds[idx] = std::make_pair(layerId, i++);
            }
        }
        catch (const cv::Exception& e)
        {
            CV_LOG_ERROR(NULL, "DNN/TFLite: Problem during import of operator "
                               << cv::format("[%s]:(%s)", type.c_str(), layerParams.name.c_str())
                               << " (" << op_idx << "/" << all_operators_size << "). Exception: " << e.what());
            if (DNN_DIAGNOSTICS_RUN)
            {
                continue;
            }
            throw;
        }
        // if (op_outputs->Get(0) == 597) {
        //     break;
        // }
    }
}

TFLiteImporter::DispatchMap TFLiteImporter::buildDispatchMap()
{
    static DispatchMap dispatch;
    if (!dispatch.empty())
        return dispatch;

    dispatch["CONV_2D"] = &TFLiteImporter::parseConvolution;
    dispatch["DEPTHWISE_CONV_2D"] = &TFLiteImporter::parseDWConvolution;
    dispatch["RELU"] = dispatch["ADD"] = dispatch["MUL"] = dispatch["PRELU"] =
        dispatch["HARD_SWISH"] = dispatch["LOGISTIC"] = &TFLiteImporter::parseEltwise;
    dispatch["MAX_POOL_2D"] = dispatch["AVERAGE_POOL_2D"] = &TFLiteImporter::parsePooling;
    dispatch["MaxPoolingWithArgmax2D"] = &TFLiteImporter::parsePoolingWithArgmax;
    dispatch["MaxUnpooling2D"] = &TFLiteImporter::parseUnpooling;
    dispatch["PAD"] = &TFLiteImporter::parsePadding;
    dispatch["RESHAPE"] = &TFLiteImporter::parseReshape;
    dispatch["CONCATENATION"] = &TFLiteImporter::parseConcat;
    dispatch["RESIZE_BILINEAR"] = dispatch["RESIZE_NEAREST_NEIGHBOR"] = &TFLiteImporter::parseResize;
    dispatch["Convolution2DTransposeBias"] = &TFLiteImporter::parseDeconvolution;
    dispatch["QUANTIZE"] = &TFLiteImporter::parseQuantize;
    dispatch["DEQUANTIZE"] = &TFLiteImporter::parseDequantize;
    dispatch["TFLite_Detection_PostProcess"] = &TFLiteImporter::parseDetectionPostProcess;
    return dispatch;
}

void TFLiteImporter::parseConvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Convolution";

    auto options = reinterpret_cast<const Conv2DOptions*>(op.builtin_options());

    parseActivation(op, options->fused_activation_function(), activParams);

    layerParams.set("pad_mode", EnumNamePadding(options->padding()));
    layerParams.set("stride_w", options->stride_w());
    layerParams.set("stride_h", options->stride_h());
    layerParams.set("dilation_w", options->dilation_w_factor());
    layerParams.set("dilation_h", options->dilation_h_factor());

    // Get filter size
    int filterIdx = op.inputs()->Get(1);
    Mat filter = allTensors[filterIdx];
    int oc = filter.size[0];
    int kh = filter.size[1];
    int kw = filter.size[2];
    layerParams.set("kernel_w", kw);
    layerParams.set("kernel_h", kh);
    layerParams.set("num_output", oc);

    bool isInt8 = filter.depth() == CV_8S;

    // Fill convolutions blobs here because of two reasons:
    // 1. Kernel transposition
    // 2. Extra blob with kernel scales in case of INT8 mode
    bool hasBias = op.inputs()->size() > 2;
    layerParams.blobs.resize(1 + (int)hasBias + (int)isInt8);
    if (hasBias) {
        Mat bias = allTensors[op.inputs()->Get(2)];
        layerParams.blobs[1] = bias.u ? bias : bias.clone();
    }

    // Reorder filter data from OHWI to OIHW and change shape correspondingly.
    transposeND(filter, {0, 3, 1, 2}, layerParams.blobs[0]);

    if (isInt8) {
        float inpScale, outScale;
        int inpZero, outZero;
        getQuantParams(op, inpScale, inpZero, outScale, outZero);

        layerParams.blobs[2] = Mat(oc, 1, CV_32F);
        auto filterScales = modelTensors->Get(filterIdx)->quantization()->scale();
        if (filterScales->size() == 1) {
            layerParams.blobs[2].setTo(inpScale * filterScales->Get(0) / outScale);
        } else {
            for (size_t i = 0; i < filterScales->size(); ++i) {
                layerParams.blobs[2].at<float>(i) = inpScale * filterScales->Get(i) / outScale;
            }
        }

        if (hasBias) {
            Mat bias = layerParams.blobs[1].reshape(1, oc);
            Mat weights_2d = layerParams.blobs[0].reshape(1, oc);
            for (int i = 0; i < oc; i++)
            {
                bias.at<int>(i) -= inpZero * (cv::sum(weights_2d.row(i))[0]);
            }
        }
    }
}

void TFLiteImporter::parseDWConvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Convolution";

    auto options = reinterpret_cast<const DepthwiseConv2DOptions*>(op.builtin_options());

    parseActivation(op, options->fused_activation_function(), activParams);

    layerParams.set("pad_mode", EnumNamePadding(options->padding()));
    layerParams.set("stride_w", options->stride_w());
    layerParams.set("stride_h", options->stride_h());
    layerParams.set("dilation_w", options->dilation_w_factor());
    layerParams.set("dilation_h", options->dilation_h_factor());

    int filterIdx = op.inputs()->Get(1);
    Mat filter = allTensors[filterIdx];
    int kh = filter.size[1];
    int kw = filter.size[2];
    int oc = filter.size[3];
    layerParams.set("kernel_w", kw);
    layerParams.set("kernel_h", kh);
    layerParams.set("num_output", oc);
    layerParams.set("group", oc);

    bool isInt8 = filter.depth() == CV_8S;

    // Fill convolutions blobs here because of two reasons:
    // 1. Kernel transposition
    // 2. Extra blob with kernel scales in case of INT8 mode
    bool hasBias = op.inputs()->size() > 2;
    layerParams.blobs.resize(1 + (int)hasBias + (int)isInt8);
    if (hasBias) {
        Mat bias = allTensors[op.inputs()->Get(2)];
        layerParams.blobs[1] = bias.u ? bias : bias.clone();
    }

    transposeND(filter, {3, 0, 1, 2}, layerParams.blobs[0]);

    if (isInt8) {
        float inpScale, outScale;
        int inpZero, outZero;
        getQuantParams(op, inpScale, inpZero, outScale, outZero);

        layerParams.blobs[2] = Mat(oc, 1, CV_32F);
        auto filterScales = modelTensors->Get(filterIdx)->quantization()->scale();
        if (filterScales->size() == 1) {
            layerParams.blobs[2].setTo(inpScale * filterScales->Get(0) / outScale);
        } else {
            for (size_t i = 0; i < filterScales->size(); ++i) {
                layerParams.blobs[2].at<float>(i) = inpScale * filterScales->Get(i) / outScale;
            }
        }

        if (hasBias) {
            Mat bias = layerParams.blobs[1].reshape(1, oc);
            Mat weights_2d = layerParams.blobs[0].reshape(1, oc);
            for (int i = 0; i < oc; i++)
            {
                bias.at<int>(i) -= inpZero * (cv::sum(weights_2d.row(i))[0]);
            }
        }
    }
}

void TFLiteImporter::parsePadding(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Padding";
    Mat paddings = allTensors[op.inputs()->Get(1)].clone();

    CV_CheckTypeEQ(paddings.type(), CV_32S, "");
    //  N    H    W    C
    // 0 1  2 3  4 5  6 7
    std::swap(paddings.at<int32_t>(2), paddings.at<int32_t>(6));
    std::swap(paddings.at<int32_t>(3), paddings.at<int32_t>(7));
    //  N    C    W    H
    // 0 1  2 3  4 5  6 7
    std::swap(paddings.at<int32_t>(4), paddings.at<int32_t>(6));
    std::swap(paddings.at<int32_t>(5), paddings.at<int32_t>(7));
    //  N    C    H    W
    // 0 1  2 3  4 5  6 7

    layerParams.set("paddings", DictValue::arrayInt<int32_t*>((int32_t*)paddings.data, paddings.total()));
}

void TFLiteImporter::parseEltwise(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    if (opcode == "ADD") {
        auto options = reinterpret_cast<const AddOptions*>(op.builtin_options());
        parseActivation(op, options->fused_activation_function(), activParams);
        layerParams.type = "Eltwise";
        layerParams.set("operation", "sum");
    }
    else if (opcode == "MUL") {
        auto options = reinterpret_cast<const MulOptions*>(op.builtin_options());
        parseActivation(op, options->fused_activation_function(), activParams);
        layerParams.type = "Eltwise";
        layerParams.set("operation", "prod");
    } else {
        parseActivation(op, opcode, layerParams, /*isFused*/ false);
    }

    if (layerParams.type == "Eltwise" && isInt8(op)) {
        const Tensor* out = modelTensors->Get(op.outputs()->Get(0));
        float outScale = out->quantization()->scale()->Get(0);
        int outZero = out->quantization()->zero_point()->Get(0);

        const size_t numInps = op.inputs()->size();
        std::vector<float> inputScales(numInps);
        std::vector<int> inputZeros(numInps);
        std::vector<float> coeffs(numInps);
        float offset = outZero;
        for (int i = 0; i < numInps; ++i) {
            const Tensor* inp = modelTensors->Get(op.inputs()->Get(i));
            float inpScale = inp->quantization()->scale()->Get(0);
            int inpZero = inp->quantization()->zero_point()->Get(0);
            inputScales[i] = inpScale;
            inputZeros[i] = inpZero;
            coeffs[i] = inpScale / outScale;
            offset -= coeffs[i] * inpZero;
        }

        layerParams.set("input_scales", DictValue::arrayReal(inputScales.data(), numInps));
        layerParams.set("input_zeropoints", DictValue::arrayInt(inputZeros.data(), numInps));
        layerParams.set("coeff", DictValue::arrayReal(coeffs.data(), numInps));
        layerParams.set("offset", offset);
        layerParams.set("scales", outScale);
        layerParams.set("zeropoints", outZero);
    }
}

void TFLiteImporter::parsePooling(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Pooling";

    auto options = reinterpret_cast<const Pool2DOptions*>(op.builtin_options());
    parseActivation(op, options->fused_activation_function(), activParams);

    layerParams.set("pad_mode", EnumNamePadding(options->padding()));
    layerParams.set("stride_w", options->stride_w());
    layerParams.set("stride_h", options->stride_h());
    layerParams.set("kernel_w", options->filter_width());
    layerParams.set("kernel_h", options->filter_height());
    if (opcode == "MAX_POOL_2D")
        layerParams.set("pool", "max");
    else if (opcode == "AVERAGE_POOL_2D")
        layerParams.set("pool", "ave");
    else
        CV_Error(Error::StsNotImplemented, "Pool type selection for " + opcode);
}

void TFLiteImporter::parsePoolingWithArgmax(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Pooling";

    CV_CheckLE(op.custom_options()->size(), sizeof(TfLitePoolParams), "");
    const auto* params = reinterpret_cast<const TfLitePoolParams*>(op.custom_options()->Data());
    if (params->activation != kTfLiteActNone) {
        CV_Error(Error::StsNotImplemented, "Argmax pooling with fused activation");
    }
    if (params->padding != kTfLitePaddingUnknown)
        layerParams.set("pad_mode", params->padding == kTfLitePaddingSame ? "SAME" : "VALID");
    layerParams.set("stride_w", params->stride_width);
    layerParams.set("stride_h", params->stride_height);
    layerParams.set("kernel_w", params->filter_width);
    layerParams.set("kernel_h", params->filter_height);
    layerParams.set("pool", "max");
}

void TFLiteImporter::parseUnpooling(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "MaxUnpool";

    CV_CheckLE(op.custom_options()->size(), sizeof(TfLitePoolParams), "");
    const auto* params = reinterpret_cast<const TfLitePoolParams*>(op.custom_options()->Data());
    if (params->activation != kTfLiteActNone) {
        CV_Error(Error::StsNotImplemented, "Unpooling with fused activation");
    }
    layerParams.set("pool_stride_w", params->stride_width);
    layerParams.set("pool_stride_h", params->stride_height);
    layerParams.set("pool_k_w", params->filter_width);
    layerParams.set("pool_k_h", params->filter_height);
    layerParams.set("pool_pad_w", 0);
    layerParams.set("pool_pad_h", 0);
}

void TFLiteImporter::parseReshape(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    DataLayout inpLayout = layouts[op.inputs()->Get(0)];

    if (inpLayout == DNN_LAYOUT_NHWC) {
        // Permute to NCHW
        std::vector<int> order = {0, 2, 3, 1};
        const std::string name = layerParams.name + "/permute";
        auto inpId = layerIds[op.inputs()->Get(0)];
        int permId = addPermuteLayer(order, name, inpId, isInt8(op) ? CV_8S : CV_32F);  // NCHW -> NHWC
        layerIds[op.inputs()->Get(0)] = std::make_pair(permId, 0);
        layouts[op.outputs()->Get(0)] = DNN_LAYOUT_NCHW;
    }

    layerParams.type = "Reshape";
    std::vector<int> shape;
    if (op.inputs()->size() > 1) {
        shape = allTensors[op.inputs()->Get(1)];
    } else {
        auto options = op.builtin_options_as_ReshapeOptions();
        CV_Assert(options);
        shape.assign(options->new_shape()->begin(), options->new_shape()->end());
    }
    layerParams.set("dim", DictValue::arrayInt<int*>(shape.data(), shape.size()));
}

void TFLiteImporter::parseConcat(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Concat";
    auto options = reinterpret_cast<const ConcatenationOptions*>(op.builtin_options());
    parseActivation(op, options->fused_activation_function(), activParams);

    int axis = options->axis();

    DataLayout inpLayout = layouts[op.inputs()->Get(0)];
    if (inpLayout == DNN_LAYOUT_NHWC) {
        // OpenCV works in NCHW data layout. So change the axis correspondingly.
        axis = normalize_axis(axis, 4);
        static const int remap[] = {0, 2, 3, 1};
        axis = remap[axis];
    }
    layerParams.set("axis", axis);
}

void TFLiteImporter::parseResize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Resize";

    if (opcode == "RESIZE_BILINEAR") {
        auto options = op.builtin_options_as_ResizeBilinearOptions();
        layerParams.set("interpolation", "bilinear");
        layerParams.set("align_corners", options->align_corners());
        layerParams.set("half_pixel_centers", options->half_pixel_centers());
    } else if (opcode == "RESIZE_NEAREST_NEIGHBOR") {
        auto options = op.builtin_options_as_ResizeNearestNeighborOptions();
        layerParams.set("interpolation", "nearest");
        layerParams.set("align_corners", options->align_corners());
        layerParams.set("half_pixel_centers", options->half_pixel_centers());
    }
    Mat shape = allTensors[op.inputs()->Get(1)].reshape(1, 1);
    layerParams.set("height", shape.at<int>(0, 0));
    layerParams.set("width", shape.at<int>(0, 1));
}

int TFLiteImporter::addPermuteLayer(const std::vector<int>& order, const std::string& permName,
                                    const std::pair<int, int>& inpId, int dtype)
{
    LayerParams permLP;
    permLP.set("order", DictValue::arrayInt<const int*>(order.data(), order.size()));
    int permId = dstNet.addLayer(permName, "Permute", dtype, permLP);
    dstNet.connect(inpId.first, inpId.second, permId, 0);
    return permId;
}

void TFLiteImporter::parseDeconvolution(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Deconvolution";

    CV_CheckLE(op.custom_options()->size(), sizeof(TfLiteTransposeConvParams), "");
    const auto* params = reinterpret_cast<const TfLiteTransposeConvParams*>(op.custom_options()->Data());
    if (params->padding != kTfLitePaddingUnknown)
        layerParams.set("pad_mode", params->padding == kTfLitePaddingSame ? "SAME" : "VALID");
    layerParams.set("stride_w", params->stride_width);
    layerParams.set("stride_h", params->stride_height);

    // Get filter size
    int filterIdx = op.inputs()->Get(1);
    Mat filter = allTensors[filterIdx];
    int oc = filter.size[0];
    int kh = filter.size[1];
    int kw = filter.size[2];
    int ic = filter.size[3];
    layerParams.set("kernel_w", kw);
    layerParams.set("kernel_h", kh);
    layerParams.set("num_output", oc);

    // Add adjust padding similar to TensorFlow (see tf_importer)
    const auto* outShape = modelTensors->Get(op.outputs()->Get(0))->shape();
    const int outH = outShape->Get(1);
    const int outW = outShape->Get(2);
    if (params->padding == kTfLitePaddingSame)
    {
        layerParams.set("adj_w", (outW - 1) % params->stride_width);
        layerParams.set("adj_h", (outH - 1) % params->stride_height);
    }
    else if (params->padding == kTfLitePaddingValid)
    {
        layerParams.set("adj_w", (outW - kw) % params->stride_width);
        layerParams.set("adj_h", (outH - kh) % params->stride_height);
    }

    // Reorder filter data from OHWI to IOHW and change shape correspondingly.
    filter = allTensors[filterIdx] = filter.reshape(1, {ic, oc, kh, kw});

    CV_CheckTypeEQ(filter.type(), CV_32F, "");
    Mat filterCopy = filter.clone();
    float* data = filterCopy.ptr<float>();
    float* dstData = filter.ptr<float>();

    int total = oc * ic * kh * kw;
    for (int i_oc = 0; i_oc < oc; i_oc++) {
        for (int i_ic = 0; i_ic < ic; i_ic++) {
            for (int i_h = 0; i_h < kh; i_h++) {
                for (int i_w = 0; i_w < kw; i_w++) {
                    int dst_i = kw * (kh * (oc * i_ic + i_oc) + i_h) + i_w;
                    int src_i = ic * (kw * (kh * i_oc + i_h) + i_w) + i_ic;
                    CV_CheckLT(dst_i, total, "");
                    CV_CheckLT(src_i, total, "");
                    dstData[dst_i] = data[src_i];
                }
            }
        }
    }
}

void TFLiteImporter::parseQuantize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Quantize";

    // const Tensor* out = modelTensors->Get(op.inputs()->Get(0));
    // float outScale = out->quantization()->scale()->Get(0);
    // int outZero = out->quantization()->zero_point()->Get(0);
    // std::cout << outScale << " " << outZero << std::endl;
    layerParams.set("scales", 1);
    layerParams.set("zeropoints", 0);
}

void TFLiteImporter::parseDequantize(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "Dequantize";

    float inpScale, outScale;
    int inpZero, outZero;
    getQuantParams(op, inpScale, inpZero, outScale, outZero);
    layerParams.set("scales", inpScale);
    layerParams.set("zeropoints", inpZero);
}

void TFLiteImporter::parseDetectionPostProcess(const Operator& op, const std::string& opcode, LayerParams& layerParams, LayerParams& activParams) {
    layerParams.type = "DetectionOutput";
    layerParams.set("num_classes", 90);
    layerParams.set("share_location", true);
    layerParams.set("background_label_id", 91);
    layerParams.set("nms_threshold", 0.5);
    layerParams.set("confidence_threshold", 0.0);
    layerParams.set("top_k", 100);
    layerParams.set("keep_top_k", 100);
    layerParams.set("code_type", "CENTER_SIZE");
    layerParams.set("variance_encoded_in_target", true);
    layerParams.set("loc_pred_transposed", true);

    // Replace third input from tensor to Const layer with the priors
    Mat priors = allTensors[op.inputs()->Get(2)].clone();

    // Change priors data from (ycenter, xcenter, h, w) to (xmin, ymin, xmax, ymax)
    priors = priors.reshape(1, priors.total() / 4);
    Mat tmp = priors.col(0).clone();
    priors.col(0) = priors.col(1) - 0.5 * priors.col(3);
    priors.col(1) = tmp - 0.5 * priors.col(2);

    tmp = priors.col(2).clone();
    priors.col(2) = priors.col(0) + priors.col(3);
    priors.col(3) = priors.col(1) + tmp;

    LayerParams priorsLP;
    priorsLP.name = layerParams.name + "/priors";
    priorsLP.type = "Const";
    priorsLP.blobs.resize(1, priors.reshape(1, {1, 1, (int)priors.total()}));

    int priorsId = dstNet.addLayer(priorsLP.name, priorsLP.type, priorsLP);
    layerIds[op.inputs()->Get(2)] = std::make_pair(priorsId, 0);
}

void TFLiteImporter::parseActivation(const Operator& op, ActivationFunctionType type, LayerParams& activParams) {
    parseActivation(op, EnumNameActivationFunctionType(type), activParams);
}

void TFLiteImporter::parseActivation(const Operator& op, const std::string& opcode, LayerParams& activParams, bool isFused) {
    if (opcode == "NONE")
        return;
    else if (opcode == "RELU6")
        activParams.type = "ReLU6";
    else if (opcode == "PRELU")
        activParams.type = "PReLU";
    else if (opcode == "RELU")
        activParams.type = "ReLU";
    else if (opcode == "HARD_SWISH")
        activParams.type = "HardSwish";
    else if (opcode == "LOGISTIC")
        activParams.type = "Sigmoid";
    else
        CV_Error(Error::StsNotImplemented, "Unsupported activation " + opcode);

    if (isInt8(op)) {
        float inpScale, outScale;
        int inpZero, outZero;
        getQuantParams(op, inpScale, inpZero, outScale, outZero);

        if (isFused) {
            activParams.type += "Int8";
            activParams.set("input_scale", outScale);
            activParams.set("input_zeropoint", outZero);
            activParams.set("scales", outScale);
            activParams.set("zeropoints", outZero);
        }

        Mat lookUpTable(1, 256, CV_8S);
        int8_t* table = lookUpTable.ptr<int8_t>();
        for (int i = -128; i < 128; i++) {
            float x, y = i;
            if (isFused)
                x = outScale * (i - outZero);
            else
                x = inpScale * (i - inpZero);

            if (opcode == "RELU6")
                y = std::min(std::max(x, 0.f), 6.f);
            else if (opcode == "LOGISTIC")
                y = 1.0f / (1.0f + std::exp(-x));
            else
                CV_Error(Error::StsNotImplemented, "Lookup table for " + opcode);

            int quantized = outZero + cvRound(y / outScale);
            table[i + 128] = saturate_cast<int8_t>(quantized);
        }
        activParams.blobs.resize(1, lookUpTable);
    }
}

bool TFLiteImporter::isInt8(const Operator& op) {
    const Tensor* out = modelTensors->Get(op.outputs()->Get(0));
    return out->type() == TensorType_INT8;
}

void TFLiteImporter::getQuantParams(const Operator& op, float& inpScale, int& inpZero, float& outScale, int& outZero) {
    const Tensor* inp = modelTensors->Get(op.inputs()->Get(0));
    const Tensor* out = modelTensors->Get(op.outputs()->Get(0));
    inpScale = outScale = inpZero = outZero = 0;
    if (inp->quantization()) {
        if (inp->quantization()->scale()) {
            CV_Assert(inp->quantization()->scale()->size() == 1);
            inpScale = inp->quantization()->scale()->Get(0);
        }
        if (inp->quantization()->zero_point()) {
            CV_Assert(inp->quantization()->zero_point()->size() == 1);
            inpZero = inp->quantization()->zero_point()->Get(0);
        }
    }
    if (out->quantization()) {
        if (out->quantization()->scale()) {
            CV_Assert(out->quantization()->scale()->size() == 1);
            outScale = out->quantization()->scale()->Get(0);
        }
        if (out->quantization()->zero_point()) {
            CV_Assert(out->quantization()->zero_point()->size() == 1);
            outZero = out->quantization()->zero_point()->Get(0);
        }
    }
}

Net readNetFromTFLite(const String &modelPath) {
    Net net;

    std::vector<char> content;

    const std::ios::openmode mode = std::ios::in | std::ios::binary;
    std::ifstream ifs(modelPath, mode);
    if (!ifs.is_open())
        CV_Error(Error::StsError, cv::format("DNN/TFLite: can't open model file '%s'", modelPath.c_str()));

    ifs.seekg(0, std::ios::end);
    const size_t sz = ifs.tellg();
    CV_Assert(sz > 0);
    content.resize(sz);
    ifs.seekg(0, std::ios::beg);

    ifs.read(content.data(), sz);
    CV_Assert(!ifs.bad());

    TFLiteImporter(net, content.data(), content.size());
    return net;
}

Net readNetFromTFLite(const std::vector<uchar>& bufferModel) {
    return readNetFromTFLite((const char*)bufferModel.data(), bufferModel.size());
}

Net readNetFromTFLite(const char *bufferModel, size_t bufSize) {
    Net net;
    TFLiteImporter(net, bufferModel, bufSize);
    return net;
}

#else  // HAVE_FLATBUFFERS

#define DNN_TFLITE_UNSUPPORTED() CV_Error(Error::StsError, "DNN/TFLite: Build OpenCV with FlatBuffers to import TFLite models: https://github.com/opencv/opencv/pull/23161")

Net readNetFromTFLite(const String &) {
    DNN_TFLITE_UNSUPPORTED();
}

Net readNetFromTFLite(const std::vector<uchar>&) {
    DNN_TFLITE_UNSUPPORTED();
}

Net readNetFromTFLite(const char *, size_t) {
    DNN_TFLITE_UNSUPPORTED();
}

#endif  // HAVE_FLATBUFFERS

CV__DNN_INLINE_NS_END
}}  // namespace cv::dnn
