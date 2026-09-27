#pragma once
#include "prediction_types.hpp"

namespace ILLIXR {
// Preserve the existing integrator's angular evolution when exporting its
// Hamilton G-to-I quaternion to the desktop predictor's JPL G-to-I convention.
// For R = rotation(integrator_orientation), the existing integrator has
//   R_dot = R [omega_H]x,
// while the desktop predictor has
//   R_dot = -[omega_D]x R.
// Therefore omega_D = -R omega_H. Conjugating the quaternion alone (or merely
// negating omega) does not preserve angular evolution for general orientations.
// Both endpoints use the current snapshot rotation as their common reference.
// This matches the instantaneous derivative and constant-rate evolution;
// time-varying measurements retain the kernels' distinct integration schemes.
inline void adapt_integrator_rotation(PredictionState& snapshot,
                                     const Eigen::Quaterniond& integrator_orientation,
                                     const Eigen::Vector3d& previous_corrected_gyro,
                                     const Eigen::Vector3d& latest_corrected_gyro) {
    const Eigen::Matrix3d rotation = integrator_orientation.toRotationMatrix();
    snapshot.orientation = integrator_orientation.conjugate();
    snapshot.w_hat = -rotation * previous_corrected_gyro;
    snapshot.w_hat2 = -rotation * latest_corrected_gyro;
}
} // namespace ILLIXR
