#include <zephyr/kernel.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <memory>

#include "SLAMMath.hpp"
#include "openvins_queues.hpp"
#include "../../src/threadloop.hpp"
#include "../../src/phonebook_new.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/data_format.hpp"
#include "../../src/data_format_opencv.hpp"
#include "../../src/replay.hpp"

using namespace ILLIXR;
using namespace OpenVINS;

K_THREAD_STACK_DEFINE(openvins_stack, 16777216);
K_MSGQ_DEFINE(openvins_imu_queue, sizeof(ImuSample), ILLIXR_IMU_QUEUE_CAPACITY, alignof(ImuSample));
K_MSGQ_DEFINE(openvins_cam_queue, sizeof(CamMsg*), ILLIXR_CAM_QUEUE_CAPACITY, alignof(CamMsg*));

#ifndef ILLIXR_VIO_DELAY_MS
#define ILLIXR_VIO_DELAY_MS 0
#endif
#ifndef ILLIXR_VIO_DELAY_AFTER_CAM
#define ILLIXR_VIO_DELAY_AFTER_CAM 1
#endif

// Original estimator configuration and EuRoC calibration are unchanged.
VIOConfig create_vio_config() {
    VIOConfig config;

    config.cam0.fx = 458.654;  config.cam0.fy = 457.296;
    config.cam0.cx = 367.215;  config.cam0.cy = 248.375;
    config.cam0.k1 = -0.28340811; config.cam0.k2 =  0.07395907;
    config.cam0.p1 =  0.00019359; config.cam0.p2 =  1.76187114e-05;

    config.cam1.fx = 457.587;  config.cam1.fy = 456.134;
    config.cam1.cx = 379.999;  config.cam1.cy = 255.238;
    config.cam1.k1 = -0.28368365; config.cam1.k2 =  0.07451284;
    config.cam1.p1 = -0.00010473; config.cam1.p2 = -3.55590700e-05;

    Eigen::Matrix4d T_C0toI;
    T_C0toI <<  0.0148655429818, -0.999880929698,  0.00414029679422, -0.0216401454975,
                0.999557249008,   0.0149672133247,  0.025715529948,   -0.064676986768,
               -0.0257744366974,  0.00375618835797, 0.999660727178,    0.00981073058949,
                0, 0, 0, 1;
    config.R_ItoC0 = T_C0toI.block<3,3>(0,0).transpose();
    config.p_C0inI = -config.R_ItoC0 * T_C0toI.block<3,1>(0,3);

    Eigen::Matrix4d T_C1toI;
    T_C1toI <<  0.0125552670891, -0.999755099723,  0.0182237714554, -0.0198435579556,
                0.999598781151,   0.0130119051815,  0.0251588363115,  0.0453689425024,
               -0.0253898008918,  0.0179005838253,  0.999517347078,   0.00786212447038,
                0, 0, 0, 1;
    config.R_ItoC1 = T_C1toI.block<3,3>(0,0).transpose();
    config.p_C1inI = -config.R_ItoC1 * T_C1toI.block<3,1>(0,3);

    config.sigma_gyro       = 0.00016968;
    config.sigma_accel      = 0.002;
    config.sigma_gyro_bias  = 1.9393e-05;
    config.sigma_accel_bias = 0.003;
    config.sigma_pixel      = 1.0;
    config.gravity << 0, 0, -9.81;

    config.max_features           = 150;
    config.min_features           = 50;
    config.max_clone_size         = 20;
    config.min_track_length       = 3;
    config.max_reprojection_error = 2.0;
    config.template_size          = 15;
    config.search_radius          = 20;
    config.stereo_search_radius   = 60;
    config.match_threshold        = 0.8;
    config.fb_check_thresh        = 2.0;

    return config;
}

// All estimator calls belong to this worker. Waiting for camera/IMU coverage
// is local to VIO; neither producer nor the integrator waits for this worker.
class OpenVINS_Plugin : public threadloop {
public:
    explicit OpenVINS_Plugin(phonebook_new& pb)
        : threadloop{pb, "openvins", openvins_stack,
                     K_THREAD_STACK_SIZEOF(openvins_stack), 5, replay::requested_hart(replay::OPENVINS)} { }

    void _p_thread_setup() override {
        vio_config_ = create_vio_config();
        vio_estimator_ = new (std::nothrow) MSCKFEstimator(vio_config_);
        if (!vio_estimator_) replay::fail("vio_allocation");
    }

    skip_option _p_should_skip() override {
        return drained_ ? skip_option::stop : skip_option::run;
    }

    void _p_one_iteration() override {
        CamMsg* camera = nullptr;
        if (k_msgq_get(&openvins_cam_queue, &camera, K_MSEC(10)) != 0) {
            if (atomic_get(&replay::cam_done)) {
                // Producer completion is published after its final put. Drain
                // any last message that raced the timed receive before EOS.
                if (k_msgq_get(&openvins_cam_queue, &camera, K_NO_WAIT) != 0) {
                    drain_final_imu();
                    return;
                }
            } else {
                return;
            }
        }
        if (!camera) {
            replay::fail("null_camera_message");
            return;
        }

        std::unique_ptr<CamMsg> camera_owner{camera};
        if (ILLIXR_VIO_DELAY_MS > 0 && !delay_injected_ &&
            cam_count_ >= ILLIXR_VIO_DELAY_AFTER_CAM) {
            delay_injected_ = true;
            // A controlled local stall for the producer-independence tests.
            replay::trace_delay(true);
            k_msleep(ILLIXR_VIO_DELAY_MS);
            replay::trace_delay(false);
        }
        const int64_t camera_ns = camera->time.time_since_epoch().count();
        while (latest_imu_ns_ < camera_ns && !should_terminate()) {
            ImuSample imu;
            if (k_msgq_get(&openvins_imu_queue, &imu, K_MSEC(10)) == 0) {
                process_imu(imu_message(imu));
            } else if (atomic_get(&replay::imu_done) &&
                       k_msgq_num_used_get(&openvins_imu_queue) == 0) {
                replay::fail("imu_does_not_cover_camera");
                break;
            }
        }
        if (!should_terminate()) process_camera_frame(*camera);
    }

    void _p_thread_teardown() override {
        // On a failed run producers also leave their loops. Wait for their EOS
        // before discarding residual records, so final publications cannot leak.
        while (!atomic_get(&replay::imu_done) || !atomic_get(&replay::cam_done)) k_msleep(1);
        k_msgq_purge(&openvins_imu_queue);
        CamMsg* camera = nullptr;
        while (k_msgq_get(&openvins_cam_queue, &camera, K_NO_WAIT) == 0) delete camera;
        delete vio_estimator_;
        vio_estimator_ = nullptr;
        atomic_set(&replay::vio_done, 1);
    }

private:
    VIOConfig vio_config_;
    MSCKFEstimator* vio_estimator_{nullptr};
    size_t cam_count_{0};
    int64_t latest_imu_ns_{std::numeric_limits<int64_t>::min()};
    int64_t latest_cam_ns_{std::numeric_limits<int64_t>::min()};
    bool drained_{false};
    bool delay_injected_{false};

    void drain_final_imu() {
        ImuSample imu;
        if (k_msgq_get(&openvins_imu_queue, &imu, K_MSEC(10)) == 0) {
            process_imu(imu_message(imu));
        } else if (atomic_get(&replay::imu_done) &&
                   k_msgq_num_used_get(&openvins_imu_queue) == 0) {
            drained_ = true;
        }
    }

    void process_imu(const ImuMsg& msg) {
        const int64_t timestamp_ns = msg.time.time_since_epoch().count();
        if (timestamp_ns <= latest_imu_ns_) {
            replay::fail("nonmonotonic_vio_imu");
            return;
        }
        latest_imu_ns_ = timestamp_ns;
        replay::trace_imu(msg.index, timestamp_ns);
        vio_estimator_->feed_imu(static_cast<double>(timestamp_ns) * 1e-9,
                                 msg.angular_v, msg.linear_a);
        replay::count(replay::IMU_VIO);
        replay::record_placement(replay::OPENVINS);
    }

    void process_camera_frame(const CamMsg& msg) {
        const int64_t timestamp_ns = msg.time.time_since_epoch().count();
        if (timestamp_ns <= latest_cam_ns_) {
            replay::fail("nonmonotonic_vio_camera");
            return;
        }
        if (msg.img0.empty() || msg.img1.empty() ||
            msg.img0.type() != CV_8UC1 || msg.img1.type() != CV_8UC1) {
            replay::fail("invalid_stereo_images");
            return;
        }
        latest_cam_ns_ = timestamp_ns;
        ++cam_count_;
        replay::trace_cam(msg.index, timestamp_ns);
        vio_estimator_->feed_stereo(static_cast<double>(timestamp_ns) * 1e-9,
                                    msg.img0, msg.img1);
        replay::count(replay::CAM_VIO);
        replay::record_placement(replay::OPENVINS);
        if (!vio_estimator_->is_initialized()) return;

        const IMUState& state = vio_estimator_->get_state();
        if (!state.p_IinG.allFinite() || !state.v_IinG.allFinite() ||
            !state.q_GtoI.coeffs().allFinite() || !state.b_accel.allFinite() ||
            !state.b_gyro.allFinite() || state.q_GtoI.norm() == 0.0) {
            replay::fail("nonfinite_vio_state");
            return;
        }

        // Preserve the original adapter's float cast and normalization.
        Eigen::Vector3f pos_f = state.p_IinG.cast<float>();
        Eigen::Quaternionf quat_f = state.q_GtoI.cast<float>();
        quat_f.normalize();
        PoseMsg pose_msg{msg.time, pos_f, quat_f};
        ImuIntegratorInput integrator_msg{
            msg.time,
            duration{0},
            ImuParams{
                vio_config_.sigma_gyro,
                vio_config_.sigma_accel,
                vio_config_.sigma_gyro_bias,
                vio_config_.sigma_accel_bias,
                vio_config_.gravity,
                1.0,
                200.0
            },
            state.b_accel,
            state.b_gyro,
            state.p_IinG,
            state.v_IinG,
            state.q_GtoI
        };
        replay::publish_vio(pose_msg, integrator_msg);
    }
};

void start_openvins(phonebook_new& pb) {
    static OpenVINS_Plugin instance{pb};
    instance.start();
}

REGISTER_PLUGIN(openvins);
