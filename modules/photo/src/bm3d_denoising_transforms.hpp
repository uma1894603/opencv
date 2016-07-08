/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective icvers.
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
//   * The name of Intel Corporation may not be used to endorse or promote products
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

#ifndef __OPENCV_BM3D_DENOISING_TRANSFORMS_HPP__
#define __OPENCV_BM3D_DENOISING_TRANSFORMS_HPP__

using namespace cv;

template <typename T>
inline static void shrink(T &val, T &nonZeroCount, const short &threshold)
{
    if (std::abs(val) < threshold)
        val = 0;
    else
        ++nonZeroCount;
}

inline static void hardThreshold2D(short *dst, short *thrMap, const int &templateWindowSizeSq)
{
    for (int i = 1; i < templateWindowSizeSq; ++i)
    {
        if (std::abs(dst[i] < thrMap[i]))
            dst[i] = 0;
    }
}

// Forward transform 4x4 block
template <typename T>
static void HaarColumn4x4(const T *src, short *dst, const int &step)
{
    const T *src0 = src;
    const T *src1 = src + 1 * step;
    const T *src2 = src + 2 * step;
    const T *src3 = src + 3 * step;

    short sum0 = (*src0 + *src1 + 1) >> 1;
    short sum1 = (*src2 + *src3 + 1) >> 1;
    short dif0 = *src0 - *src1;
    short dif1 = *src2 - *src3;

    short sum00 = (sum0 + sum1 + 1) >> 1;
    short dif00 = sum0 - sum1;

    dst[0 * 4] = sum00;
    dst[1 * 4] = dif00;
    dst[2 * 4] = dif0;
    dst[3 * 4] = dif1;
}

template <typename T>
static void HaarRow4x4(const T *src, short *dst)
{
    short sum0 = (src[0] + src[1] + 1) >> 1;
    short sum1 = (src[2] + src[3] + 1) >> 1;
    short dif0 = src[0] - src[1];
    short dif1 = src[2] - src[3];

    short sum00 = (sum0 + sum1 + 1) >> 1;
    short dif00 = sum0 - sum1;

    dst[0] = sum00;
    dst[1] = dif00;
    dst[2] = dif0;
    dst[3] = dif1;
}

template <typename T>
static void Haar4x4(const T *ptr, short *dst, const short &step)
{
    short temp[16];

    // Trans col
    HaarColumn4x4(ptr, temp, step);
    HaarColumn4x4(ptr + 1, temp + 1, step);
    HaarColumn4x4(ptr + 2, temp + 2, step);
    HaarColumn4x4(ptr + 3, temp + 3, step);

    // Trans rows
    HaarRow4x4(temp, dst);
    HaarRow4x4(temp + 1 * 4, dst + 1 * 4);
    HaarRow4x4(temp + 2 * 4, dst + 2 * 4);
    HaarRow4x4(temp + 3 * 4, dst + 3 * 4);
}

static void InvHaarColumn4x4(short *src, short *dst)
{
    short src0 = src[0 * 4] * 2;
    short src1 = src[1 * 4];
    short src2 = src[2 * 4];
    short src3 = src[3 * 4];

    short sum0 = (src0 + src1) >> 1;
    short dif0 = (src0 - src1) >> 1; // this stinks!
    sum0 *= 2;
    dif0 *= 2;

    dst[0 * 4] = (sum0 + src2) >> 1;
    dst[1 * 4] = (sum0 - src2) >> 1;
    dst[2 * 4] = (dif0 + src3) >> 1;
    dst[3 * 4] = (dif0 - src3) >> 1;
}

static void InvHaarRow4x4(short *src, short *dst)
{
    short src0 = src[0] * 2;
    short src1 = src[1];
    short src2 = src[2];
    short src3 = src[3];

    short sum0 = (src0 + src1) >> 1;
    short dif0 = (src0 - src1) >> 1; // this stinks!
    sum0 *= 2;
    dif0 *= 2;

    dst[0] = (sum0 + src2) >> 1;
    dst[1] = (sum0 - src2) >> 1;
    dst[2] = (dif0 + src3) >> 1;
    dst[3] = (dif0 - src3) >> 1;
}

static void InvHaar4x4(short *src)
{
    short temp[16];

    InvHaarColumn4x4(src, temp);
    InvHaarColumn4x4(src + 1, temp + 1);
    InvHaarColumn4x4(src + 2, temp + 2);
    InvHaarColumn4x4(src + 3, temp + 3);

    InvHaarRow4x4(temp, src);
    InvHaarRow4x4(temp + 1 * 4, src + 1 * 4);
    InvHaarRow4x4(temp + 2 * 4, src + 2 * 4);
    InvHaarRow4x4(temp + 3 * 4, src + 3 * 4);
}

/// 1D forward transformations

static short HaarTransformShrink2(short **z, const int &n, short *&thrMap)
{
    short sum = (z[0][n] + z[1][n] + 1) >> 1;
    short dif = z[0][n] - z[1][n];

    short nonZeroCount = 0;
    shrink(sum, nonZeroCount, *thrMap++);
    shrink(dif, nonZeroCount, *thrMap++);

    z[0][n] = sum;
    z[1][n] = dif;

    return nonZeroCount;
}

static short HaarTransformShrink4(short **z, const int &n, short *&thrMap)
{
    short sum0 = (z[0][n] + z[1][n] + 1) >> 1;
    short sum1 = (z[2][n] + z[3][n] + 1) >> 1;
    short dif0 = z[0][n] - z[1][n];
    short dif1 = z[2][n] - z[3][n];

    short sum00 = (sum0 + sum1 + 1) >> 1;
    short dif00 = sum0 - sum1;

    short nonZeroCount = 0;
    shrink(sum00, nonZeroCount, *thrMap++);
    shrink(dif00, nonZeroCount, *thrMap++);
    shrink(dif0, nonZeroCount, *thrMap++);
    shrink(dif1, nonZeroCount, *thrMap++);

    z[0][n] = sum00;
    z[1][n] = dif00;
    z[2][n] = dif0;
    z[3][n] = dif1;

    return nonZeroCount;
}

static short HaarTransformShrink8(short **z, const int &n, short *&thrMap)
{
    short sum0 = (z[0][n] + z[1][n] + 1) >> 1;
    short sum1 = (z[2][n] + z[3][n] + 1) >> 1;
    short sum2 = (z[4][n] + z[5][n] + 1) >> 1;
    short sum3 = (z[6][n] + z[7][n] + 1) >> 1;
    short dif0 = z[0][n] - z[1][n];
    short dif1 = z[2][n] - z[3][n];
    short dif2 = z[4][n] - z[5][n];
    short dif3 = z[6][n] - z[7][n];

    short sum00 = (sum0 + sum1 + 1) >> 1;
    short sum11 = (sum2 + sum3 + 1) >> 1;
    short dif00 = sum0 - sum1;
    short dif11 = sum2 - sum3;

    short sum000 = (sum00 + sum11 + 1) >> 1;
    short dif000 = sum00 - sum11;

    short nonZeroCount = 0;
    shrink(sum000, nonZeroCount, *thrMap++);
    shrink(dif000, nonZeroCount, *thrMap++);
    shrink(dif00, nonZeroCount, *thrMap++);
    shrink(dif11, nonZeroCount, *thrMap++);
    shrink(dif0, nonZeroCount, *thrMap++);
    shrink(dif1, nonZeroCount, *thrMap++);
    shrink(dif2, nonZeroCount, *thrMap++);
    shrink(dif3, nonZeroCount, *thrMap++);

    z[0][n] = sum000;
    z[1][n] = dif000;
    z[2][n] = dif00;
    z[3][n] = dif11;
    z[4][n] = dif0;
    z[5][n] = dif1;
    z[6][n] = dif2;
    z[7][n] = dif3;

    return nonZeroCount;
}

/// Functions for inverse 1D transforms

template <typename T>
static void InverseHaarTransform2(T **src, const int &n)
{
    T src0 = src[0][n] * 2;
    T src1 = src[1][n];

    src[0][n] = (src0 + src1) >> 1;
    src[1][n] = (src0 - src1) >> 1;
}

template <typename T>
static void InverseHaarTransform4(T **src, const int &n)
{
    T src0 = src[0][n] * 2;
    T src1 = src[1][n];
    T src2 = src[2][n];
    T src3 = src[3][n];

    T sum0 = (src0 + src1) >> 1;
    T dif0 = (src0 - src1) >> 1;
    sum0 *= 2;
    dif0 *= 2;

    src[0][n] = (sum0 + src2) >> 1;
    src[1][n] = (sum0 - src2) >> 1;
    src[2][n] = (dif0 + src3) >> 1;
    src[3][n] = (dif0 - src3) >> 1;
}

template <typename T>
static void InverseHaarTransform8(T **src, const int &n)
{
    T src0 = src[0][n] * 2;
    T src1 = src[1][n];
    T src2 = src[2][n];
    T src3 = src[3][n];
    T src4 = src[4][n];
    T src5 = src[5][n];
    T src6 = src[6][n];
    T src7 = src[7][n];

    T sum0 = (src0 + src1) >> 1;
    T dif0 = (src0 - src1) >> 1;
    sum0 *= 2;
    dif0 *= 2;

    T sum00 = (sum0 + src2) >> 1;
    T dif00 = (sum0 - src2) >> 1;
    T sum11 = (dif0 + src3) >> 1;
    T dif11 = (dif0 - src3) >> 1;
    sum00 *= 2;
    dif00 *= 2;
    sum11 *= 2;
    dif11 *= 2;

    src[0][n] = (sum00 + src4) >> 1;
    src[1][n] = (sum00 - src4) >> 1;
    src[2][n] = (dif00 + src5) >> 1;
    src[3][n] = (dif00 - src5) >> 1;
    src[4][n] = (sum11 + src6) >> 1;
    src[5][n] = (sum11 - src6) >> 1;
    src[6][n] = (dif11 + src7) >> 1;
    src[7][n] = (dif11 - src7) >> 1;
}

#endif