// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#ifndef _GRFMT_WEBP_H_
#define _GRFMT_WEBP_H_

#include "grfmt_base.hpp"

#ifdef HAVE_WEBP

#include <fstream>

struct WebPAnimDecoder;

namespace cv
{

class WebPDecoder CV_FINAL : public BaseImageDecoder
{
public:
    WebPDecoder();
    ~WebPDecoder() CV_OVERRIDE;

    bool readData(Mat& img) CV_OVERRIDE;
    bool readHeader() CV_OVERRIDE;
    bool nextPage() CV_OVERRIDE;

    size_t signatureLength() const CV_OVERRIDE;
    bool checkSignature(const String& signature) const CV_OVERRIDE;

    ImageDecoder newDecoder() const CV_OVERRIDE;

protected:
    struct UniquePtrDeleter {
        void operator()(WebPAnimDecoder* decoder) const;
    };

    std::ifstream fs;
    size_t fs_size;
    Mat data;
    std::unique_ptr<WebPAnimDecoder, UniquePtrDeleter> anim_decoder;
    bool m_has_animation;
    int m_previous_timestamp;
};

class WebPEncoder CV_FINAL : public BaseImageEncoder
{
public:
    WebPEncoder();
    ~WebPEncoder() CV_OVERRIDE;

    bool write(const Mat& img, const std::vector<int>& params) CV_OVERRIDE;
    bool writemulti(const std::vector<Mat>& img_vec, const std::vector<int>& params) CV_OVERRIDE;
    bool writeanimation(const Animation& animation, const std::vector<int>& params) CV_OVERRIDE;

    ImageEncoder newEncoder() const CV_OVERRIDE;
};

}

#endif

#endif /* _GRFMT_WEBP_H_ */
