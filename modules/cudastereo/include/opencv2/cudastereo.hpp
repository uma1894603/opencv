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

#ifndef OPENCV_CUDASTEREO_HPP
#define OPENCV_CUDASTEREO_HPP

#ifndef __cplusplus
#  error cudastereo.hpp header must be compiled as C++
#endif

#include "opencv2/core/cuda.hpp"
#include "opencv2/calib3d.hpp"

/**
  @addtogroup cuda
  @{
    @defgroup cudastereo Stereo Correspondence
  @}
 */

namespace cv { namespace cuda {

//! @addtogroup cudastereo
//! @{

/////////////////////////////////////////
// StereoBM

/** @brief Class computing stereo correspondence (disparity map) using the block matching algorithm. :

@sa StereoBM
 */
class CV_EXPORTS StereoBM : public cv::StereoBM
{
public:
    using cv::StereoBM::compute;

    virtual void compute(InputArray left, InputArray right, OutputArray disparity, Stream& stream) = 0;
};

/** @brief Creates StereoBM object.

@param numDisparities the disparity search range. For each pixel algorithm will find the best
disparity from 0 (default minimum disparity) to numDisparities. The search range can then be
shifted by changing the minimum disparity.
@param blockSize the linear size of the blocks compared by the algorithm. The size should be odd
(as the block is centered at the current pixel). Larger block size implies smoother, though less
accurate disparity map. Smaller block size gives more detailed disparity map, but there is higher
chance for algorithm to find a wrong correspondence.
 */
CV_EXPORTS Ptr<cuda::StereoBM> createStereoBM(int numDisparities = 64, int blockSize = 19);

/////////////////////////////////////////
// StereoBeliefPropagation

/** @brief Class computing stereo correspondence using the belief propagation algorithm. :

The class implements algorithm described in @cite Felzenszwalb2006 . It can compute own data cost
(using a truncated linear model) or use a user-provided data cost.

@note
   StereoBeliefPropagation requires a lot of memory for message storage:

    \f[width \_ step  \cdot height  \cdot ndisp  \cdot 4  \cdot (1 + 0.25)\f]

    and for data cost storage:

    \f[width\_step \cdot height \cdot ndisp \cdot (1 + 0.25 + 0.0625 +  \dotsm + \frac{1}{4^{levels}})\f]

    width_step is the number of bytes in a line including padding.

StereoBeliefPropagation uses a truncated linear model for the data cost and discontinuity terms:

\f[DataCost = data \_ weight  \cdot \min ( \lvert Img_Left(x,y)-Img_Right(x-d,y)  \rvert , max \_ data \_ term)\f]

\f[DiscTerm =  \min (disc \_ single \_ jump  \cdot \lvert f_1-f_2  \rvert , max \_ disc \_ term)\f]

For more details, see @cite Felzenszwalb2006 .

By default, StereoBeliefPropagation uses floating-point arithmetics and the CV_32FC1 type for
messages. But it can also use fixed-point arithmetics and the CV_16SC1 message type for better
performance. To avoid an overflow in this case, the parameters must satisfy the following
requirement:

\f[10  \cdot 2^{levels-1}  \cdot max \_ data \_ term < SHRT \_ MAX\f]

@sa StereoMatcher
 */
class CV_EXPORTS StereoBeliefPropagation : public cv::StereoMatcher
{
public:
    using cv::StereoMatcher::compute;

    /** @overload */
    virtual void compute(InputArray left, InputArray right, OutputArray disparity, Stream& stream) = 0;

    /** @brief Enables the stereo correspondence operator that finds the disparity for the specified data cost.

    @param data User-specified data cost, a matrix of msg_type type and
    Size(\<image columns\>\*ndisp, \<image rows\>) size.
    @param disparity Output disparity map. If disparity is empty, the output type is CV_16SC1 .
    Otherwise, the type is retained. In 16-bit signed format, the disparity values do not have
    fractional bits.
    @param stream Stream for the asynchronous version.
     */
    virtual void compute(InputArray data, OutputArray disparity, Stream& stream = Stream::Null()) = 0;

    //! number of BP iterations on each level
    virtual int getNumIters() const = 0;
    virtual void setNumIters(int iters) = 0;

    //! number of levels
    virtual int getNumLevels() const = 0;
    virtual void setNumLevels(int levels) = 0;

    //! truncation of data cost
    virtual double getMaxDataTerm() const = 0;
    virtual void setMaxDataTerm(double max_data_term) = 0;

    //! data weight
    virtual double getDataWeight() const = 0;
    virtual void setDataWeight(double data_weight) = 0;

    //! truncation of discontinuity cost
    virtual double getMaxDiscTerm() const = 0;
    virtual void setMaxDiscTerm(double max_disc_term) = 0;

    //! discontinuity single jump
    virtual double getDiscSingleJump() const = 0;
    virtual void setDiscSingleJump(double disc_single_jump) = 0;

    //! type for messages (CV_16SC1 or CV_32FC1)
    virtual int getMsgType() const = 0;
    virtual void setMsgType(int msg_type) = 0;

    /** @brief Uses a heuristic method to compute the recommended parameters ( ndisp, iters and levels ) for the
    specified image size ( width and height ).
     */
    static void estimateRecommendedParams(int width, int height, int& ndisp, int& iters, int& levels);
};

/** @brief Creates StereoBeliefPropagation object.

@param ndisp Number of disparities.
@param iters Number of BP iterations on each level.
@param levels Number of levels.
@param msg_type Type for messages. CV_16SC1 and CV_32FC1 types are supported.
 */
CV_EXPORTS Ptr<cuda::StereoBeliefPropagation>
    createStereoBeliefPropagation(int ndisp = 64, int iters = 5, int levels = 5, int msg_type = CV_32F);

/////////////////////////////////////////
// StereoConstantSpaceBP

/** @brief Class computing stereo correspondence using the constant space belief propagation algorithm. :

The class implements algorithm described in @cite Yang2010 . StereoConstantSpaceBP supports both local
minimum and global minimum data cost initialization algorithms. For more details, see the paper
mentioned above. By default, a local algorithm is used. To enable a global algorithm, set
use_local_init_data_cost to false .

StereoConstantSpaceBP uses a truncated linear model for the data cost and discontinuity terms:

\f[DataCost = data \_ weight  \cdot \min ( \lvert I_2-I_1  \rvert , max \_ data \_ term)\f]

\f[DiscTerm =  \min (disc \_ single \_ jump  \cdot \lvert f_1-f_2  \rvert , max \_ disc \_ term)\f]

For more details, see @cite Yang2010 .

By default, StereoConstantSpaceBP uses floating-point arithmetics and the CV_32FC1 type for
messages. But it can also use fixed-point arithmetics and the CV_16SC1 message type for better
performance. To avoid an overflow in this case, the parameters must satisfy the following
requirement:

\f[10  \cdot 2^{levels-1}  \cdot max \_ data \_ term < SHRT \_ MAX\f]

 */
class CV_EXPORTS StereoConstantSpaceBP : public cuda::StereoBeliefPropagation
{
public:
    //! number of active disparity on the first level
    virtual int getNrPlane() const = 0;
    virtual void setNrPlane(int nr_plane) = 0;

    virtual bool getUseLocalInitDataCost() const = 0;
    virtual void setUseLocalInitDataCost(bool use_local_init_data_cost) = 0;

    /** @brief Uses a heuristic method to compute parameters (ndisp, iters, levelsand nrplane) for the specified
    image size (widthand height).
     */
    static void estimateRecommendedParams(int width, int height, int& ndisp, int& iters, int& levels, int& nr_plane);
};

/** @brief Creates StereoConstantSpaceBP object.

@param ndisp Number of disparities.
@param iters Number of BP iterations on each level.
@param levels Number of levels.
@param nr_plane Number of disparity levels on the first level.
@param msg_type Type for messages. CV_16SC1 and CV_32FC1 types are supported.
 */
CV_EXPORTS Ptr<cuda::StereoConstantSpaceBP>
    createStereoConstantSpaceBP(int ndisp = 128, int iters = 8, int levels = 4, int nr_plane = 4, int msg_type = CV_32F);

/////////////////////////////////////////
// DisparityBilateralFilter

/** @brief Class refining a disparity map using joint bilateral filtering. :

The class implements @cite Yang2010 algorithm.
 */
class CV_EXPORTS DisparityBilateralFilter : public cv::Algorithm
{
public:
    /** @brief Refines a disparity map using joint bilateral filtering.

    @param disparity Input disparity map. CV_8UC1 and CV_16SC1 types are supported.
    @param image Input image. CV_8UC1 and CV_8UC3 types are supported.
    @param dst Destination disparity map. It has the same size and type as disparity .
    @param stream Stream for the asynchronous version.
     */
    virtual void apply(InputArray disparity, InputArray image, OutputArray dst, Stream& stream = Stream::Null()) = 0;

    virtual int getNumDisparities() const = 0;
    virtual void setNumDisparities(int numDisparities) = 0;

    virtual int getRadius() const = 0;
    virtual void setRadius(int radius) = 0;

    virtual int getNumIters() const = 0;
    virtual void setNumIters(int iters) = 0;

    //! truncation of data continuity
    virtual double getEdgeThreshold() const = 0;
    virtual void setEdgeThreshold(double edge_threshold) = 0;

    //! truncation of disparity continuity
    virtual double getMaxDiscThreshold() const = 0;
    virtual void setMaxDiscThreshold(double max_disc_threshold) = 0;

    //! filter range sigma
    virtual double getSigmaRange() const = 0;
    virtual void setSigmaRange(double sigma_range) = 0;
};

/** @brief Creates DisparityBilateralFilter object.

@param ndisp Number of disparities.
@param radius Filter radius.
@param iters Number of iterations.
 */
CV_EXPORTS Ptr<cuda::DisparityBilateralFilter>
    createDisparityBilateralFilter(int ndisp = 64, int radius = 3, int iters = 1);

/////////////////////////////////////////
// DepthMapDenoiseWeightedHuber

/** @brief Class refining a depth map. It is part of DTAM

The class implements the complicated half of the DTAM (@cite Newcombe11)
algorithm's mapping core, but can be used independently to refine depthmaps.
The high level algorithm in @cite Newcombe11 is based on @cite Chambolle11

Code contributed by Paul Foster for GSoC 2014 OpenDTAM project.

In 3D reconstruction, a common way to find the location of points in space is to intersect the rays of one image with another, and considering points where rays of the same color intersect to be more likely. This gives a cost as a function of the depth chosen at each pixel:

\f[C(d)\f]

(This is implicitly summed over the whole image.)
The problem is that many different depths will produce reasonable colors when pixels are considered individually so we add a term to the cost penalizing the difference between pixels and add a weighting factor:

\f[f\mathbf{(\nabla\mathbf{d}) + \lambda C(d)}\f]

where \f$\mathbf{\nabla d}\f$ is the difference between neighbouring pixels.

@note
   This is a slight abuse of notation since there are actually four neighbors for each pixel, so we evaluate the left term over each pair of neighbors and sum.

One common choice for \f$f\f$ is the *Huber norm*, which is:

\f[\left\| \nabla \mathbf{d} \right\|_e= \left\{\begin{matrix} \frac{(\nabla \mathbf{d})^2}{2\epsilon} & \mathrm{if} \left | \nabla \mathbf{d} \right | < \epsilon \\ \left |\nabla \mathbf{d} \right | - \frac{\epsilon}{2} &\mathrm{else} \end{matrix}\right\}\f]

This is the same as a metal wire: it starts out acting like a spring, but when stretched too much it deforms plastically.

This gives:

\f[\left\| \nabla \mathbf{d} \right\|_e  + \lambda \mathbf{C(\mathbf{d})}\f]

This problem is intractable to solve so we do a *relaxation*: we repeatedly solve the left and right sides independently, but enforce that the two solutions must be increasingly similar as we go along. We do this by creating a spring force between the two solutions. Remember from physics that a spring's energy is expressed as \f$\frac{1}{2} k(x_1-x_2)^2\f$, so we write:

\f[\left\| \nabla \mathbf{d} \right\|_e + \frac{1}{2\theta} (\mathbf{d-a})^2  + \lambda \mathbf{C(\mathbf{a})}\f]


\f$\frac{1}{\theta}\f$ is the spring constant, \f$\mathbf{d}\f$ is one solution, and \f$\mathbf{a}\f$ is the other. We refer to \f$\theta\f$ as the *stiffness*.

We can also give a hint to the left hand side that certain places are likely to have discontinuities by varying the thickness of the wires:

\f[\mathbf{g}\left\| \nabla \mathbf{d} \right\|_e + \frac{1}{2\theta} (\mathbf{d-a})^2  + \lambda \mathbf{C(\mathbf{a})}\f]

The function \f$\mathbf{g}\f$ is the weight function.

The right half is a literal search through all possible values of \f$\mathbf{C(\mathbf{a})}\f$ for each pixel.

It turns out that solving the left half:

\f[\mathbf{g}\left\| \nabla \mathbf{d} \right\|_e + \frac{1}{2\theta} (\mathbf{d-a}_{fixed})^2\f]

is quite hard.

*/
class CV_EXPORTS DepthmapDenoiseWeightedHuber : public cv::Algorithm
{
public:
    //! This may be called repeatedly to iteratively refine the internal depthmap
    virtual cv::cuda::GpuMat operator()(InputArray input,
                                        float epsilon,
                                        float theta) = 0;

    //! In case you want to do these explicitly


    //* \f$gx(x,y)\f$ is the weight between pixels \f$(x,y)\f$ and \f$(x+1,y)\f$ (right neighbor)
    //* \f$gy(x,y)\f$ is the weight between pixels \f$(x,y)\f$ and \f$(x,y+1)\f$ (down neighbor)
    /** Use to preallocate memory for the functor or replace the internal \f$g\f$ function buffers with custom ones. */
    virtual void allocate(int rows, int cols, InputArray gx = GpuMat(),InputArray gy = GpuMat()) = 0;
    /** Used to precache the \f$g\f$ values or add a visibleLightImage after object creation. */
    virtual void cacheGValues(InputArray visibleLightImage = GpuMat()) = 0;

    // Following reserved for later use, not implemented
    virtual void setStream(Stream) = 0;//not implemented, may be unsafe to implement
    virtual Stream getStream() = 0;//not implemented

    virtual void setAlpha(float alpha) = 0;//not implemented
    virtual float getAlpha() = 0;//not implemented

    virtual void setBeta(float beta) = 0;//not implemented
    virtual float getBeta() = 0;//not implemented
};

//! The visibleLightImage is a CV_32FC1 grayscale image of the scene, which can be used as a hint for edge placement.
/** Generates a denoising functor to handle the algorithm state on the GPU. */
CV_EXPORTS Ptr<DepthmapDenoiseWeightedHuber>
    createDepthmapDenoiseWeightedHuber(InputArray visibleLightImage=GpuMat(), Stream cvStream=Stream::Null());

/////////////////////////////////////////
// Utility

/** @brief Reprojects a disparity image to 3D space.

@param disp Input single-channel 8-bit unsigned, 16-bit signed, 32-bit signed or 32-bit
floating-point disparity image. If 16-bit signed format is used, the values are assumed to have no
fractional bits.
@param xyzw Output 3- or 4-channel floating-point image of the same size as disp . Each element of
xyzw(x,y) contains 3D coordinates (x,y,z) or (x,y,z,1) of the point (x,y) , computed from the
disparity map.
@param Q \f$4 \times 4\f$ perspective transformation matrix that can be obtained via stereoRectify .
@param dst_cn The number of channels for output image. Can be 3 or 4.
@param stream Stream for the asynchronous version.

@sa reprojectImageTo3D
 */
CV_EXPORTS void reprojectImageTo3D(InputArray disp, OutputArray xyzw, InputArray Q, int dst_cn = 4, Stream& stream = Stream::Null());

/** @brief Colors a disparity image.

@param src_disp Input single-channel 8-bit unsigned, 16-bit signed, 32-bit signed or 32-bit
floating-point disparity image. If 16-bit signed format is used, the values are assumed to have no
fractional bits.
@param dst_disp Output disparity image. It has the same size as src_disp. The type is CV_8UC4
in BGRA format (alpha = 255).
@param ndisp Number of disparities.
@param stream Stream for the asynchronous version.

This function draws a colored disparity map by converting disparity values from [0..ndisp) interval
first to HSV color space (where different disparity values correspond to different hues) and then
converting the pixels to RGB for visualization.
 */
CV_EXPORTS void drawColorDisp(InputArray src_disp, OutputArray dst_disp, int ndisp, Stream& stream = Stream::Null());

//! @}

}} // namespace cv { namespace cuda {

#endif /* OPENCV_CUDASTEREO_HPP */
