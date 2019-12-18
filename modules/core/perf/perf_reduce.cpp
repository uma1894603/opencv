#include "perf_precomp.hpp"
#include "opencv2/core/core_c.h"

namespace opencv_test
{
using namespace perf;

CV_ENUM(ROp, REDUCE_SUM, REDUCE_AVG, REDUCE_MAX, REDUCE_MIN, REDUCE_SUM2)
typedef tuple<Size, MatType, ROp> Size_MatType_ROp_t;
typedef perf::TestBaseWithParam<Size_MatType_ROp_t> Size_MatType_ROp;


PERF_TEST_P(Size_MatType_ROp, reduceR,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                ROp::all()
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int reduceOp = get<2>(GetParam());

    int ddepth = -1;
    if( CV_MAT_DEPTH(matType) < CV_32S && (reduceOp == REDUCE_SUM || reduceOp == REDUCE_AVG || reduceOp == REDUCE_SUM2) )
        ddepth = CV_32S;

    Mat src(sz, matType);
    Mat vec(1, sz.width, ddepth < 0 ? matType : ddepth);

    declare.in(src, WARMUP_RNG).out(vec);
    declare.time(100);

    int runs = 15;
    TEST_CYCLE_MULTIRUN(runs) reduce(src, vec, 0, reduceOp, ddepth);

    if (reduceOp == REDUCE_SUM2)
      SANITY_CHECK_NOTHING();
    else
      SANITY_CHECK(vec, 1);
}

PERF_TEST_P(Size_MatType_ROp, reduceC,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                ROp::all()
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int reduceOp = get<2>(GetParam());

    int ddepth = -1;
    if( CV_MAT_DEPTH(matType)< CV_32S && (reduceOp == REDUCE_SUM || reduceOp == REDUCE_AVG || reduceOp == REDUCE_SUM2) )
        ddepth = CV_32S;

    Mat src(sz, matType);
    Mat vec(sz.height, 1, ddepth < 0 ? matType : ddepth);

    declare.in(src, WARMUP_RNG).out(vec);
    declare.time(100);

    TEST_CYCLE() reduce(src, vec, 1, reduceOp, ddepth);

    if (reduceOp == REDUCE_SUM2)
      SANITY_CHECK_NOTHING();
    else
      SANITY_CHECK(vec, 1);
}

} // namespace
