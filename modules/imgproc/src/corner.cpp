/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Copyright (C) 2014-2015, Itseez Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "opencl_kernels_imgproc.hpp"
#include "opencv2/core/hal/intrin.hpp"

namespace cv
{

#if CV_AVX
// load three 8-packed float vector and deinterleave
// probably it's better to write down somewhere else
static inline void load_deinterleave(const float* ptr, __m256& a, __m256& b, __m256& c)
{
    __m256 s0 = _mm256_loadu_ps(ptr);                    // a0, b0, c0, a1, b1, c1, a2, b2,
    __m256 s1 = _mm256_loadu_ps(ptr + 8);                // c2, a3, b3, c3, a4, b4, c4, a5,
    __m256 s2 = _mm256_loadu_ps(ptr + 16);               // b5, c5, a6, b6, c6, a7, b7, c7,
    __m256 s3 = _mm256_permute2f128_ps(s1, s2, 0x21);    // a4, b4, c4, a5, b5, c5, a6, b6,
    __m256 s4 = _mm256_permute2f128_ps(s2, s2, 0x33);    // c6, a7, b7, c7, c6, a7, b7, c7,

    __m256 v00 = _mm256_unpacklo_ps(s0, s3);             // a0, a4, b0, b4, b1, b5, c1, c5,
    __m256 v01 = _mm256_unpackhi_ps(s0, s3);             // c0, c4, a1, a5, a2, a6, b2, b6,
    __m256 v02 = _mm256_unpacklo_ps(s1, s4);             // c2, c6, a3, a7, x,  x,  x,  x,
    __m256 v03 = _mm256_unpackhi_ps(s1, s4);             // b3, b7, c3, c7, x,  x,  x,  x,
    __m256 v04 = _mm256_permute2f128_ps(v02, v03, 0x20); // c2, c6, a3, a7, b3, b7, c3, c7,
    __m256 v05 = _mm256_permute2f128_ps(v01, v03, 0x21); // a2, a6, b2, b6, b3, b7, c3, c7,

    __m256 v10 = _mm256_unpacklo_ps(v00, v05);           // a0, a2, a4, a6, b1, b3, b5, b7,
    __m256 v11 = _mm256_unpackhi_ps(v00, v05);           // b0, b2, b4, b6, c1, c3, c5, c7,
    __m256 v12 = _mm256_unpacklo_ps(v01, v04);           // c0, c2, c4, c6, x,  x,  x,  x,
    __m256 v13 = _mm256_unpackhi_ps(v01, v04);           // a1, a3, a5, a7, x,  x,  x,  x,
    __m256 v14 = _mm256_permute2f128_ps(v11, v12, 0x20); // b0, b2, b4, b6, c0, c2, c4, c6,
    __m256 v15 = _mm256_permute2f128_ps(v10, v11, 0x31); // b1, b3, b5, b7, c1, c3, c5, c7,

    __m256 v20 = _mm256_unpacklo_ps(v14, v15);           // b0, b1, b2, b3, c0, c1, c2, c3,
    __m256 v21 = _mm256_unpackhi_ps(v14, v15);           // b4, b5, b6, b7, c4, c5, c6, c7,
    __m256 v22 = _mm256_unpacklo_ps(v10, v13);           // a0, a1, a2, a3, x,  x,  x,  x,
    __m256 v23 = _mm256_unpackhi_ps(v10, v13);           // a4, a5, a6, a7, x,  x,  x,  x,

    a = _mm256_permute2f128_ps(v22, v23, 0x20);          // a0, a1, a2, a3, a4, a5, a6, a7,
    b = _mm256_permute2f128_ps(v20, v21, 0x20);          // b0, b1, b2, b3, b4, b5, b6, b7,
    c = _mm256_permute2f128_ps(v20, v21, 0x31);          // c0, c1, c2, c3, c4, c5, c6, c7,
}

// realign four 3-packed vector to three 4-packed vector
static inline void v_pack4x3to3x4(const __m128i& s0, const __m128i& s1, const __m128i& s2, const __m128i& s3, __m128i& d0, __m128i& d1, __m128i& d2)
{
    d0 = _mm_or_si128(s0, _mm_slli_si128(s1, 12));
    d1 = _mm_or_si128(_mm_srli_si128(s1, 4), _mm_slli_si128(s2, 8));
    d2 = _mm_or_si128(_mm_srli_si128(s2, 8), _mm_slli_si128(s3, 4));
}

// separate high and low 128 bit and cast to __m128i
static inline void v_separate_lo_hi(const __m256& src, __m128i& lo, __m128i& hi)
{
    lo = _mm_castps_si128(_mm256_castps256_ps128(src));
    hi = _mm_castps_si128(_mm256_extractf128_ps(src, 1));
}

// interleave three 8-float vector and store
static inline void store_interleave(float* ptr, const __m256& a, const __m256& b, const __m256& c)
{
    __m128i a0, a1, b0, b1, c0, c1;
    v_separate_lo_hi(a, a0, a1);
    v_separate_lo_hi(b, b0, b1);
    v_separate_lo_hi(c, c0, c1);

    v_uint32x4 z = v_setzero_u32();
    v_uint32x4 u0, u1, u2, u3;
    v_transpose4x4(v_uint32x4(a0), v_uint32x4(b0), v_uint32x4(c0), z, u0, u1, u2, u3);
    v_pack4x3to3x4(u0.val, u1.val, u2.val, u3.val, a0, b0, c0);
    v_transpose4x4(v_uint32x4(a1), v_uint32x4(b1), v_uint32x4(c1), z, u0, u1, u2, u3);
    v_pack4x3to3x4(u0.val, u1.val, u2.val, u3.val, a1, b1, c1);

    _mm256_storeu_ps(ptr, _mm256_setr_m128(_mm_castsi128_ps(a0), _mm_castsi128_ps(b0)));
    _mm256_storeu_ps(ptr + 8, _mm256_setr_m128(_mm_castsi128_ps(c0), _mm_castsi128_ps(a1)));
    _mm256_storeu_ps(ptr + 16,  _mm256_setr_m128(_mm_castsi128_ps(b1), _mm_castsi128_ps(c1)));
}
#endif // CV_AVX

static void calcMinEigenVal( const Mat& _cov, Mat& _dst )
{
    int i, j;
    Size size = _cov.size();
#if CV_AVX
    bool haveAvx = checkHardwareSupport(CV_CPU_AVX);
#endif
#if CV_SIMD128
    bool haveSimd = hasSIMD128();
#endif

    if( _cov.isContinuous() && _dst.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
    }

    for( i = 0; i < size.height; i++ )
    {
        const float* cov = _cov.ptr<float>(i);
        float* dst = _dst.ptr<float>(i);
        j = 0;
#if CV_AVX
        if( haveAvx )
        {
            __m256 half = _mm256_set1_ps(0.5f);
            for( ; j <= size.width - 8; j += 8 )
            {
                __m256 v_a, v_b, v_c, v_t;
                load_deinterleave(cov + j*3, v_a, v_b, v_c);
                v_a = _mm256_mul_ps(v_a, half);
                v_c = _mm256_mul_ps(v_c, half);
                v_t = _mm256_sub_ps(v_a, v_c);
                v_t = _mm256_add_ps(_mm256_mul_ps(v_b, v_b), _mm256_mul_ps(v_t, v_t));
                _mm256_storeu_ps(dst + j, _mm256_sub_ps(_mm256_add_ps(v_a, v_c), _mm256_sqrt_ps(v_t)));
            }
        }
#endif // CV_AVX

#if CV_SIMD128
        if( haveSimd )
        {
            v_float32x4 half = v_setall_f32(0.5f);
            for( ; j <= size.width - v_float32x4::nlanes; j += v_float32x4::nlanes )
            {
                v_float32x4 v_a, v_b, v_c, v_t;
                v_load_deinterleave(cov + j*3, v_a, v_b, v_c);
                v_a *= half;
                v_c *= half;
                v_t = v_a - v_c;
                v_t = v_muladd(v_b, v_b, (v_t * v_t));
                v_store(dst + j, (v_a + v_c) - v_sqrt(v_t));
            }
        }
#endif // CV_SIMD128

        for( ; j < size.width; j++ )
        {
            float a = cov[j*3]*0.5f;
            float b = cov[j*3+1];
            float c = cov[j*3+2]*0.5f;
            dst[j] = (float)((a + c) - std::sqrt((a - c)*(a - c) + b*b));
        }
    }
}


static void calcHarris( const Mat& _cov, Mat& _dst, double k )
{
    int i, j;
    Size size = _cov.size();
#if CV_AVX
    bool haveAvx = checkHardwareSupport(CV_CPU_AVX);
#endif
#if CV_SIMD128
    bool haveSimd = hasSIMD128();
#endif

    if( _cov.isContinuous() && _dst.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
    }

    for( i = 0; i < size.height; i++ )
    {
        const float* cov = _cov.ptr<float>(i);
        float* dst = _dst.ptr<float>(i);
        j = 0;

#if CV_AVX
        if( haveAvx )
        {
            __m256 v_k = _mm256_set1_ps((float)k);

            for( ; j <= size.width - 8; j += 8 )
            {
                __m256 v_a, v_b, v_c;
                load_deinterleave(cov + j * 3, v_a, v_b, v_c);

                __m256 v_ac_bb = _mm256_sub_ps(_mm256_mul_ps(v_a, v_c), _mm256_mul_ps(v_b, v_b));
                __m256 v_ac = _mm256_add_ps(v_a, v_c);
                __m256 v_dst = _mm256_sub_ps(v_ac_bb, _mm256_mul_ps(v_k, _mm256_mul_ps(v_ac, v_ac)));
                _mm256_storeu_ps(dst + j, v_dst);
            }
        }
#endif // CV_AVX

#if CV_SIMD128
        if( haveSimd )
        {
            v_float32x4 v_k = v_setall_f32((float)k);

            for( ; j <= size.width - v_float32x4::nlanes; j += v_float32x4::nlanes )
            {
                v_float32x4 v_a, v_b, v_c;
                v_load_deinterleave(cov + j * 3, v_a, v_b, v_c);

                v_float32x4 v_ac_bb = v_a * v_c - v_b * v_b;
                v_float32x4 v_ac = v_a + v_c;
                v_float32x4 v_dst = v_ac_bb - v_k * v_ac * v_ac;
                v_store(dst + j, v_dst);
            }
        }
#endif // CV_SIMD128

        for( ; j < size.width; j++ )
        {
            float a = cov[j*3];
            float b = cov[j*3+1];
            float c = cov[j*3+2];
            dst[j] = (float)(a*c - b*b - k*(a + c)*(a + c));
        }
    }
}


static void eigen2x2( const float* cov, float* dst, int n )
{
    for( int j = 0; j < n; j++ )
    {
        double a = cov[j*3];
        double b = cov[j*3+1];
        double c = cov[j*3+2];

        double u = (a + c)*0.5;
        double v = std::sqrt((a - c)*(a - c)*0.25 + b*b);
        double l1 = u + v;
        double l2 = u - v;

        double x = b;
        double y = l1 - a;
        double e = fabs(x);

        if( e + fabs(y) < 1e-4 )
        {
            y = b;
            x = l1 - c;
            e = fabs(x);
            if( e + fabs(y) < 1e-4 )
            {
                e = 1./(e + fabs(y) + FLT_EPSILON);
                x *= e, y *= e;
            }
        }

        double d = 1./std::sqrt(x*x + y*y + DBL_EPSILON);
        dst[6*j] = (float)l1;
        dst[6*j + 2] = (float)(x*d);
        dst[6*j + 3] = (float)(y*d);

        x = b;
        y = l2 - a;
        e = fabs(x);

        if( e + fabs(y) < 1e-4 )
        {
            y = b;
            x = l2 - c;
            e = fabs(x);
            if( e + fabs(y) < 1e-4 )
            {
                e = 1./(e + fabs(y) + FLT_EPSILON);
                x *= e, y *= e;
            }
        }

        d = 1./std::sqrt(x*x + y*y + DBL_EPSILON);
        dst[6*j + 1] = (float)l2;
        dst[6*j + 4] = (float)(x*d);
        dst[6*j + 5] = (float)(y*d);
    }
}

static void calcEigenValsVecs( const Mat& _cov, Mat& _dst )
{
    Size size = _cov.size();
    if( _cov.isContinuous() && _dst.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
    }

    for( int i = 0; i < size.height; i++ )
    {
        const float* cov = _cov.ptr<float>(i);
        float* dst = _dst.ptr<float>(i);

        eigen2x2(cov, dst, size.width);
    }
}


enum { MINEIGENVAL=0, HARRIS=1, EIGENVALSVECS=2 };


static void
cornerEigenValsVecs( const Mat& src, Mat& eigenv, int block_size,
                     int aperture_size, int op_type, double k=0.,
                     int borderType=BORDER_DEFAULT )
{
#ifdef HAVE_TEGRA_OPTIMIZATION
    if (tegra::useTegra() && tegra::cornerEigenValsVecs(src, eigenv, block_size, aperture_size, op_type, k, borderType))
        return;
#endif
#if CV_AVX
    bool haveAvx = checkHardwareSupport(CV_CPU_AVX);
#endif
#if CV_SIMD128
    bool haveSimd = hasSIMD128();
#endif

    int depth = src.depth();
    double scale = (double)(1 << ((aperture_size > 0 ? aperture_size : 3) - 1)) * block_size;
    if( aperture_size < 0 )
        scale *= 2.0;
    if( depth == CV_8U )
        scale *= 255.0;
    scale = 1.0/scale;

    CV_Assert( src.type() == CV_8UC1 || src.type() == CV_32FC1 );

    Mat Dx, Dy;
    if( aperture_size > 0 )
    {
        Sobel( src, Dx, CV_32F, 1, 0, aperture_size, scale, 0, borderType );
        Sobel( src, Dy, CV_32F, 0, 1, aperture_size, scale, 0, borderType );
    }
    else
    {
        Scharr( src, Dx, CV_32F, 1, 0, scale, 0, borderType );
        Scharr( src, Dy, CV_32F, 0, 1, scale, 0, borderType );
    }

    Size size = src.size();
    Mat cov( size, CV_32FC3 );
    int i, j;

    for( i = 0; i < size.height; i++ )
    {
        float* cov_data = cov.ptr<float>(i);
        const float* dxdata = Dx.ptr<float>(i);
        const float* dydata = Dy.ptr<float>(i);
        j = 0;

#if CV_AVX
        if( haveAvx )
        {
            for( ; j <= size.width - 8; j += 8 )
            {
                __m256 v_dx = _mm256_loadu_ps(dxdata + j);
                __m256 v_dy = _mm256_loadu_ps(dydata + j);

                __m256 v_dst0, v_dst1, v_dst2;
                v_dst0 = _mm256_mul_ps(v_dx, v_dx);
                v_dst1 = _mm256_mul_ps(v_dx, v_dy);
                v_dst2 = _mm256_mul_ps(v_dy, v_dy);

                store_interleave(cov_data + j * 3, v_dst0, v_dst1, v_dst2);
            }
        }
#endif // CV_AVX

#if CV_SIMD128
        if( haveSimd )
        {
            for( ; j <= size.width - v_float32x4::nlanes; j += v_float32x4::nlanes )
            {
                v_float32x4 v_dx = v_load(dxdata + j);
                v_float32x4 v_dy = v_load(dydata + j);

                v_float32x4 v_dst0, v_dst1, v_dst2;
                v_dst0 = v_dx * v_dx;
                v_dst1 = v_dx * v_dy;
                v_dst2 = v_dy * v_dy;

                v_store_interleave(cov_data + j * 3, v_dst0, v_dst1, v_dst2);
            }
        }
#endif // CV_SIMD128

        for( ; j < size.width; j++ )
        {
            float dx = dxdata[j];
            float dy = dydata[j];

            cov_data[j*3] = dx*dx;
            cov_data[j*3+1] = dx*dy;
            cov_data[j*3+2] = dy*dy;
        }
    }

    boxFilter(cov, cov, cov.depth(), Size(block_size, block_size),
        Point(-1,-1), false, borderType );

    if( op_type == MINEIGENVAL )
        calcMinEigenVal( cov, eigenv );
    else if( op_type == HARRIS )
        calcHarris( cov, eigenv, k );
    else if( op_type == EIGENVALSVECS )
        calcEigenValsVecs( cov, eigenv );
}

#ifdef HAVE_OPENCL

static bool extractCovData(InputArray _src, UMat & Dx, UMat & Dy, int depth,
                           float scale, int aperture_size, int borderType)
{
    UMat src = _src.getUMat();

    Size wholeSize;
    Point ofs;
    src.locateROI(wholeSize, ofs);

    const int sobel_lsz = 16;
    if ((aperture_size == 3 || aperture_size == 5 || aperture_size == 7 || aperture_size == -1) &&
        wholeSize.height > sobel_lsz + (aperture_size >> 1) &&
        wholeSize.width > sobel_lsz + (aperture_size >> 1))
    {
        CV_Assert(depth == CV_8U || depth == CV_32F);

        Dx.create(src.size(), CV_32FC1);
        Dy.create(src.size(), CV_32FC1);

        size_t localsize[2] = { (size_t)sobel_lsz, (size_t)sobel_lsz };
        size_t globalsize[2] = { localsize[0] * (1 + (src.cols - 1) / localsize[0]),
                                 localsize[1] * (1 + (src.rows - 1) / localsize[1]) };

        int src_offset_x = (int)((src.offset % src.step) / src.elemSize());
        int src_offset_y = (int)(src.offset / src.step);

        const char * const borderTypes[] = { "BORDER_CONSTANT", "BORDER_REPLICATE", "BORDER_REFLECT",
                                             "BORDER_WRAP", "BORDER_REFLECT101" };

        ocl::Kernel k(format("sobel%d", aperture_size).c_str(), ocl::imgproc::covardata_oclsrc,
                      cv::format("-D BLK_X=%d -D BLK_Y=%d -D %s -D SRCTYPE=%s%s",
                                 (int)localsize[0], (int)localsize[1], borderTypes[borderType], ocl::typeToStr(depth),
                                 aperture_size < 0 ? " -D SCHARR" : ""));
        if (k.empty())
            return false;

        k.args(ocl::KernelArg::PtrReadOnly(src), (int)src.step, src_offset_x, src_offset_y,
               ocl::KernelArg::WriteOnlyNoSize(Dx), ocl::KernelArg::WriteOnly(Dy),
               wholeSize.height, wholeSize.width, scale);

        return k.run(2, globalsize, localsize, false);
    }
    else
    {
        if (aperture_size > 0)
        {
            Sobel(_src, Dx, CV_32F, 1, 0, aperture_size, scale, 0, borderType);
            Sobel(_src, Dy, CV_32F, 0, 1, aperture_size, scale, 0, borderType);
        }
        else
        {
            Scharr(_src, Dx, CV_32F, 1, 0, scale, 0, borderType);
            Scharr(_src, Dy, CV_32F, 0, 1, scale, 0, borderType);
        }
    }

    return true;
}

static bool ocl_cornerMinEigenValVecs(InputArray _src, OutputArray _dst, int block_size,
                                      int aperture_size, double k, int borderType, int op_type)
{
    CV_Assert(op_type == HARRIS || op_type == MINEIGENVAL);

    if ( !(borderType == BORDER_CONSTANT || borderType == BORDER_REPLICATE ||
           borderType == BORDER_REFLECT || borderType == BORDER_REFLECT_101) )
        return false;

    int type = _src.type(), depth = CV_MAT_DEPTH(type);
    if ( !(type == CV_8UC1 || type == CV_32FC1) )
        return false;

    const char * const borderTypes[] = { "BORDER_CONSTANT", "BORDER_REPLICATE", "BORDER_REFLECT",
                                         "BORDER_WRAP", "BORDER_REFLECT101" };
    const char * const cornerType[] = { "CORNER_MINEIGENVAL", "CORNER_HARRIS", 0 };


    double scale = (double)(1 << ((aperture_size > 0 ? aperture_size : 3) - 1)) * block_size;
    if (aperture_size < 0)
        scale *= 2.0;
    if (depth == CV_8U)
        scale *= 255.0;
    scale = 1.0 / scale;

    UMat Dx, Dy;
    if (!extractCovData(_src, Dx, Dy, depth, (float)scale, aperture_size, borderType))
        return false;

    ocl::Kernel cornelKernel("corner", ocl::imgproc::corner_oclsrc,
                             format("-D anX=%d -D anY=%d -D ksX=%d -D ksY=%d -D %s -D %s",
                                    block_size / 2, block_size / 2, block_size, block_size,
                                    borderTypes[borderType], cornerType[op_type]));
    if (cornelKernel.empty())
        return false;

    _dst.createSameSize(_src, CV_32FC1);
    UMat dst = _dst.getUMat();

    cornelKernel.args(ocl::KernelArg::ReadOnly(Dx), ocl::KernelArg::ReadOnly(Dy),
                      ocl::KernelArg::WriteOnly(dst), (float)k);

    size_t blockSizeX = 256, blockSizeY = 1;
    size_t gSize = blockSizeX - block_size / 2 * 2;
    size_t globalSizeX = (Dx.cols) % gSize == 0 ? Dx.cols / gSize * blockSizeX : (Dx.cols / gSize + 1) * blockSizeX;
    size_t rows_per_thread = 2;
    size_t globalSizeY = ((Dx.rows + rows_per_thread - 1) / rows_per_thread) % blockSizeY == 0 ?
                         ((Dx.rows + rows_per_thread - 1) / rows_per_thread) :
                         (((Dx.rows + rows_per_thread - 1) / rows_per_thread) / blockSizeY + 1) * blockSizeY;

    size_t globalsize[2] = { globalSizeX, globalSizeY }, localsize[2] = { blockSizeX, blockSizeY };
    return cornelKernel.run(2, globalsize, localsize, false);
}

static bool ocl_preCornerDetect( InputArray _src, OutputArray _dst, int ksize, int borderType, int depth )
{
    UMat Dx, Dy, D2x, D2y, Dxy;

    if (!extractCovData(_src, Dx, Dy, depth, 1, ksize, borderType))
        return false;

    Sobel( _src, D2x, CV_32F, 2, 0, ksize, 1, 0, borderType );
    Sobel( _src, D2y, CV_32F, 0, 2, ksize, 1, 0, borderType );
    Sobel( _src, Dxy, CV_32F, 1, 1, ksize, 1, 0, borderType );

    _dst.create( _src.size(), CV_32FC1 );
    UMat dst = _dst.getUMat();

    double factor = 1 << (ksize - 1);
    if( depth == CV_8U )
        factor *= 255;
    factor = 1./(factor * factor * factor);

    ocl::Kernel k("preCornerDetect", ocl::imgproc::precornerdetect_oclsrc);
    if (k.empty())
        return false;

    k.args(ocl::KernelArg::ReadOnlyNoSize(Dx), ocl::KernelArg::ReadOnlyNoSize(Dy),
           ocl::KernelArg::ReadOnlyNoSize(D2x), ocl::KernelArg::ReadOnlyNoSize(D2y),
           ocl::KernelArg::ReadOnlyNoSize(Dxy), ocl::KernelArg::WriteOnly(dst), (float)factor);

    size_t globalsize[2] = { (size_t)dst.cols, (size_t)dst.rows };
    return k.run(2, globalsize, NULL, false);
}

#endif

}

#if defined(HAVE_IPP)
namespace cv
{
static bool ipp_cornerMinEigenVal( InputArray _src, OutputArray _dst, int blockSize, int ksize, int borderType )
{
    CV_INSTRUMENT_REGION_IPP()

#if IPP_VERSION_X100 >= 800
    Mat src = _src.getMat();
    _dst.create( src.size(), CV_32FC1 );
    Mat dst = _dst.getMat();

    {
        typedef IppStatus (CV_STDCALL * ippiMinEigenValGetBufferSize)(IppiSize, int, int, int*);
        typedef IppStatus (CV_STDCALL * ippiMinEigenVal)(const void*, int, Ipp32f*, int, IppiSize, IppiKernelType, int, int, Ipp8u*);
        IppiKernelType kerType;
        int kerSize = ksize;
        if (ksize < 0)
        {
            kerType = ippKernelScharr;
            kerSize = 3;
        } else
        {
            kerType = ippKernelSobel;
        }
        bool isolated = (borderType & BORDER_ISOLATED) != 0;
        int borderTypeNI = borderType & ~BORDER_ISOLATED;
        if ((borderTypeNI == BORDER_REPLICATE && (!src.isSubmatrix() || isolated)) &&
            (kerSize == 3 || kerSize == 5) && (blockSize == 3 || blockSize == 5))
        {
            ippiMinEigenValGetBufferSize getBufferSizeFunc = 0;
            ippiMinEigenVal ippiMinEigenVal_C1R = 0;
            float norm_coef = 0.f;

            if (src.type() == CV_8UC1)
            {
                getBufferSizeFunc = (ippiMinEigenValGetBufferSize) ippiMinEigenValGetBufferSize_8u32f_C1R;
                ippiMinEigenVal_C1R = (ippiMinEigenVal) ippiMinEigenVal_8u32f_C1R;
                norm_coef = 1.f / 255.f;
            } else if (src.type() == CV_32FC1)
            {
                getBufferSizeFunc = (ippiMinEigenValGetBufferSize) ippiMinEigenValGetBufferSize_32f_C1R;
                ippiMinEigenVal_C1R = (ippiMinEigenVal) ippiMinEigenVal_32f_C1R;
                norm_coef = 255.f;
            }
            norm_coef = kerType == ippKernelSobel ? norm_coef : norm_coef / 2.45f;

            if (getBufferSizeFunc && ippiMinEigenVal_C1R)
            {
                int bufferSize;
                IppiSize srcRoi = { src.cols, src.rows };
                IppStatus ok = getBufferSizeFunc(srcRoi, kerSize, blockSize, &bufferSize);
                if (ok >= 0)
                {
                    AutoBuffer<uchar> buffer(bufferSize);
                    ok = CV_INSTRUMENT_FUN_IPP(ippiMinEigenVal_C1R, src.ptr(), (int) src.step, dst.ptr<Ipp32f>(), (int) dst.step, srcRoi, kerType, kerSize, blockSize, buffer);
                    CV_SUPPRESS_DEPRECATED_START
                    if (ok >= 0) ok = CV_INSTRUMENT_FUN_IPP(ippiMulC_32f_C1IR, norm_coef, dst.ptr<Ipp32f>(), (int) dst.step, srcRoi);
                    CV_SUPPRESS_DEPRECATED_END
                    if (ok >= 0)
                    {
                        CV_IMPL_ADD(CV_IMPL_IPP);
                        return true;
                    }
                }
            }
        }
    }
#else
    CV_UNUSED(_src); CV_UNUSED(_dst); CV_UNUSED(blockSize); CV_UNUSED(borderType);
#endif
    return false;
}
}
#endif

void cv::cornerMinEigenVal( InputArray _src, OutputArray _dst, int blockSize, int ksize, int borderType )
{
    CV_INSTRUMENT_REGION()

    CV_OCL_RUN(_src.dims() <= 2 && _dst.isUMat(),
               ocl_cornerMinEigenValVecs(_src, _dst, blockSize, ksize, 0.0, borderType, MINEIGENVAL))

#ifdef HAVE_IPP
    int kerSize = (ksize < 0)?3:ksize;
    bool isolated = (borderType & BORDER_ISOLATED) != 0;
    int borderTypeNI = borderType & ~BORDER_ISOLATED;
#endif
    CV_IPP_RUN(((borderTypeNI == BORDER_REPLICATE && (!_src.isSubmatrix() || isolated)) &&
            (kerSize == 3 || kerSize == 5) && (blockSize == 3 || blockSize == 5)) && IPP_VERSION_X100 >= 800,
        ipp_cornerMinEigenVal( _src, _dst, blockSize, ksize, borderType ));


    Mat src = _src.getMat();
    _dst.create( src.size(), CV_32FC1 );
    Mat dst = _dst.getMat();

    cornerEigenValsVecs( src, dst, blockSize, ksize, MINEIGENVAL, 0, borderType );
}


#if defined(HAVE_IPP)
namespace cv
{
static bool ipp_cornerHarris( InputArray _src, OutputArray _dst, int blockSize, int ksize, double k, int borderType )
{
    CV_INSTRUMENT_REGION_IPP()

#if IPP_VERSION_X100 >= 810 && IPP_DISABLE_BLOCK
    Mat src = _src.getMat();
    _dst.create( src.size(), CV_32FC1 );
    Mat dst = _dst.getMat();

    {
        int type = src.type(), depth = CV_MAT_DEPTH(type), cn = CV_MAT_CN(type);
        int borderTypeNI = borderType & ~BORDER_ISOLATED;
        bool isolated = (borderType & BORDER_ISOLATED) != 0;

        if ( (ksize == 3 || ksize == 5) && (type == CV_8UC1 || type == CV_32FC1) &&
            (borderTypeNI == BORDER_CONSTANT || borderTypeNI == BORDER_REPLICATE) && cn == 1 && (!src.isSubmatrix() || isolated) )
        {
            IppiSize roisize = { src.cols, src.rows };
            IppiMaskSize masksize = ksize == 5 ? ippMskSize5x5 : ippMskSize3x3;
            IppDataType datatype = type == CV_8UC1 ? ipp8u : ipp32f;
            Ipp32s bufsize = 0;

            double scale = (double)(1 << ((ksize > 0 ? ksize : 3) - 1)) * blockSize;
            if (ksize < 0)
                scale *= 2.0;
            if (depth == CV_8U)
                scale *= 255.0;
            scale = std::pow(scale, -4.0);

            if (ippiHarrisCornerGetBufferSize(roisize, masksize, blockSize, datatype, cn, &bufsize) >= 0)
            {
                Ipp8u * buffer = ippsMalloc_8u(bufsize);
                IppiDifferentialKernel filterType = ksize > 0 ? ippFilterSobel : ippFilterScharr;
                IppiBorderType borderTypeIpp = borderTypeNI == BORDER_CONSTANT ? ippBorderConst : ippBorderRepl;
                IppStatus status = (IppStatus)-1;

                if (depth == CV_8U)
                    status = CV_INSTRUMENT_FUN_IPP(ippiHarrisCorner_8u32f_C1R,((const Ipp8u *)src.data, (int)src.step, (Ipp32f *)dst.data, (int)dst.step, roisize,
                        filterType, masksize, blockSize, (Ipp32f)k, (Ipp32f)scale, borderTypeIpp, 0, buffer));
                else if (depth == CV_32F)
                    status = CV_INSTRUMENT_FUN_IPP(ippiHarrisCorner_32f_C1R,((const Ipp32f *)src.data, (int)src.step, (Ipp32f *)dst.data, (int)dst.step, roisize,
                        filterType, masksize, blockSize, (Ipp32f)k, (Ipp32f)scale, borderTypeIpp, 0, buffer));
                ippsFree(buffer);

                if (status >= 0)
                {
                    CV_IMPL_ADD(CV_IMPL_IPP);
                    return true;
                }
            }
        }
    }
#else
    CV_UNUSED(_src); CV_UNUSED(_dst); CV_UNUSED(blockSize);  CV_UNUSED(ksize); CV_UNUSED(k); CV_UNUSED(borderType);
#endif
    return false;
}
}
#endif

void cv::cornerHarris( InputArray _src, OutputArray _dst, int blockSize, int ksize, double k, int borderType )
{
    CV_INSTRUMENT_REGION()

    CV_OCL_RUN(_src.dims() <= 2 && _dst.isUMat(),
               ocl_cornerMinEigenValVecs(_src, _dst, blockSize, ksize, k, borderType, HARRIS))

#ifdef HAVE_IPP
    int borderTypeNI = borderType & ~BORDER_ISOLATED;
    bool isolated = (borderType & BORDER_ISOLATED) != 0;
#endif
    CV_IPP_RUN(((ksize == 3 || ksize == 5) && (_src.type() == CV_8UC1 || _src.type() == CV_32FC1) &&
        (borderTypeNI == BORDER_CONSTANT || borderTypeNI == BORDER_REPLICATE) && CV_MAT_CN(_src.type()) == 1 &&
        (!_src.isSubmatrix() || isolated)) && IPP_VERSION_X100 >= 810 && IPP_DISABLE_BLOCK, ipp_cornerHarris( _src, _dst, blockSize, ksize, k, borderType ));


    Mat src = _src.getMat();
    _dst.create( src.size(), CV_32FC1 );
    Mat dst = _dst.getMat();


    cornerEigenValsVecs( src, dst, blockSize, ksize, HARRIS, k, borderType );
}


void cv::cornerEigenValsAndVecs( InputArray _src, OutputArray _dst, int blockSize, int ksize, int borderType )
{
    CV_INSTRUMENT_REGION()

    Mat src = _src.getMat();
    Size dsz = _dst.size();
    int dtype = _dst.type();

    if( dsz.height != src.rows || dsz.width*CV_MAT_CN(dtype) != src.cols*6 || CV_MAT_DEPTH(dtype) != CV_32F )
        _dst.create( src.size(), CV_32FC(6) );
    Mat dst = _dst.getMat();
    cornerEigenValsVecs( src, dst, blockSize, ksize, EIGENVALSVECS, 0, borderType );
}


void cv::preCornerDetect( InputArray _src, OutputArray _dst, int ksize, int borderType )
{
    CV_INSTRUMENT_REGION()

    int type = _src.type();
    CV_Assert( type == CV_8UC1 || type == CV_32FC1 );

    CV_OCL_RUN( _src.dims() <= 2 && _dst.isUMat(),
                ocl_preCornerDetect(_src, _dst, ksize, borderType, CV_MAT_DEPTH(type)))

    Mat Dx, Dy, D2x, D2y, Dxy, src = _src.getMat();
    _dst.create( src.size(), CV_32FC1 );
    Mat dst = _dst.getMat();

    Sobel( src, Dx, CV_32F, 1, 0, ksize, 1, 0, borderType );
    Sobel( src, Dy, CV_32F, 0, 1, ksize, 1, 0, borderType );
    Sobel( src, D2x, CV_32F, 2, 0, ksize, 1, 0, borderType );
    Sobel( src, D2y, CV_32F, 0, 2, ksize, 1, 0, borderType );
    Sobel( src, Dxy, CV_32F, 1, 1, ksize, 1, 0, borderType );

    double factor = 1 << (ksize - 1);
    if( src.depth() == CV_8U )
        factor *= 255;
    factor = 1./(factor * factor * factor);
#if CV_SIMD128
    float factor_f = (float)factor;
    bool haveSimd = hasSIMD128();
    v_float32x4 v_factor = v_setall_f32(factor_f), v_m2 = v_setall_f32(-2.0f);
#endif

    Size size = src.size();
    int i, j;
    for( i = 0; i < size.height; i++ )
    {
        float* dstdata = dst.ptr<float>(i);
        const float* dxdata = Dx.ptr<float>(i);
        const float* dydata = Dy.ptr<float>(i);
        const float* d2xdata = D2x.ptr<float>(i);
        const float* d2ydata = D2y.ptr<float>(i);
        const float* dxydata = Dxy.ptr<float>(i);

        j = 0;

#if CV_SIMD128
        if (haveSimd)
        {
            for( ; j <= size.width - v_float32x4::nlanes; j += v_float32x4::nlanes )
            {
                v_float32x4 v_dx = v_load(dxdata + j);
                v_float32x4 v_dy = v_load(dydata + j);

                v_float32x4 v_s1 = (v_dx * v_dx) * v_load(d2ydata + j);
                v_float32x4 v_s2 = v_muladd((v_dy * v_dy),  v_load(d2xdata + j), v_s1);
                v_float32x4 v_s3 = v_muladd((v_dy * v_dx) * v_load(dxydata + j), v_m2, v_s2);

                v_store(dstdata + j, v_s3 * v_factor);
            }
        }
#endif

        for( ; j < size.width; j++ )
        {
            float dx = dxdata[j];
            float dy = dydata[j];
            dstdata[j] = (float)(factor*(dx*dx*d2ydata[j] + dy*dy*d2xdata[j] - 2*dx*dy*dxydata[j]));
        }
    }
}

CV_IMPL void
cvCornerMinEigenVal( const CvArr* srcarr, CvArr* dstarr,
                     int block_size, int aperture_size )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst = cv::cvarrToMat(dstarr);

    CV_Assert( src.size() == dst.size() && dst.type() == CV_32FC1 );
    cv::cornerMinEigenVal( src, dst, block_size, aperture_size, cv::BORDER_REPLICATE );
}

CV_IMPL void
cvCornerHarris( const CvArr* srcarr, CvArr* dstarr,
                int block_size, int aperture_size, double k )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst = cv::cvarrToMat(dstarr);

    CV_Assert( src.size() == dst.size() && dst.type() == CV_32FC1 );
    cv::cornerHarris( src, dst, block_size, aperture_size, k, cv::BORDER_REPLICATE );
}


CV_IMPL void
cvCornerEigenValsAndVecs( const void* srcarr, void* dstarr,
                          int block_size, int aperture_size )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst = cv::cvarrToMat(dstarr);

    CV_Assert( src.rows == dst.rows && src.cols*6 == dst.cols*dst.channels() && dst.depth() == CV_32F );
    cv::cornerEigenValsAndVecs( src, dst, block_size, aperture_size, cv::BORDER_REPLICATE );
}


CV_IMPL void
cvPreCornerDetect( const void* srcarr, void* dstarr, int aperture_size )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst = cv::cvarrToMat(dstarr);

    CV_Assert( src.size() == dst.size() && dst.type() == CV_32FC1 );
    cv::preCornerDetect( src, dst, aperture_size, cv::BORDER_REPLICATE );
}

/* End of file */
