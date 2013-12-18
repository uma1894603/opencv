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
// Copyright (C) 2009-2010, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Matthias Bady aegirxx ==> gmail.com
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
#include "opencl_kernels.hpp"

using namespace cv;
using namespace cv::ocl;

BRIEF_OCL::BRIEF_OCL( int _bytes ) : bytes( _bytes )
{
}

void
BRIEF_OCL::compute( const oclMat& image, oclMat& keypoints, oclMat& descriptors ) const
{
    oclMat grayImage = image;
    if ( image.type( ) != CV_8U ) cvtColor( image, grayImage, COLOR_BGR2GRAY );

    oclMat sum;
    integral( grayImage, sum, CV_32S );
    cl_mem sumTexture = bindTexture(sum);

    //TODO filter keypoints by border

    oclMat xRow = keypoints.row( 0 );
    oclMat yRow = keypoints.row( 1 );
    descriptors = oclMat( keypoints.cols, bytes, CV_8U );

    std::stringstream build_opt;
    build_opt << " -D BYTES=" << bytes << " -D KERNEL_SIZE=" << KERNEL_SIZE;

    const String kernelname = "extractBriefDescriptors";
    size_t globalThreads[3] = {keypoints.cols, 1, 1};
    size_t localThreads[3] = {1, 1, 1};

    std::vector< std::pair<size_t, const void *> > args;
    args.push_back(std::make_pair(sizeof(cl_mem), (void *)&sumTexture));
    args.push_back(std::make_pair(sizeof(cl_mem), (void *)&xRow.data));
    args.push_back(std::make_pair(sizeof(cl_mem), (void *)&yRow.data));
    args.push_back(std::make_pair(sizeof(cl_mem), (void *)&descriptors.data));
    Context* ctx = Context::getContext( );
    openCLExecuteKernel( ctx, &brief, kernelname, globalThreads, localThreads, args, -1, -1, build_opt.str().c_str() );
    openCLFree(sumTexture);
}

//optimiuerungsstrategieen:
// - vorher ganzes bild blurren (vgl. orb)