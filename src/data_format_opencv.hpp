#pragma once
#include <cstddef>
#include <relative_clock.hpp>
#include <opencv2/core.hpp>
#include <Eigen/Dense>
// in ../../src/data_format.hpp (approx)
namespace ILLIXR {
    using ullong = unsigned long long;

    struct CamMsg {
        time_point      time;
        cv::Mat         img0;
        cv::Mat         img1;
        size_t          index{0};  // Original embedded stereo-pair index.
    };

}
