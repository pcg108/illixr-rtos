#include "../../src/plugin_registry.hpp"
#include "../../src/replay.hpp"
#include "../../src/threadloop.hpp"
#include "imu_integrator_queue.hpp"
#if ILLIXR_GPU_PIPELINE
#include "../../src/pose_prediction.hpp"
#include "../../src/prediction_adapter.hpp"
#endif
#include <cmath>
#include <zephyr/kernel.h>

using namespace ILLIXR;
K_MSGQ_DEFINE(imu_integrator_queue, sizeof(ImuSample), ILLIXR_IMU_QUEUE_CAPACITY, alignof(ImuSample));
K_THREAD_STACK_DEFINE(imu_integrator_stack, 65536);

class ImuIntegrator : public threadloop {
  public:
    explicit ImuIntegrator(phonebook_new &pb)
        : threadloop{pb, "imu_integrator", imu_integrator_stack, K_THREAD_STACK_SIZEOF(imu_integrator_stack), 5,
                     replay::requested_hart(replay::IMU_INTEGRATOR_WORKER)} {}

  protected:
    skip_option _p_should_skip() override {
        if (atomic_get(&replay::imu_done) && atomic_get(&replay::vio_done) &&
            k_msgq_num_used_get(&imu_integrator_queue) == 0) {
            if (apply_latest_baseline())
                publish();
            return skip_option::stop;
        }
        return skip_option::run;
    }
    void _p_one_iteration() override {
        ImuSample sample;
        if (k_msgq_get(&imu_integrator_queue, &sample, K_MSEC(1)) == 0) {
            const ImuMsg msg = imu_message(sample);
            if (history_size_ == history_capacity) {
                history_begin_ = (history_begin_ + 1) % history_capacity;
                --history_size_;
                history_wrapped_ = true;
            }
            history_[(history_begin_ + history_size_) % history_capacity] = msg;
            ++history_size_;
            replay::highwater(replay::INTEGRATOR_HISTORY_HIGHWATER, history_size_);
            replay::count(replay::IMU_INTEGRATOR);
            replay::record_placement(replay::IMU_INTEGRATOR_WORKER);
            const bool reset = apply_latest_baseline();
            if (!reset && has_state_)
                integrate(msg);
            publish();
        } else {
            if (apply_latest_baseline())
                publish();
        }
    }
    void _p_thread_teardown() override {
        // Producers may still be finishing on failure; runtime drains any final records after joins.
        k_msgq_purge(&imu_integrator_queue);
        atomic_set(&replay::integrator_done, 1);
    }

  private:
    static constexpr size_t history_capacity = 4096;
    ImuMsg history_[history_capacity];
    size_t history_begin_{}, history_size_{};
    bool history_wrapped_{}, has_state_{};
    uint64_t baseline_sequence_{};
    int64_t baseline_ts_{-1}, last_ts_{-1}, published_ts_{-1};
    double last_t_{-1};
    Eigen::Vector3d position_{Eigen::Vector3d::Zero()}, velocity_{Eigen::Vector3d::Zero()};
    Eigen::Quaterniond orientation_{Eigen::Quaterniond::Identity()};
    Eigen::Vector3d bias_gyro_{Eigen::Vector3d::Zero()}, bias_accel_{Eigen::Vector3d::Zero()}, gravity_{0, 0, -9.81};
    bool apply_latest_baseline() {
        ImuIntegratorInput input{};
        uint64_t seq{};
        if (!replay::vio_baseline.read(input, seq) || seq == baseline_sequence_)
            return false;
        baseline_sequence_ = seq;
        const auto ts = input.timestamp.time_since_epoch().count();
        if (ts <= baseline_ts_)
            return false;
        if (history_wrapped_ && history_size_ && ts < history_[history_begin_].time.time_since_epoch().count()) {
            replay::fail("VIO baseline predates retained IMU history");
            return false;
        }
        baseline_ts_ = ts;
        last_ts_ = ts;
        last_t_ = static_cast<double>(ts) * 1e-9;
        position_ = input.position;
        velocity_ = input.velocity;
        orientation_ = input.orientation.normalized();
        bias_gyro_ = input.bias_gyro;
        bias_accel_ = input.bias_accel;
        gravity_ = input.params.n_gravity;
        has_state_ = true;
        for (size_t i = 0; i < history_size_; ++i)
            integrate(history_[(history_begin_ + i) % history_capacity]);
        return true;
    }
    void integrate(const ImuMsg &imu) {
        const auto ts = imu.time.time_since_epoch().count();
        if (ts <= last_ts_)
            return;
        const double t = static_cast<double>(ts) * 1e-9;
        const double dt = (last_t_ < 0) ? 0 : (t - last_t_);
        last_ts_ = ts;
        last_t_ = t;
        if (dt <= 0 || dt > 0.1)
            return;
        // Existing integration equations are intentionally unchanged.
        Eigen::Vector3d gyro = imu.angular_v - bias_gyro_;
        Eigen::Vector3d accel = imu.linear_a - bias_accel_;
        Eigen::Matrix3d R_ItoG = orientation_.toRotationMatrix().transpose();
        Eigen::Vector3d accel_global = R_ItoG * accel;
        Eigen::Vector3d accel_true = accel_global - gravity_;
        position_ += velocity_ * dt + 0.5 * accel_true * dt * dt;
        velocity_ += accel_true * dt;
        double angle = gyro.norm() * dt;
        if (angle > 1e-10) {
            Eigen::Quaterniond dq(Eigen::AngleAxisd(angle, gyro.normalized()));
            orientation_ = (orientation_ * dq).normalized();
        }
    }
    void publish() {
        if (!has_state_ || last_ts_ < published_ts_)
            return;
        PoseMsg out{time_point{duration{last_ts_}}, position_.cast<float>(), orientation_.cast<float>().normalized()};
        if (!replay::valid_pose(out)) {
            replay::fail("invalid propagated pose");
            return;
        }
        replay::propagated_pose.publish(out);
#if ILLIXR_GPU_PIPELINE
        // Export a coherent value snapshot. Prediction never reads this
        // worker's mutable integration state, and no integration equations
        // change when the downstream profile is enabled.
        const ImuMsg *latest = nullptr, *previous = nullptr;
        for (size_t i = history_size_; i > 0; --i) {
            const auto &sample = history_[(history_begin_ + i - 1) % history_capacity];
            if (sample.time.time_since_epoch().count() > last_ts_)
                continue;
            if (!latest)
                latest = &sample;
            else {
                previous = &sample;
                break;
            }
        }
        if (latest && previous) {
            PredictionState state;
            state.timestamp_ns = last_ts_;
            state.previous_timestamp_ns = previous->time.time_since_epoch().count();
            state.position = position_;
            state.velocity = velocity_;
            // Adapt both the quaternion and angular-rate convention. A
            // quaternion-only conversion would reverse predicted rotation
            // relative to this integrator's unchanged right-multiplied update.
            adapt_integrator_rotation(state, orientation_,
                previous->angular_v - bias_gyro_, latest->angular_v - bias_gyro_);
            state.a_hat = previous->linear_a - bias_accel_;
            state.a_hat2 = latest->linear_a - bias_accel_;
            prediction_state.publish(state);
        }
#endif
        replay::record_placement(replay::IMU_INTEGRATOR_WORKER, true);
        published_ts_ = last_ts_;
    }
};
void start_imu_integrator(phonebook_new &pb) {
    static ImuIntegrator instance{pb};
    instance.start();
}
REGISTER_PLUGIN(imu_integrator);
