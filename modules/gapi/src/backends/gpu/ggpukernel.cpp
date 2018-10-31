// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Copyright (C) 2018 Intel Corporation


#include <cassert>

#include "opencv2/gapi/gpu/ggpukernel.hpp"

cv::UMat cv::GGPUContext::inMat(int input)
{
    return (inArg<cv::UMat>(input));
}

cv::UMat& cv::GGPUContext::outMatR(int output)
{
    return (*(util::get<cv::UMat*>(m_results.at(output))));
}

const cv::gapi::own::Scalar& cv::GGPUContext::inVal(int input)
{
    return inArg<cv::gapi::own::Scalar>(input);
}

cv::gapi::own::Scalar& cv::GGPUContext::outValR(int output)
{
    return *util::get<cv::gapi::own::Scalar*>(m_results.at(output));
}

cv::detail::VectorRef& cv::GGPUContext::outVecRef(int output)
{
    return util::get<cv::detail::VectorRef>(m_results.at(output));
}

cv::GGPUKernel::GGPUKernel()
{
}

cv::GGPUKernel::GGPUKernel(const GGPUKernel::F &f)
    : m_f(f)
{
}

void cv::GGPUKernel::apply(GGPUContext &ctx)
{
    CV_Assert(m_f);
    m_f(ctx);
}
