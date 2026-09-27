#pragma once

#include <Eigen/Dense>
#include <cmath>

namespace ILLIXR::timewarp_math {

// CPU-side projection and rotational timewarp from pcg108/ILLIXR,
// c9f4b6864d058211cb555a96bffa0e8c689001ca:
// include/illixr/math_util.hpp and headless_timewarp_vk/plugin.cpp.
// No shader, distortion mesh, or image resampling is performed here.
inline Eigen::Matrix4f projection() {
  constexpr float pi = 3.14159265358979323846f;
  const float tan_left = -std::tan(45.0f * (pi / 180.0f));
  const float tan_right = std::tan(45.0f * (pi / 180.0f));
  const float tan_down = tan_left;
  const float tan_up = tan_right;
  constexpr float near_z = 0.1f, far_z = 20.0f;
  Eigen::Matrix4f result = Eigen::Matrix4f::Zero();
  result(0, 0) = 2 / (tan_right - tan_left);
  result(0, 2) = (tan_right + tan_left) / (tan_right - tan_left);
  result(1, 1) = 2 / (tan_up - tan_down);
  result(1, 2) = (tan_up + tan_down) / (tan_up - tan_down);
  result(2, 2) = -far_z / (far_z - near_z);
  result(2, 3) = -(far_z * near_z) / (far_z - near_z);
  result(3, 2) = -1;
  return result;
}

inline Eigen::Matrix4f transform(const Eigen::Quaternionf &render_orientation,
                               const Eigen::Quaternionf &new_orientation) {
  const Eigen::Matrix4f render_projection = projection();
  Eigen::Matrix4f render_view = Eigen::Matrix4f::Identity();
  Eigen::Matrix4f new_view = Eigen::Matrix4f::Identity();
  render_view.block<3, 3>(0, 0) = render_orientation.toRotationMatrix();
  new_view.block<3, 3>(0, 0) = new_orientation.toRotationMatrix();
  Eigen::Matrix4f tex_coord_projection;
  tex_coord_projection <<
      0.5f * render_projection(0, 0), 0.0f,
      0.5f * render_projection(0, 2) - 0.5f, 0.0f,
      0.0f, 0.5f * render_projection(1, 1),
      0.5f * render_projection(1, 2) - 0.5f, 0.0f,
      0.0f, 0.0f, -1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f;
  Eigen::Matrix4f delta_view = render_view.inverse() * new_view;
  delta_view(0, 3) = 0.0f;
  delta_view(1, 3) = 0.0f;
  delta_view(2, 3) = 0.0f;
  return tex_coord_projection * delta_view;
}

} // namespace ILLIXR::timewarp_math
