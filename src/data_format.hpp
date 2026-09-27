#pragma once
#include <cstddef>
#include "imu_sample.hpp"
#include <relative_clock.hpp>
#include <Eigen/Dense>
// in ../../src/data_format.hpp (approx)
namespace ILLIXR {
    using ullong = unsigned long long;

    struct ImuMsg {
        time_point      time;
        Eigen::Vector3d angular_v;
        Eigen::Vector3d linear_a;
        std::size_t index{};
    };

    inline ImuMsg imu_message(const ImuSample& sample) {
        return {time_point{std::chrono::nanoseconds{sample.timestamp_ns}},
                Eigen::Vector3d{sample.angular_v[0], sample.angular_v[1], sample.angular_v[2]},
                Eigen::Vector3d{sample.linear_a[0], sample.linear_a[1], sample.linear_a[2]},
                sample.index};
    }
    struct PoseMsg {
        time_point timestamp;
        Eigen::Vector3f position;        // [x, y, z] meters
        Eigen::Quaternionf orientation;  // (w, x, y, z)
    };
    struct ImuParams {
        double gyro_noise;           // Gyro white noise
        double acc_noise;            // Accel white noise
        double gyro_walk;            // Gyro bias random walk
        double acc_walk;             // Accel bias random walk
        Eigen::Vector3d n_gravity;   // Gravity vector
        double imu_integration_sigma;
        double nominal_rate;
    };
    struct ImuIntegratorInput {
        time_point timestamp;
        duration cam_imu_offset;
        ImuParams params;
        Eigen::Vector3d bias_accel;    // Current biases
        Eigen::Vector3d bias_gyro;
        Eigen::Vector3d position;      // Current full state
        Eigen::Vector3d velocity;
        Eigen::Quaterniond orientation;
    };

}
