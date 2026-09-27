#pragma once
// Prediction math ported from pcg108/ILLIXR c9f4b6864d058211cb555a96bffa0e8c689001ca,
// plugins/pose_prediction/plugin.cpp. See prediction-LICENSE.txt for upstream terms.
// The RK4 arithmetic, fixed +9.81 gravity and JPL quaternion conventions are retained.
#include "prediction_types.hpp"
#include <cmath>

namespace ILLIXR {
struct DesktopPredictionMath {
    static Eigen::Matrix<double, 13, 1> predict_mean_rk4(const PredictionState& state, double dt) {
        // Pre-compute things
        // The desktop caller provides a single coherent imu_raw snapshot here.
        const PredictionState* imu_raw = &state;
        if (dt == 0.0) {
            Eigen::Matrix<double, 13, 1> result = Eigen::Matrix<double, 13, 1>::Zero();
            result.template head<4>() = Eigen::Vector4d{state.orientation.x(), state.orientation.y(), state.orientation.z(), state.orientation.w()};
            result.template segment<3>(4) = state.position;
            result.template segment<3>(7) = state.velocity;
            return result;
        }

        Eigen::Vector3d w_hat   = imu_raw->w_hat;
        Eigen::Vector3d a_hat   = imu_raw->a_hat;
        Eigen::Vector3d w_alpha = (imu_raw->w_hat2 - imu_raw->w_hat) / dt;
        Eigen::Vector3d a_jerk  = (imu_raw->a_hat2 - imu_raw->a_hat) / dt;

        // y0 ================
        Eigen::Quaterniond temp_quat = imu_raw->orientation;
        Eigen::Vector4d    q_0       = {temp_quat.x(), temp_quat.y(), temp_quat.z(), temp_quat.w()};
        Eigen::Vector3d    p_0       = imu_raw->position;
        Eigen::Vector3d    v_0       = imu_raw->velocity;

        // k1 ================
        Eigen::Vector4d dq_0   = {0, 0, 0, 1};
        Eigen::Vector4d q0_dot = 0.5 * Omega(w_hat) * dq_0;
        Eigen::Matrix3d R_Gto0 = quat_2_Rot(quat_multiply(dq_0, q_0));
        Eigen::Vector3d v0_dot = R_Gto0.transpose() * a_hat - Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k1_q = q0_dot * dt;
        Eigen::Vector3d k1_p = v_0 * dt;
        Eigen::Vector3d k1_v = v0_dot * dt;

        // k2 ================
        w_hat += 0.5 * w_alpha * dt;
        a_hat += 0.5 * a_jerk * dt;

        Eigen::Vector4d dq_1 = quatnorm(dq_0 + 0.5 * k1_q);
        Eigen::Vector3d v_1  = v_0 + 0.5 * k1_v;

        Eigen::Vector4d q1_dot = 0.5 * Omega(w_hat) * dq_1;
        Eigen::Matrix3d R_Gto1 = quat_2_Rot(quat_multiply(dq_1, q_0));
        Eigen::Vector3d v1_dot = R_Gto1.transpose() * a_hat - Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k2_q = q1_dot * dt;
        Eigen::Vector3d k2_p = v_1 * dt;
        Eigen::Vector3d k2_v = v1_dot * dt;

        // k3 ================
        Eigen::Vector4d dq_2 = quatnorm(dq_0 + 0.5 * k2_q);
        // Eigen::Vector3d p_2 = p_0+0.5*k2_p;
        Eigen::Vector3d v_2 = v_0 + 0.5 * k2_v;

        Eigen::Vector4d q2_dot = 0.5 * Omega(w_hat) * dq_2;
        Eigen::Matrix3d R_Gto2 = quat_2_Rot(quat_multiply(dq_2, q_0));
        Eigen::Vector3d v2_dot = R_Gto2.transpose() * a_hat - Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k3_q = q2_dot * dt;
        Eigen::Vector3d k3_p = v_2 * dt;
        Eigen::Vector3d k3_v = v2_dot * dt;

        // k4 ================
        w_hat += 0.5 * w_alpha * dt;
        a_hat += 0.5 * a_jerk * dt;

        Eigen::Vector4d dq_3 = quatnorm(dq_0 + k3_q);
        // Eigen::Vector3d p_3 = p_0+k3_p;
        Eigen::Vector3d v_3 = v_0 + k3_v;

        Eigen::Vector4d q3_dot = 0.5 * Omega(w_hat) * dq_3;
        Eigen::Matrix3d R_Gto3 = quat_2_Rot(quat_multiply(dq_3, q_0));
        Eigen::Vector3d v3_dot = R_Gto3.transpose() * a_hat - Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k4_q = q3_dot * dt;
        Eigen::Vector3d k4_p = v_3 * dt;
        Eigen::Vector3d k4_v = v3_dot * dt;

        // y+dt ================
        Eigen::Matrix<double, 13, 1> state_plus = Eigen::Matrix<double, 13, 1>::Zero();
        Eigen::Vector4d dq = quatnorm(dq_0 + (1.0 / 6.0) * k1_q + (1.0 / 3.0) * k2_q + (1.0 / 3.0) * k3_q + (1.0 / 6.0) * k4_q);
        state_plus.block(0, 0, 4, 1) = quat_multiply(dq, q_0);
        state_plus.block(4, 0, 3, 1) = p_0 + (1.0 / 6.0) * k1_p + (1.0 / 3.0) * k2_p + (1.0 / 3.0) * k3_p + (1.0 / 6.0) * k4_p;
        state_plus.block(7, 0, 3, 1) = v_0 + (1.0 / 6.0) * k1_v + (1.0 / 3.0) * k2_v + (1.0 / 3.0) * k3_v + (1.0 / 6.0) * k4_v;

        return state_plus;
    }

    /**
     * @brief Integrated quaternion from angular velocity
     *
     * See equation (48) of trawny tech report [Indirect Kalman Filter for 3D Attitude
     * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf).
     *
     */
    static inline Eigen::Matrix<double, 4, 4> Omega(Eigen::Matrix<double, 3, 1> w) {
        Eigen::Matrix<double, 4, 4> mat;
        mat.block(0, 0, 3, 3) = -skew_x(w);
        mat.block(3, 0, 1, 3) = -w.transpose();
        mat.block(0, 3, 3, 1) = w;
        mat(3, 3)             = 0;
        return mat;
    }

    /**
     * @brief Normalizes a quaternion to make sure it is unit norm
     * @param q_t Quaternion to normalized
     * @return Normalized quaterion
     */
    static inline Eigen::Matrix<double, 4, 1> quatnorm(Eigen::Matrix<double, 4, 1> q_t) {
        if (q_t(3, 0) < 0) {
            q_t *= -1;
        }
        return q_t / q_t.norm();
    }

    /**
     * @brief Skew-symmetric matrix from a given 3x1 vector
     *
     * This is based on equation 6 in [Indirect Kalman Filter for 3D Attitude
     * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf): \f{align*}{ \lfloor\mathbf{v}\times\rfloor =
     *  \begin{bmatrix}
     *  0 & -v_3 & v_2 \\ v_3 & 0 & -v_1 \\ -v_2 & v_1 & 0
     *  \end{bmatrix}
     * @f}
     *
     * @param[in] w 3x1 vector to be made a skew-symmetric
     * @return 3x3 skew-symmetric matrix
     */
    static inline Eigen::Matrix<double, 3, 3> skew_x(const Eigen::Matrix<double, 3, 1>& w) {
        Eigen::Matrix<double, 3, 3> w_x;
        w_x << 0, -w(2), w(1), w(2), 0, -w(0), -w(1), w(0), 0;
        return w_x;
    }

    /**
     * @brief Converts JPL quaterion to SO(3) rotation matrix
     *
     * This is based on equation 62 in [Indirect Kalman Filter for 3D Attitude
     * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf): \f{align*}{ \mathbf{R} =
     * (2q_4^2-1)\mathbf{I}_3-2q_4\lfloor\mathbf{q}\times\rfloor+2\mathbf{q}^\top\mathbf{q}
     * @f}
     *
     * @param[in] q JPL quaternion
     * @return 3x3 SO(3) rotation matrix
     */
    static inline Eigen::Matrix<double, 3, 3> quat_2_Rot(const Eigen::Matrix<double, 4, 1>& q) {
        Eigen::Matrix<double, 3, 3> q_x = skew_x(q.block(0, 0, 3, 1));
        Eigen::Matrix3d             Rot = (2 * std::pow(q(3, 0), 2) - 1) * Eigen::Matrix3d::Identity() - 2 * q(3, 0) * q_x +
            2 * q.block(0, 0, 3, 1) * (q.block(0, 0, 3, 1).transpose());
        return Rot;
    }

    /**
     * @brief Multiply two JPL quaternions
     *
     * This is based on equation 9 in [Indirect Kalman Filter for 3D Attitude
     * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf). We also enforce that the quaternion is unique by having q_4
     * be greater than zero. \f{align*}{ \bar{q}\otimes\bar{p}= \mathcal{L}(\bar{q})\bar{p}= \begin{bmatrix}
     *  q_4\mathbf{I}_3+\lfloor\mathbf{q}\times\rfloor & \mathbf{q} \\
     *  -\mathbf{q}^\top & q_4
     *  \end{bmatrix}
     *  \begin{bmatrix}
     *  \mathbf{p} \\ p_4
     *  \end{bmatrix}
     * @f}
     *
     * @param[in] q First JPL quaternion
     * @param[in] p Second JPL quaternion
     * @return 4x1 resulting p*q quaternion
     */
    static inline Eigen::Matrix<double, 4, 1> quat_multiply(const Eigen::Matrix<double, 4, 1>& q,
                                                            const Eigen::Matrix<double, 4, 1>& p) {
        Eigen::Matrix<double, 4, 1> q_t;
        Eigen::Matrix<double, 4, 4> Qm;
        // create big L matrix
        Qm.block(0, 0, 3, 3) = q(3, 0) * Eigen::Matrix3d::Identity() - skew_x(q.block(0, 0, 3, 1));
        Qm.block(0, 3, 3, 1) = q.block(0, 0, 3, 1);
        Qm.block(3, 0, 1, 3) = -q.block(0, 0, 3, 1).transpose();
        Qm(3, 3)             = q(3, 0);
        q_t                  = Qm * p;
        // ensure unique by forcing q_4 to be >0
        if (q_t(3, 0) < 0) {
            q_t *= -1;
        }
        // normalize and return
        return q_t / q_t.norm();
    }
};
} // namespace ILLIXR
