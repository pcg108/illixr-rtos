#include <zephyr/kernel.h>
#include <chrono>
#include <new>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../../src/data_format_opencv.hpp"
#include "../../src/threadloop.hpp"
#include "../../src/phonebook_new.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/replay.hpp"
#include "../openvins/openvins_queues.hpp"
#include "embedded_cam.hpp"

using namespace ILLIXR;

K_THREAD_STACK_DEFINE(offline_cam_stack, 524288);

class Offline_cam : public threadloop {
public:
    explicit Offline_cam(phonebook_new& pb)
        : threadloop{pb, "offline_cam", offline_cam_stack,
                     K_THREAD_STACK_SIZEOF(offline_cam_stack), 5, replay::requested_hart(replay::OFFLINE_CAM)} { }

    skip_option _p_should_skip() override {
        return next_idx_ == kEmbeddedCamCount ? skip_option::stop : skip_option::run;
    }

    void _p_one_iteration() override {
        if (!replay::sleep_until_dataset(kEmbeddedCam[next_idx_].ts_ns) || should_terminate()) return;
        const int64_t now = get_global_relative_clock().dataset_now_ns();
        size_t selected = next_idx_;
        while (selected + 1 < kEmbeddedCamCount && kEmbeddedCam[selected + 1].ts_ns <= now) ++selected;
        replay::count(replay::CAM_SKIPPED, selected - next_idx_);
        // Every selected timestamp is attempted once, including queue drops.
        next_idx_ = selected + 1;
        const auto& frame = kEmbeddedCam[selected];

        cv::Mat png0(1, static_cast<int>(frame.cam0_size), CV_8UC1,
                     const_cast<uint8_t*>(frame.cam0_png));
        cv::Mat png1(1, static_cast<int>(frame.cam1_size), CV_8UC1,
                     const_cast<uint8_t*>(frame.cam1_png));
        cv::Mat img0 = cv::imdecode(png0, cv::IMREAD_GRAYSCALE);
        cv::Mat img1 = cv::imdecode(png1, cv::IMREAD_GRAYSCALE);
        if (img0.empty() || img1.empty()) {
            replay::fail("camera_decode");
            return;
        }
        auto* msg = new (std::nothrow) CamMsg{
            time_point{std::chrono::nanoseconds{frame.ts_ns}}, img0, img1, selected
        };
        if (!msg) {
            replay::fail("camera_allocation");
            return;
        }
        if (replay::failed()) {
            delete msg;
            return;
        }
        replay::record_placement(replay::OFFLINE_CAM);
        if (k_msgq_put(&openvins_cam_queue, &msg, K_NO_WAIT) != 0) {
            delete msg;  // Last image references are released on return.
            replay::count(replay::CAM_DROPPED);
        } else {
            replay::count(replay::CAM_PUBLISHED);
            replay::record_placement(replay::OFFLINE_CAM, true);
            replay::highwater(replay::CAM_HIGHWATER, k_msgq_num_used_get(&openvins_cam_queue));
        }
    }

    void _p_thread_teardown() override { atomic_set(&replay::cam_done, 1); }

private:
    size_t next_idx_{0};
};

void start_offline_cam(phonebook_new& pb) {
    static Offline_cam instance{pb};
    instance.start();
}

REGISTER_PLUGIN(offline_cam);
