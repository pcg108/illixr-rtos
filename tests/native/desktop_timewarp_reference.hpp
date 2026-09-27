// Native oracle copied from pcg108/ILLIXR c9f4b6864d058211cb555a96bffa0e8c689001ca.
// Original math_util.hpp and headless_timewarp_vk CPU transform calculations.
// See ../../src/prediction-LICENSE.txt.
#pragma once

#include <Eigen/Dense>
#include <cmath>

namespace desktop_reference {
namespace math_util {
    /// Calculates a projection matrix with the given tangent angles and clip planes
    void projection(Eigen::Matrix4f* result, const float tan_left, const float tan_right, const float tan_up,
                    float const tan_down, const float near_z, const float far_z) {
        const float tan_width  = tan_right - tan_left;
        const float tan_height = tan_up - tan_down;

        // https://www.scratchapixel.com/lessons/3d-basic-rendering/perspective-and-orthographic-projection-matrix/building-basic-perspective-projection-matrix
        (*result)(0, 0) = 2 / tan_width;
        (*result)(0, 1) = 0;
        (*result)(0, 2) = (tan_right + tan_left) / tan_width;
        (*result)(0, 3) = 0;

        (*result)(1, 0) = 0;
        (*result)(1, 1) = 2 / tan_height;
        (*result)(1, 2) = (tan_up + tan_down) / tan_height;
        (*result)(1, 3) = 0;

        (*result)(2, 0) = 0;
        (*result)(2, 1) = 0;
        (*result)(2, 2) = -far_z / (far_z - near_z);
        (*result)(2, 3) = -(far_z * near_z) / (far_z - near_z);

        (*result)(3, 0) = 0;
        (*result)(3, 1) = 0;
        (*result)(3, 2) = -1;
        (*result)(3, 3) = 0;
    }

    /// Calculates a projection matrix with the given FoVs and clip planes
    void projection_fov(Eigen::Matrix4f* result, const float fov_left, const float fov_right, const float fov_up,
                        const float fov_down, const float near_z, const float far_z) {
        const float tan_left  = -tanf(static_cast<float>(fov_left * (M_PI / 180.0f)));
        const float tan_right = tanf(static_cast<float>(fov_right * (M_PI / 180.0f)));

        const float tan_down = -tanf(static_cast<float>(fov_down * (M_PI / 180.0f)));
        const float tan_up   = tanf(static_cast<float>(fov_up * (M_PI / 180.0f)));

        projection(result, tan_left, tan_right, tan_up, tan_down, near_z, far_z);
    }
} // namespace math_util
} // namespace ILLIXR

namespace desktop_reference {
struct Timewarp {
    static void calculate_timewarp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& renderProjectionMatrix,
                                             const Eigen::Matrix4f& renderViewMatrix, const Eigen::Matrix4f& newViewMatrix) {
        // Eigen stores matrices internally in column-major order.
        // However, the (i,j) accessors are row-major (i.e, the first argument
        // is which row, and the second argument is which column.)
        Eigen::Matrix4f texCoordProjection;
        texCoordProjection << 0.5f * renderProjectionMatrix(0, 0), 0.0f, 0.5f * renderProjectionMatrix(0, 2) - 0.5f, 0.0f, 0.0f,
            0.5f * renderProjectionMatrix(1, 1), 0.5f * renderProjectionMatrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f;

        // Calculate the delta between the view matrix used for rendering and
        // a more recent or predicted view matrix based on new sensor input.
        Eigen::Matrix4f inverseRenderViewMatrix = renderViewMatrix.inverse();

        Eigen::Matrix4f deltaViewMatrix = inverseRenderViewMatrix * newViewMatrix;

        deltaViewMatrix(0, 3) = 0.0f;
        deltaViewMatrix(1, 3) = 0.0f;
        deltaViewMatrix(2, 3) = 0.0f;

        // Accumulate the transforms.
        transform = texCoordProjection * deltaViewMatrix;
    }

};
}
