// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Copyright (C) 2020 Intel Corporation

#include "precomp.hpp"
#include "backends/onnx/gonnxbackend.hpp"

#ifdef HAVE_ONNX

#include <ade/util/algorithm.hpp> // any_of
#include <ade/util/zip_range.hpp>
#include <opencv2/gapi/infer.hpp>
#include <opencv2/gapi/own/convert.hpp>
#include <opencv2/gapi/gframe.hpp>

#include "api/gbackend_priv.hpp" // FIXME: Make it part of Backend SDK!

namespace {
struct ONNXCallContext;
}

namespace cv {
namespace gimpl {
namespace onnx {

enum TensorPosition : int {
    INPUT,
    OUTPUT
};

struct TensorInfo {
    TensorInfo() = default;
    explicit TensorInfo(const Ort::TensorTypeAndShapeInfo& info)
        : dims(info.GetShape())
        , type(info.GetElementType())
        , is_dynamic(std::find(dims.begin(), dims.end(), -1) != dims.end()) {
        if (!is_dynamic) {
            size = std::accumulate(dims.begin(),
                                   dims.end(),
                                   static_cast<int64_t>(1),
                                   std::multiplies<int64_t>());
        }
        // Heuristic: check if the tensor is grayscale input
        if (dims.size() == 4u
            && dims[0]  == 1
            && dims[1]  == 1
            && dims[2]   > 1
            && dims[3]   > 1) {
            is_grayscale = true;
        }
    }

    std::string name;
    std::vector<int64_t> dims;
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    int64_t size = -1;

    bool normalize = true;

    bool is_dynamic = false;
    bool is_grayscale = false;

    struct MeanStdev {
        cv::Scalar mean;
        cv::Scalar stdev;
    };
    cv::util::optional<MeanStdev> mstd;
};

using Views = std::vector<std::unique_ptr<cv::MediaFrame::View>>;

class ONNXCompiled {
    // ONNX Resources
    // NOTE: Env must live with the session, otherwise segfaults.
    Ort::Env this_env{nullptr};
    Ort::Session this_session{nullptr};
    Ort::MemoryInfo this_memory_info{nullptr};

    std::vector<TensorInfo> in_tensor_info;
    std::vector<TensorInfo> out_tensor_info;
    bool is_dynamic = false;

    // G-API <Net> description
    gapi::onnx::detail::ParamDesc params;

    // Input/output tensor information
    std::vector<TensorInfo> getTensorInfo(TensorPosition pos);

    // Run-time data structures
    std::vector<cv::Mat> in_data;
    std::vector<cv::Mat> out_data;

    void Run(const std::vector<cv::Mat>& ins,
             const std::vector<cv::Mat>& outs);

public:
    explicit ONNXCompiled(const gapi::onnx::detail::ParamDesc &pp);

    // Extract the information about output layer #i
    cv::GMatDesc outMeta(int i) const;

    // Assign input/output info
    std::size_t numInputs() const { return params.num_in; }
    std::size_t numOutputs() const { return params.num_out; }
    void setInput(int i, const cv::Mat &m);
    void setInput(ONNXCallContext &ctx, const int idx, const int name_idx, Views &views, const cv::Rect &roi);
    void setOutput(int idx, cv::Mat &m);
    cv::Mat allocOutput(int i) const;

    // Run with the assigned inputs/outputs
    void run();
};

} // namespace onnx
} // namespace gimpl
} // namespace cv

namespace {

inline std::vector<const char*> getCharNames(const std::vector<std::string>& names) {
    std::vector<const char*> out_vec;
    for (const auto& el : names) {
            out_vec.push_back(el.data());
    }
    return out_vec;
}

inline int getIdxByName(const std::vector<cv::gimpl::onnx::TensorInfo>& info, const std::string& name) {
    // FIXME: Cache the ordering
    const auto it = std::find_if(info.begin(), info.end(), [&](const cv::gimpl::onnx::TensorInfo &i) {
            return i.name == name;
        });
    GAPI_Assert(it != info.end());
    return std::distance(info.begin(), it);
}

inline int toCV(ONNXTensorElementDataType prec) {
    switch (prec) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return CV_8U;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return CV_32F;
    default: GAPI_Assert(false && "Unsupported data type");
    }
    return -1;
}

inline std::vector<int> toCV(const std::vector<int64_t> &vsz) {
    std::vector<int> result;
    result.reserve(vsz.size());
    for (auto sz : vsz) {
        result.push_back(ade::util::checked_cast<int>(sz));
    }
    return result;
}

inline cv::Mat toCV(Ort::Value &v) {
    auto info = v.GetTensorTypeAndShapeInfo();
    return cv::Mat(toCV(info.GetShape()),
                   toCV(info.GetElementType()),
                   reinterpret_cast<void*>(v.GetTensorMutableData<uint8_t*>()));
}

inline std::vector<int64_t> toORT(const cv::MatSize &sz) {
    return cv::to_own<int64_t>(sz);
}

inline void preprocess(const cv::Mat& src,
                       const cv::gimpl::onnx::TensorInfo& ti,
                             cv::Mat& dst) {
    GAPI_Assert(src.depth() == CV_32F || src.depth() == CV_8U);

    if (src.depth() == CV_32F) {
        // Just pass the tensor as-is.
        // No layout or dimension transformations done here!
        // TODO: This needs to be aligned across all NN backends.
        GAPI_Assert(toCV(ti.type) == CV_32F && "Only 32F model input is supported for 32F data");
        const auto tensor_dims = toORT(src.size);
        if (tensor_dims.size() == ti.dims.size()) {
            for (size_t i = 0; i < ti.dims.size(); ++i) {
                GAPI_Assert((ti.dims[i] == -1 || ti.dims[i] == tensor_dims[i]) &&
                            "32F tensor dimensions should match with all non-dynamic NN input dimensions");
            }
        } else {
            GAPI_Assert(false && "32F tensor size should match with NN input");
        }

        dst = src;
    } else {
        // 8U input: full preprocessing path
        GAPI_Assert(src.depth()   == CV_8U && "Only 8U data type is supported for preproc");
        GAPI_Assert(ti.dims.size() == 4u && "Only NCHW/NHWC layouts are supported for preproc");

        const auto ddepth = toCV(ti.type);
        GAPI_Assert((ddepth == CV_8U || ddepth == CV_32F)
                    && "Only 8U and 32F model input is supported for 8U data");

        // Assess the expected input layout
        const bool is_hwc = [&](int ch) {
            if (ti.is_grayscale)       return false; // 1,1,h,w
            else if (ti.dims[3] == ch) return true;  // _,_,_,c
            else if (ti.dims[1] == ch) return false; // _,c,_,_
            else cv::util::throw_error(std::logic_error("Couldn't identify input tensor layout"));
        } (src.channels());

        int new_c = src.channels();
        cv::Mat csc;
        if (ti.is_grayscale && new_c == 3) {
            cv::cvtColor(src, csc, cv::COLOR_BGR2GRAY);
            new_c = 1;
        } else {
            csc = src;
        }

        // NHWC vs NCHW
        int new_h = -1, new_w = -1;
        if (ti.is_dynamic) {
            // reuse h & w from the input image
            new_h = src.rows;
            new_w = src.cols;
        } else {
            // take h & w from the ONNX tensor info
            new_h = ti.dims[is_hwc ? 1 : 2];
            new_w = ti.dims[is_hwc ? 2 : 3];
        }
        GAPI_Assert(new_h != -1 && new_w != -1);

        cv::Mat rsz, pp;
        cv::resize(csc, rsz, cv::Size(new_w, new_h));
        if (src.depth() == CV_8U && ddepth == CV_32F) {
            rsz.convertTo(pp, ddepth, ti.normalize ? 1.f / 255 : 1.f);
            if (ti.mstd.has_value()) {
                pp -= ti.mstd->mean;
                pp /= ti.mstd->stdev;
            }
        } else {
            pp = rsz;
        }

        if (!is_hwc && new_c > 1) {
            // Convert to CHW
            dst.create(cv::Size(new_w, new_h * new_c), ddepth);
            std::vector<cv::Mat> planes(new_c);
            for (int ch = 0; ch < new_c; ++ch) {
                planes[ch] = dst.rowRange(ch * new_h, (ch + 1) * new_h);
            }
            cv::split(pp, planes);
        } else {
            // Keep HWC
            dst = pp;
        }

        // Ensure dst is a tensor shape (not a 2D image)
        if (ti.is_dynamic) {
            // Reshape to input dimensions
            const std::vector<int> out_dims = is_hwc
                ? std::vector<int>{1, new_h, new_w, new_c}
                : std::vector<int>{1, new_c, new_h, new_w};
            dst = dst.reshape(1, out_dims);
        } else {
            // Reshape to ONNX dimensions (no -1s there!)
            dst = dst.reshape(1, toCV(ti.dims));
        }
    }
}

void preprocess(const std::unique_ptr<cv::MediaFrame::View>& view,
                const cv::GFrameDesc& desc,
                const cv::gimpl::onnx::TensorInfo& ti,
                const cv::Rect& roi,
                      cv::Mat& dst) {
    cv::Mat pp;
    switch (desc.fmt) {
        case cv::MediaFormat::BGR: {
            pp = cv::Mat(desc.size, CV_8UC3, view->ptr[0], view->stride[0]);
            break;
        }
        case cv::MediaFormat::NV12: {
            const auto y_plane  = cv::Mat(desc.size, CV_8UC1, view->ptr[0], view->stride[0]);
            const auto uv_plane = cv::Mat(desc.size / 2, CV_8UC2, view->ptr[1], view->stride[1]);
            cvtColorTwoPlane(y_plane, uv_plane, pp, cv::COLOR_YUV2BGR_NV12);
            break;
        }
        default:
            GAPI_Assert(false && "Unsupported media format for ONNX backend");
    }
    preprocess(roi.empty() ? pp : pp(roi), ti, dst);
}

template <typename T>
inline Ort::Value createTensor(const Ort::MemoryInfo& memory_info,
                               const cv::gimpl::onnx::TensorInfo& tensor_params,
                               const cv::Mat& data) {
    (void) tensor_params;
    auto ort_dims = toORT(data.size);
    return Ort::Value::CreateTensor<T>(memory_info,
                                       const_cast<T*>(data.ptr<T>()),
                                       data.total(),
                                       ort_dims.data(),
                                       ort_dims.size());
}

inline Ort::Value createTensor(const Ort::MemoryInfo& memory_info,
                               const cv::gimpl::onnx::TensorInfo& tensor_params,
                               const cv::Mat& data) {
    GAPI_Assert(data.isContinuous ());
    switch (tensor_params.type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
        return createTensor<uint8_t>(memory_info, tensor_params, data);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
        return createTensor<float>(memory_info, tensor_params, data);
    default:
        GAPI_Assert(false && "Unsupported data type");
    }
    return Ort::Value{nullptr};
}

struct ONNXUnit {
    static const char *name() { return "ONNXModelConfig"; }

    std::shared_ptr<cv::gimpl::onnx::ONNXCompiled> oc;

    explicit ONNXUnit(const cv::gapi::onnx::detail::ParamDesc &pp)
        : oc(new cv::gimpl::onnx::ONNXCompiled(pp)) {
    }
};

struct ONNXCallContext {
    // Input parameters passed to an inference operation.
    std::vector<cv::GArg> args;
    cv::GShapes in_shapes;
    //FIXME: avoid conversion of arguments from internal representation to OpenCV one on each call
    //to OCV kernel. (This can be achieved by a two single time conversions in GCPUExecutable::run,
    //once on enter for input and output arguments, and once before return for output arguments only
    //FIXME: check if the above applies to this backend (taken from CPU)
    std::unordered_map<std::size_t, cv::GRunArgP> results;

    // Generic accessor API
    template<typename T>
    const T& inArg(std::size_t input) { return args.at(input).get<T>(); }

    // Syntax sugar
    const cv::Mat&   inMat(std::size_t input) {
        return inArg<cv::Mat>(input);
    }

    const cv::MediaFrame& inFrame(std::size_t input) {
        return inArg<cv::MediaFrame>(input);
    }

    cv::Mat&         outMatR(std::size_t output) {
        return *cv::util::get<cv::Mat*>(results.at(output));
    }

    template<typename T> std::vector<T>& outVecR(std::size_t output) { // FIXME: the same issue
        return outVecRef(output).wref<T>();
    }
    cv::detail::VectorRef& outVecRef(std::size_t output) {
        return cv::util::get<cv::detail::VectorRef>(results.at(output));
    }
};

struct ONNXCallable {
    static const char *name() { return "ONNXRequestCallable"; }
    using Run = std::function<void(const ONNXUnit &, ONNXCallContext &)>;
    Run run;
};

struct KImpl {
    cv::gimpl::CustomMetaFunction::CM customMetaFunc;
    ONNXCallable::Run run;
};

// FIXME: Is there a way to take a typed graph (our GModel),
// and create a new typed graph _ATOP_ of that (by extending with a couple of
// new types?).
// Alternatively, is there a way to compose types graphs?
//
// If not, we need to introduce that!
using GONNXModel = ade::TypedGraph
    < cv::gimpl::Protocol
    , cv::gimpl::Op
    , cv::gimpl::NetworkParams
    , cv::gimpl::CustomMetaFunction
    , ONNXUnit
    , ONNXCallable
    >;

// FIXME: Same issue with Typed and ConstTyped
using GConstGONNXModel = ade::ConstTypedGraph
    < cv::gimpl::Protocol
    , cv::gimpl::Op
    , cv::gimpl::NetworkParams
    , cv::gimpl::CustomMetaFunction
    , ONNXUnit
    , ONNXCallable
    >;
} // anonymous namespace

// GCPUExcecutable implementation //////////////////////////////////////////////
cv::gimpl::onnx::GONNXExecutable::GONNXExecutable(const ade::Graph &g,
                                                  const std::vector<ade::NodeHandle> &nodes)
    : m_g(g), m_gm(m_g) {
    // FIXME: Currently this backend is capable to run a single inference node only.
    // Need to extend our island fusion with merge/not-to-merge decision making parametrization
    GConstGONNXModel iem(g);

    for (auto &nh : nodes) {
        switch (m_gm.metadata(nh).get<NodeType>().t) {
        case NodeType::OP:
            if (this_nh == nullptr) {
                this_nh = nh;
            }
            else {
                util::throw_error(std::logic_error("Multi-node inference is not supported!"));
            }
            break;

        case NodeType::DATA: {
            m_dataNodes.push_back(nh);
            const auto &desc = m_gm.metadata(nh).get<Data>();
            if (desc.storage == Data::Storage::CONST_VAL) {
                util::throw_error(std::logic_error("No const data supported in backend!"));
            }
            if (desc.storage == Data::Storage::INTERNAL) {
                util::throw_error(std::logic_error("No internal data supported in backend!"));
            }
            break;
        }
        default: util::throw_error(std::logic_error("Unsupported NodeType"));
        }
    }
}

// FIXME: Document what it does
cv::GArg cv::gimpl::onnx::GONNXExecutable::packArg(const cv::GArg &arg) {
    // No API placeholders allowed at this point
    // FIXME: this check has to be done somewhere in compilation stage.
    GAPI_Assert(   arg.kind != cv::detail::ArgKind::GMAT
                && arg.kind != cv::detail::ArgKind::GSCALAR
                && arg.kind != cv::detail::ArgKind::GARRAY
                && arg.kind != cv::detail::ArgKind::GOPAQUE
                && arg.kind != cv::detail::ArgKind::GFRAME);

    if (arg.kind != cv::detail::ArgKind::GOBJREF) {
        util::throw_error(std::logic_error("Inference supports G-types ONLY!"));
    }
    GAPI_Assert(arg.kind == cv::detail::ArgKind::GOBJREF);

    // Wrap associated CPU object (either host or an internal one)
    // FIXME: object can be moved out!!! GExecutor faced that.
    const cv::gimpl::RcDesc &ref = arg.get<cv::gimpl::RcDesc>();
    switch (ref.shape)
    {
    case GShape::GMAT:    return GArg(m_res.slot<cv::Mat>()[ref.id]);

    // Note: .at() is intentional for GArray as object MUST be already there
    //   (and constructed by either bindIn/Out or resetInternal)
    case GShape::GARRAY:  return GArg(m_res.slot<cv::detail::VectorRef>().at(ref.id));

    // Note: .at() is intentional for GOpaque as object MUST be already there
    //   (and constructed by either bindIn/Out or resetInternal)
    case GShape::GOPAQUE:  return GArg(m_res.slot<cv::detail::OpaqueRef>().at(ref.id));

    case GShape::GFRAME:   return GArg(m_res.slot<cv::MediaFrame>().at(ref.id));

    default:
        util::throw_error(std::logic_error("Unsupported GShape type"));
        break;
    }
}

void cv::gimpl::onnx::GONNXExecutable::run(std::vector<InObj>  &&input_objs,
                                           std::vector<OutObj> &&output_objs) {
    // Update resources with run-time information - what this Island
    // has received from user (or from another Island, or mix...)
    // FIXME: Check input/output objects against GIsland protocol

    for (auto& it : input_objs)   magazine::bindInArg (m_res, it.first, it.second);
    for (auto& it : output_objs)  magazine::bindOutArg(m_res, it.first, it.second);

    // FIXME: Running just a single node now.
    // Not sure if need to support many of them, though
    // FIXME: Make this island-unmergeable?
    const auto &op = m_gm.metadata(this_nh).get<Op>();

    // Initialize kernel's execution context:
    // - Input parameters
    ONNXCallContext context;
    context.args.reserve(op.args.size());
    using namespace std::placeholders;
    ade::util::transform(op.args,
                         std::back_inserter(context.args),
                         std::bind(&GONNXExecutable::packArg, this, _1));

    // NB: Need to store inputs shape to recognize GFrame/GMat
    ade::util::transform(op.args,
                         std::back_inserter(context.in_shapes),
                         [](const cv::GArg& arg) {
                             return arg.get<cv::gimpl::RcDesc>().shape;
                         });

    // - Output parameters.
    for (const auto &out_it : ade::util::indexed(op.outs)) {
        // FIXME: Can the same GArg type resolution mechanism be reused here?
        const auto out_port  = ade::util::index(out_it);
        const auto out_desc  = ade::util::value(out_it);
        context.results[out_port] = magazine::getObjPtr(m_res, out_desc);
    }

    // And now trigger the execution
    GConstGONNXModel giem(m_g);
    const auto &uu = giem.metadata(this_nh).get<ONNXUnit>();
    const auto &kk = giem.metadata(this_nh).get<ONNXCallable>();
    kk.run(uu, context);

    for (auto &it : output_objs) magazine::writeBack(m_res, it.first, it.second);
}

namespace cv {
namespace gimpl {
namespace onnx {

ONNXCompiled::ONNXCompiled(const gapi::onnx::detail::ParamDesc &pp)
    : params(pp) {

    // Validate input parameters before allocating any resources
    if (params.num_in > 1u && params.num_in != params.input_names.size()) {
        cv::util::throw_error(std::logic_error("Please specify input layer names for "
                                               + params.model_path));
    }
    if (params.num_out > 1u && params.num_out != params.output_names.size()) {
        cv::util::throw_error(std::logic_error("Please specify output layer names for "
                                               + params.model_path));
    }

    // Create and initialize the ONNX session
    Ort::SessionOptions session_options;
    this_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "");
    this_session = Ort::Session(this_env, params.model_path.data(), session_options);
    this_memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    in_tensor_info = getTensorInfo(INPUT);
    out_tensor_info = getTensorInfo(OUTPUT);

    const auto is_dyn = [](const TensorInfo &ti) {
        return ti.is_dynamic;
    };
    is_dynamic = ade::util::any_of(in_tensor_info, is_dyn)
              || ade::util::any_of(out_tensor_info, is_dyn);
    if (is_dynamic && !params.custom_post_proc) {
        util::throw_error(std::logic_error("This network has dynamic shapes. "
                                           "Please provide a custom post-processing function "
                                           "(.cfgPostProc) in network parameters"));
    }

    // Update parameters based on session information
    if (params.num_in == 1u && params.input_names.empty()) {
        params.input_names = { in_tensor_info.front().name };
    }
    if (params.num_out == 1u && params.output_names.empty()) {
        params.output_names = { out_tensor_info.front().name };
    }

    // Validate what is supported currently
    GAPI_Assert(params.const_inputs.empty()
                && "Const inputs are not currently supported");
    GAPI_Assert(std::all_of(in_tensor_info.begin(),
                            in_tensor_info.end(),
                            [](const cv::gimpl::onnx::TensorInfo &p) {
                                return p.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
                                    || p.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
                            })
                && "Only FP32 and U8 inputs for NN are supported");

    // Put mean and std in appropriate tensor params
    if (!params.mean.empty() || !params.stdev.empty()) {
        GAPI_Assert(params.mean.size() == params.stdev.size() &&
                    params.mean.size() == params.input_names.size());
        for (auto idx : ade::util::iota(params.num_in)) {
            const auto ort_idx = getIdxByName(in_tensor_info, params.input_names[idx]);
            using M = TensorInfo::MeanStdev;
            in_tensor_info[ort_idx].mstd = util::make_optional(M{ params.mean[idx]
                                                                , params.stdev[idx] });
        }
    }

    // Update normalize flags for input tensors
    if (!params.normalize.empty()) {
        for (auto idx : ade::util::iota(params.num_in)) {
            const auto ort_idx = getIdxByName(in_tensor_info, params.input_names[idx]);
            in_tensor_info[ort_idx].normalize = params.normalize[idx];
        }
    }

    // Pre-allocate vectors (not buffers) for runtime info
    in_data.resize(params.num_in);
    out_data.resize(params.num_out);
}

std::vector<TensorInfo> ONNXCompiled::getTensorInfo(TensorPosition pos) {
    GAPI_Assert(pos == INPUT || pos == OUTPUT);

    const auto num_nodes = pos == INPUT
        ? this_session.GetInputCount()
        : this_session.GetOutputCount();

    std::vector<TensorInfo> tensor_info;
    tensor_info.reserve(num_nodes);

    Ort::AllocatorWithDefaultOptions allocator;
    for (auto i : ade::util::iota(num_nodes)) {
        const auto info = pos == INPUT
            ? this_session.GetInputTypeInfo(i)
            : this_session.GetOutputTypeInfo(i);
        tensor_info.emplace_back(info.GetTensorTypeAndShapeInfo());

        char *name_p = pos == INPUT
            ? this_session.GetInputName(i, allocator)
            : this_session.GetOutputName(i, allocator);
        tensor_info.back().name = name_p;
        allocator.Free(name_p);
    }

    return tensor_info;
}

cv::GMatDesc ONNXCompiled::outMeta(int idx) const {
    if (is_dynamic) {
        GAPI_Assert(!params.out_metas.empty()
                    && "Metadata must be specified if NN has dynamic inputs!");
        return params.out_metas.at(idx);
    }
    const auto ort_idx = getIdxByName(out_tensor_info, params.output_names[idx]);
    return cv::GMatDesc(toCV(out_tensor_info[ort_idx].type),
                        toCV(out_tensor_info[ort_idx].dims));
}

void ONNXCompiled::setInput(int i, const cv::Mat &m) {
    const auto in_idx  = i;
    const auto in_name = params.input_names[in_idx];
    const auto ort_idx = getIdxByName(in_tensor_info, in_name);
    preprocess(m, in_tensor_info[ort_idx], in_data[in_idx]);
}

void ONNXCompiled::setInput(ONNXCallContext &ctx,
                            const int in_idx,
                            const int name_idx,
                            Views& views,
                            const cv::Rect& roi = cv::Rect()) {
    const auto in_name = params.input_names[name_idx];
    const auto ort_idx = getIdxByName(in_tensor_info, in_name);

    switch (ctx.in_shapes[in_idx]) {
        case cv::GShape::GFRAME: {
            const cv::MediaFrame& frame = ctx.inFrame(in_idx);
            views.emplace_back(new cv::MediaFrame::View(frame.access(cv::MediaFrame::Access::R)));
            preprocess(views.back(), frame.desc(), in_tensor_info[ort_idx], roi, in_data[name_idx]);
            break;
        }
        case cv::GShape::GMAT: {
            preprocess(roi.empty() ? ctx.inMat(in_idx) : ctx.inMat(in_idx)(roi),
                       in_tensor_info[ort_idx],
                       in_data[name_idx]);
            break;
        }
        default: {
            GAPI_Assert("Unsupported input shape for ONNX backend");
        }
    }
}

void ONNXCompiled::setOutput(int i, cv::Mat &m) {
    // FIXME: No need in double-indexing?
    out_data[i] = m;
}

cv::Mat ONNXCompiled::allocOutput(int i) const {
    cv::Mat m;
    m.create(toCV(out_tensor_info[i].dims),
             toCV(out_tensor_info[i].type));
    return m;
}

void ONNXCompiled::Run(const std::vector<cv::Mat>& ins,
                       const std::vector<cv::Mat>& outs) {
    std::vector<Ort::Value> in_tensors, out_tensors;

    auto in_run_names  = getCharNames(params.input_names);

    for (const auto it : ade::util::indexed(params.input_names)) {
        auto i         = ade::util::index(it);
        auto in_name   = ade::util::value(it);
        const auto idx = getIdxByName(in_tensor_info, in_name);
        in_tensors.emplace_back(createTensor(this_memory_info,
                                             in_tensor_info[idx],
                                             ins[i]));
    }

    if (!is_dynamic) {
        // Easy path - just run the session which is bound to G-API's
        // internal data
        for (auto i : ade::util::iota(params.output_names.size())) {
        out_tensors.emplace_back(createTensor(this_memory_info,
                                              out_tensor_info[i],
                                              outs[i]));
        }
        auto out_run_names = getCharNames(params.output_names);
        this_session.Run(Ort::RunOptions{nullptr},
                         in_run_names.data(),
                         &in_tensors.front(),
                         params.input_names.size(),
                         out_run_names.data(),
                         &out_tensors.front(),
                         params.output_names.size());
    } else {
        // Hard path - run session & user-defined post-processing
        // NOTE: use another list of output names here
        std::vector<const char*> out_names;
        for (auto &&ti : out_tensor_info) {
            out_names.push_back(ti.name.c_str());
        }

        auto outputs = this_session.Run(Ort::RunOptions{nullptr},
                                        in_run_names.data(),
                                        &in_tensors.front(),
                                        params.input_names.size(),
                                        out_names.data(),
                                        out_names.size());
        std::unordered_map<std::string, cv::Mat> onnx_outputs;
        std::unordered_map<std::string, cv::Mat> gapi_outputs;

        GAPI_Assert(outputs.size() == out_names.size());
        // Fill in ONNX tensors
        for (auto &&iter : ade::util::zip(ade::util::toRange(out_tensor_info),
                                          ade::util::toRange(outputs))) {
            const auto &out_name   = std::get<0>(iter).name;
                  auto &out_tensor = std::get<1>(iter);
            onnx_outputs[out_name] = toCV(out_tensor);
        }

        // Fill in G-API outputs
        for (auto &&it: ade::util::indexed(params.output_names)) {
            gapi_outputs[ade::util::value(it)] = outs[ade::util::index(it)];
        }
        params.custom_post_proc(onnx_outputs, gapi_outputs);
    }
}

void ONNXCompiled::run() {
    Run(in_data, out_data);
}

static void checkInputMeta(const cv::GMetaArg mm) {
    switch (mm.index()) {
        case cv::GMetaArg::index_of<cv::GMatDesc>(): break;
        case cv::GMetaArg::index_of<cv::GFrameDesc>(): {
            const auto &meta = util::get<cv::GFrameDesc>(mm);
            switch (meta.fmt) {
                case cv::MediaFormat::NV12: break;
                case cv::MediaFormat::BGR:  break;
                default:
                    GAPI_Assert(false && "Unsupported media format for ONNX backend");
            } break;
        } break;
        default:
            util::throw_error(std::runtime_error("Unsupported input meta for ONNX backend"));
    }
}

struct Infer: public cv::detail::KernelTag {
    using API = cv::GInferBase;
    static cv::gapi::GBackend backend()  { return cv::gapi::onnx::backend(); }
    static KImpl kernel()                { return KImpl{outMeta, run}; }

    static cv::GMetaArgs outMeta(const ade::Graph      &gr,
                                 const ade::NodeHandle &nh,
                                 const cv::GMetaArgs   &in_metas,
                                 const cv::GArgs       &/*in_args*/) {
        cv::GMetaArgs result;

        GConstGONNXModel gm(gr);
        const auto &uu = gm.metadata(nh).get<ONNXUnit>();

        GAPI_Assert(uu.oc->numInputs() == in_metas.size()
                    && "Known input layers count doesn't match input meta count");
        for (auto &&mm : in_metas) {
            checkInputMeta(mm);
        }
        for (auto &&idx : ade::util::iota(uu.oc->numOutputs())) {
            result.emplace_back(uu.oc->outMeta(idx));
        }
        return result;
    }

    static void run(const ONNXUnit &uu, ONNXCallContext &ctx) {
        Views views;
        for (auto &&idx : ade::util::iota(uu.oc->numInputs())) {
            uu.oc->setInput(ctx, idx, idx, views);
        }
        for (auto &&idx : ade::util::iota(uu.oc->numOutputs())) {
            uu.oc->setOutput(idx, ctx.outMatR(idx));
        }
        uu.oc->run();
    }
};

struct InferROI: public cv::detail::KernelTag {
    using API = cv::GInferROIBase;
    static cv::gapi::GBackend backend()  { return cv::gapi::onnx::backend(); }
    static KImpl kernel()                { return KImpl{outMeta, run}; }

    static cv::GMetaArgs outMeta(const ade::Graph      &gr,
                                 const ade::NodeHandle &nh,
                                 const cv::GMetaArgs   &in_metas,
                                 const cv::GArgs       &/*in_args*/) {
        cv::GMetaArgs result;

        GConstGONNXModel gm(gr);
        const auto &uu = gm.metadata(nh).get<ONNXUnit>();
        GAPI_Assert(1u == uu.oc->numInputs());
        GAPI_Assert(2u == in_metas.size());
        checkInputMeta(in_metas.at(1));
        for (auto &&idx : ade::util::iota(uu.oc->numOutputs())) {
            result.emplace_back(uu.oc->outMeta(idx));
        }
        return result;
    }

    static void run(const ONNXUnit &uu, ONNXCallContext &ctx) {
        Views views;
        // non-generic version for now, per the InferROI's definition
        GAPI_Assert(uu.oc->numInputs() == 1u);
        const auto& this_roi = ctx.inArg<cv::detail::OpaqueRef>(0).rref<cv::Rect>();
        uu.oc->setInput(ctx, 1, 0, views, this_roi);
        for (auto &&idx : ade::util::iota(uu.oc->numOutputs())) {
            uu.oc->setOutput(idx, ctx.outMatR(idx));
        }
        uu.oc->run();
    }
};

struct InferList: public cv::detail::KernelTag {
    using API = cv::GInferListBase;
    static cv::gapi::GBackend backend()  { return cv::gapi::onnx::backend(); }
    static KImpl kernel()                { return KImpl{outMeta, run}; }

    static cv::GMetaArgs outMeta(const ade::Graph      &gr,
                                 const ade::NodeHandle &nh,
                                 const cv::GMetaArgs   &in_metas,
                                 const cv::GArgs       &/*in_args*/) {
        GConstGONNXModel gm(gr);
        const auto &uu = gm.metadata(nh).get<ONNXUnit>();

        // Note our input layers list order matches the API order and so
        // meta order.
        GAPI_Assert(uu.oc->numInputs() == (in_metas.size() - 1u)
                    && "Known input layers count doesn't match input meta count");

        for (auto i : ade::util::iota(uu.oc->numInputs())) {
            const auto &mm = in_metas[i + 1];
            checkInputMeta(mm);
        }

        // roi-list version is much easier at the moment.
        // All our outputs are vectors which don't have
        // metadata at the moment - so just create a vector of
        // "empty" array metadatas of the required size.
        return cv::GMetaArgs(uu.oc->numOutputs(),
                             cv::GMetaArg{cv::empty_array_desc()});
    }

    static void run(const ONNXUnit &uu, ONNXCallContext &ctx) {
        Views views;
        // non-generic version for now:
        // - assumes input 0 is always ROI list
        // - assumes all inputs/outputs are always Mats
        GAPI_Assert(uu.oc->numInputs() == 1); // roi list is not counted in net's inputs

        const auto& in_roi_vec = ctx.inArg<cv::detail::VectorRef>(0u).rref<cv::Rect>();

        for (auto i : ade::util::iota(uu.oc->numOutputs())) {
            ctx.outVecR<cv::Mat>(i).clear();
        }
        for (const auto &rc : in_roi_vec) {
            uu.oc->setInput(ctx, 1, 0, views, rc);
            std::vector<cv::Mat> out_mats(uu.oc->numOutputs());
            for (auto i : ade::util::iota(uu.oc->numOutputs())) {
                out_mats[i] = uu.oc->allocOutput(i);
                uu.oc->setOutput(i, out_mats[i]);
            }
            uu.oc->run();
            for (auto i : ade::util::iota(uu.oc->numOutputs())) {
                std::vector<cv::Mat> &out_vec = ctx.outVecR<cv::Mat>(i);
                out_vec.push_back(std::move(out_mats[i]));
            }
        }
    }
};

struct InferList2: public cv::detail::KernelTag {
    using API = cv::GInferList2Base;
    static cv::gapi::GBackend backend()  { return cv::gapi::onnx::backend(); }
    static KImpl kernel()                { return KImpl{outMeta, run}; }

    static cv::GMetaArgs outMeta(const ade::Graph      &gr,
                                 const ade::NodeHandle &nh,
                                 const cv::GMetaArgs   &in_metas,
                                 const cv::GArgs       &/*in_args*/) {

        GConstGONNXModel gm(gr);
        const auto &uu = gm.metadata(nh).get<ONNXUnit>();

        // Note our input layers list order matches the API order and so
        // meta order.
        GAPI_Assert(uu.oc->numInputs() == (in_metas.size() - 1u)
                    && "Known input layers count doesn't match input meta count");

        // In contrast to InferList, the InferList2 has only one
        // "full-frame" image argument, and all the rest are arrays of
        // ether ROI or blobs. So here we set the 0th arg image format
        // to all inputs which are ROI-based (skipping the
        // "blob"-based ones)
        // FIXME: this is filtering not done, actually! GArrayDesc has
        // no hint for type!
        const auto &mm_0   = in_metas[0u];
        switch (in_metas[0u].index()) {
            case cv::GMetaArg::index_of<cv::GMatDesc>(): {
                const auto &meta_0 = util::get<cv::GMatDesc>(mm_0);
                GAPI_Assert(   !meta_0.isND()
                            && !meta_0.planar
                            && "Only images are supported as the 0th argument");
                break;
            }
            case cv::GMetaArg::index_of<cv::GFrameDesc>(): {
                // FIXME: Is there any validation for GFrame ?
                break;
            }
            default:
                util::throw_error(std::runtime_error("Unsupported input meta for ONNX backend"));
        }
        if (util::holds_alternative<cv::GMatDesc>(mm_0)) {
            const auto &meta_0 = util::get<cv::GMatDesc>(mm_0);
            GAPI_Assert(   !meta_0.isND()
                        && !meta_0.planar
                        && "Only images are supported as the 0th argument");
        }
        for (auto i : ade::util::iota(uu.oc->numInputs())) {
            const auto &mm = in_metas[i + 1];
            GAPI_Assert(util::holds_alternative<cv::GArrayDesc>(mm)
                        && "Non-array inputs are not supported");
        }

        // roi-list version is much easier at the moment.
        // All our outputs are vectors which don't have
        // metadata at the moment - so just create a vector of
        // "empty" array metadatas of the required size.
        return cv::GMetaArgs(uu.oc->numOutputs(),
                             cv::GMetaArg{cv::empty_array_desc()});
    }

    static void run(const ONNXUnit &uu, ONNXCallContext &ctx) {
        Views views;
        GAPI_Assert(ctx.args.size() > 1u
                    && "This operation must have at least two arguments");

        // Since we do a ROI list inference, always assume our input buffer is image
        // Take the next argument, which must be vector (of any kind).
        // Use this only to obtain the ROI list size (sizes of all
        // other vectors must be equal to this one)
        const auto list_size = ctx.inArg<cv::detail::VectorRef>(1u).size();

        for (auto i : ade::util::iota(uu.oc->numOutputs())) {
            ctx.outVecR<cv::Mat>(i).clear();
        }
        // For every ROI in the list {{{
        for (const auto &list_idx : ade::util::iota(list_size)) {
            std::vector<Ort::Value> in_tensors, out_tensors;
            std::vector<cv::Mat> in_mats(uu.oc->numInputs());
            // For every input of the net {{{
            for (auto in_idx : ade::util::iota(uu.oc->numInputs())) {
                const auto &this_vec = ctx.inArg<cv::detail::VectorRef>(in_idx+1u);
                GAPI_Assert(this_vec.size() == list_size);
                // Prepare input {{{
                //   FIXME: Terrible run-time logic based on RTTI!
                //   FIXME: Will never work on non-RTTI systems!
                //   FIXME: Need to replace with a static type tags
                //   (like with serialization) instead!
                if (this_vec.holds<cv::Rect>()) {
                    // ROI case - create an ROI blob
                    const auto &vec = this_vec.rref<cv::Rect>();
                    uu.oc->setInput(ctx, in_idx, in_idx, views, vec[list_idx]);
                } else if (this_vec.holds<cv::Mat>()) {
                    // Mat case - create a regular blob
                    // FIXME: NOW Assume Mats are always BLOBS (not
                    // images)
                    const auto &vec = this_vec.rref<cv::Mat>();
                    uu.oc->setInput(in_idx, vec[list_idx]);
                } else {
                    GAPI_Assert(false && "Only Rect and Mat types are supported for infer list 2!");
                }
                // }}} (Preapre input)
            } // }}} (For every input of the net)

            std::vector<cv::Mat> out_mats(uu.oc->numOutputs());
            for (auto i : ade::util::iota(uu.oc->numOutputs())) {
                out_mats[i] = uu.oc->allocOutput(i);
                uu.oc->setOutput(i, out_mats[i]);
            }
            uu.oc->run();

            for (auto i : ade::util::iota(uu.oc->numOutputs())) {
                std::vector<cv::Mat> &out_vec = ctx.outVecR<cv::Mat>(i);
                out_vec.push_back(std::move(out_mats[i]));
            }
        } // }}} (For every ROI in the list)
    }
};

} // namespace onnx
} // namespace gapi
} // namespace cv

namespace {
    class GONNXBackendImpl final: public cv::gapi::GBackend::Priv {
        virtual void unpackKernel(ade::Graph            &gr,
                                  const ade::NodeHandle &nh,
                                  const cv::GKernelImpl &ii) override {
            using namespace cv::gimpl;
            // FIXME: Introduce a DNNBackend interface which'd specify
            // the framework for this???
            GONNXModel gm(gr);
            const auto &np = gm.metadata(nh).get<NetworkParams>();
            const auto &pp = cv::util::any_cast<cv::gapi::onnx::detail::ParamDesc>(np.opaque);
            const auto &ki = cv::util::any_cast<KImpl>(ii.opaque);
            gm.metadata(nh).set(ONNXUnit{pp});
            gm.metadata(nh).set(ONNXCallable{ki.run});
            gm.metadata(nh).set(CustomMetaFunction{ki.customMetaFunc});
        }

        virtual EPtr compile(const ade::Graph &graph,
                             const cv::GCompileArgs &,
                             const std::vector<ade::NodeHandle> &nodes) const override {
            return EPtr{new cv::gimpl::onnx::GONNXExecutable(graph, nodes)};
        }

        virtual cv::gapi::GKernelPackage auxiliaryKernels() const override {
            return cv::gapi::kernels< cv::gimpl::onnx::Infer
                                    , cv::gimpl::onnx::InferROI
                                    , cv::gimpl::onnx::InferList
                                    , cv::gimpl::onnx::InferList2
                                    >();
        }
    };
}

cv::gapi::GBackend cv::gapi::onnx::backend() {
    static cv::gapi::GBackend this_backend(std::make_shared<GONNXBackendImpl>());
    return this_backend;
}
#else // HAVE_ONNX

cv::gapi::GBackend cv::gapi::onnx::backend() {
    // Still provide this symbol to avoid linking issues
    util::throw_error(std::runtime_error("G-API has been compiled without ONNX support"));
}
#endif // HAVE_ONNX
