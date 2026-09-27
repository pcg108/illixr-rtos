#pragma once
#include <Eigen/Dense>
#include <cstddef>
#include <cstdint>

namespace ILLIXR {
// orientation has the desktop imu_raw convention: Hamilton I-to-G quaternion,
// equivalently JPL G-to-I coefficients. The RTOS integrator's Hamilton G-to-I
// quaternion must be conjugated at publication. The w_hat endpoints are also
// adapted to the predictor's angular convention; see prediction_adapter.hpp.
// The producer's integration equations and accelerometer readings are unchanged.
struct PredictionState {
    std::int64_t timestamp_ns{-1}, previous_timestamp_ns{-1};
    Eigen::Vector3d position{Eigen::Vector3d::Zero()};
    Eigen::Vector3d velocity{Eigen::Vector3d::Zero()};
    Eigen::Quaterniond orientation{Eigen::Quaterniond::Identity()};
    Eigen::Vector3d w_hat{Eigen::Vector3d::Zero()}, a_hat{Eigen::Vector3d::Zero()};
    Eigen::Vector3d w_hat2{Eigen::Vector3d::Zero()}, a_hat2{Eigen::Vector3d::Zero()};
};
enum class PredictionConsumer : unsigned { Render, Timewarp, Count };
enum class PredictionStatus : unsigned { Valid, Fallback, Stale, Invalid };
struct PredictionResult {
    Eigen::Vector3f position{Eigen::Vector3f::Zero()};
    Eigen::Quaternionf orientation{Eigen::Quaternionf::Identity()};
    std::int64_t source_timestamp_ns{-1};
    std::uint64_t source_sequence{};
    std::int64_t computed_runtime_ns{}, target_timestamp_ns{}, horizon_ns{};
    PredictionStatus status{PredictionStatus::Fallback};
    [[nodiscard]] bool valid() const { return status == PredictionStatus::Valid; }
    [[nodiscard]] bool stale() const { return status == PredictionStatus::Stale; }
};
inline constexpr std::int64_t prediction_max_horizon_ns = 50'000'000;
} // namespace ILLIXR
