#include <zephyr/kernel.h>
#include <chrono>

#include "../../src/threadloop.hpp"
#include "../../src/phonebook_new.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/data_format.hpp"
#include "../../src/replay.hpp"
#include "../openvins/openvins_queues.hpp"
#include "../imu_integrator/imu_integrator_queue.hpp"
#include "embedded_imu.hpp"

using namespace ILLIXR;

K_THREAD_STACK_DEFINE(offline_imu_stack, 262144);

class Offline_imu : public threadloop {
public:
    explicit Offline_imu(phonebook_new& pb)
        : threadloop{pb, "offline_imu", offline_imu_stack,
                     K_THREAD_STACK_SIZEOF(offline_imu_stack), 3, replay::requested_hart(replay::OFFLINE_IMU)} { }

    skip_option _p_should_skip() override {
        return current_idx_ == kEmbeddedImuCount ? skip_option::stop : skip_option::run;
    }

    void _p_one_iteration() override {
        const auto& sample = kEmbeddedImu[current_idx_];
        if (!replay::sleep_until_dataset(sample.ts_ns) || should_terminate()) return;

        const ImuSample message{
            sample.ts_ns, {sample.wx, sample.wy, sample.wz},
            {sample.ax, sample.ay, sample.az}, current_idx_
        };

        // Publication never waits for VIO or integration. Any lost IMU makes
        // this run invalid, including loss at just one subscriber.
        const int vio_rc = k_msgq_put(&openvins_imu_queue, &message, K_NO_WAIT);
        const int int_rc = k_msgq_put(&imu_integrator_queue, &message, K_NO_WAIT);
        if (vio_rc != 0 || int_rc != 0) {
            replay::count(replay::IMU_OVERFLOW);
            replay::fail("imu_queue_overflow");
            return;
        }
        replay::count(replay::IMU_PUBLISHED);
        replay::record_placement(replay::OFFLINE_IMU);
        replay::record_placement(replay::OFFLINE_IMU, true);
        replay::highwater(replay::IMU_VIO_HIGHWATER, k_msgq_num_used_get(&openvins_imu_queue));
        replay::highwater(replay::IMU_INT_HIGHWATER, k_msgq_num_used_get(&imu_integrator_queue));
        ++current_idx_;
    }

    void _p_thread_teardown() override {
        // Separate EOS flags remain publishable even when a queue is full.
        atomic_set(&replay::imu_done, 1);
    }

private:
    size_t current_idx_{0};
};

void start_offline_imu(phonebook_new& pb) {
    static Offline_imu instance{pb};
    instance.start();
}

REGISTER_PLUGIN(offline_imu);
