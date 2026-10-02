#include "blas_backend.hpp"
#ifdef ILLIXR_EYE_TRACKING
#include "eye_tracking.hpp"
#endif
#include "vector_check.hpp"
#include "../plugins/imu_integrator/imu_integrator_queue.hpp"
#include "../plugins/openvins/openvins_queues.hpp"
#include "clock_check.hpp"
#include "data_format_opencv.hpp"
#include "phonebook_new.hpp"
#include "runtime.hpp"
#if ILLIXR_GPU_PIPELINE
#include "gpu_pipeline.hpp"
#include "pose_prediction.hpp"
#endif
#include <cstdio>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>

#ifndef ILLIXR_PLATFORM_CHECK_ONLY
#define ILLIXR_PLATFORM_CHECK_ONLY 0
#endif

extern "C" {
extern volatile uint64_t tohost;
extern volatile uint64_t fromhost;
}
static void simulator_exit(int code) {
  fflush(stdout);
  // HTIF device 0, command 0. Wait for the console's previous transaction.
  while (tohost) {
    k_yield();
  }
  __asm__ volatile("fence rw,rw" ::: "memory");
  tohost = (static_cast<uint64_t>(code) << 1) | 1;
  for (;;) {
    __asm__ volatile("wfi");
  }
}
int main() {
  using namespace ILLIXR;
  // Drain the deferred boot banner before emitting machine-readable records.
  log_flush();
  replay::initialize();
#if ILLIXR_GPU_PIPELINE
  gpu_pipeline::initialize();
#endif
  const auto online_harts = clock_check::run();
  clock_check::platform(online_harts);
#ifdef ILLIXR_DIAGNOSTIC_FPU_TRAP
  extern bool image_fpu_trap_check_entry();
  if (!image_fpu_trap_check_entry()) replay::fail("faulting FP divide modified its destination");
#endif
#ifdef ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC
  extern bool image_arithmetic_check_entry();
  if (!image_arithmetic_check_entry()) replay::fail("image arithmetic preflight failed");
#endif
  blas_backend::initialize();
  if (!vector_check::run()) replay::fail("vector context preflight failed");
  if (!replay::failed() && !blas_backend::self_test()) replay::fail("BLAS self-test failed");
  if (ILLIXR_PLATFORM_CHECK_ONLY) {
    printf("ILLIXR_DIAGNOSTIC %s\n", replay::failure_reason);
    simulator_exit(replay::failed() ? 1 : 0);
  }
  init_phonebook_global();
  auto &pb = get_phonebook();
  Runtime runtime{pb};
  int64_t runtime_ns = 0;
  if (!replay::failed()) {
    runtime.start_all_plugins();
    constexpr int64_t period_ns = 1000000000LL / 120;
    int64_t deadline = 0;
    auto &clock = get_global_relative_clock();
    while (!runtime.finished() && !replay::failed()) {
      replay::observe_pose();
#if ILLIXR_GPU_PIPELINE
      gpu_pipeline::observe_display(replay::hart_id());
#endif
      deadline += period_ns;
      const auto now = clock.now_ns();
      if (deadline <= now) {
        const auto missed = (now - deadline) / period_ns + 1;
        replay::count(replay::PROBE_MISSED, missed);
        deadline += missed * period_ns;
      }
      k_sleep(K_TIMEOUT_ABS_TICKS(clock.absolute_ticks(deadline)));
    }
    runtime.shutdown();
#if ILLIXR_GPU_PIPELINE
    if (!replay::failed()) gpu_pipeline::finish_presentation(replay::hart_id());
#endif
    runtime_ns = clock.now_ns();
  }
#ifdef ILLIXR_EYE_TRACKING
  eye_tracking::shutdown();
  if (get_global_relative_clock().is_started())
    runtime_ns = get_global_relative_clock().now_ns();
#endif
  // After every producer has joined it is safe to reclaim residual failure-path
  // records.
  CamMsg *cam = nullptr;
  k_msgq_purge(&openvins_imu_queue);
  k_msgq_purge(&imu_integrator_queue);
  while (k_msgq_get(&openvins_cam_queue, &cam, K_NO_WAIT) == 0)
    delete cam;
  using namespace replay;
  if (!failed() && (value(IMU_PUBLISHED) != value(IMU_VIO) ||
                    value(IMU_PUBLISHED) != value(IMU_INTEGRATOR)))
    fail("IMU end-of-stream count mismatch");
  const auto &export_clock = get_global_relative_clock();
  const auto export_start_ns = export_clock.is_started() ? export_clock.now_ns() : 0;
  trace_output::begin();
#if ILLIXR_GPU_PIPELINE
  if (!get_pose_prediction().validate())
    fail("prediction validation failed");
  gpu_pipeline::validate();
  get_pose_prediction().dump();
  gpu_pipeline::dump();
#endif
#ifdef ILLIXR_EYE_TRACKING
  eye_tracking::dump();
#endif
  dump_trace();
  dump_placement();
  blas_backend::dump();
  trace_output::print("ILLIXR_PROPAGATION {\"max_observed_position_norm_m\":%.17g}\n",
         max_observed_propagated_position_norm);
  if (!trace_output::finish()) fail("batched trace export failed");
  const auto export_end_ns = export_clock.is_started() ? export_clock.now_ns() : 0;
  // Only fixed diagnostic strings enter this JSON. Exceptions are printed
  // separately.
  printf("ILLIXR_DIAGNOSTIC %s\n", failure_reason);
  printf("ILLIXR_RESULT "
         "{\"status\":\"%s\",\"origin_ns\":%lld,\"online_harts\":%u,\"imu_"
         "published\":%ld,\"imu_processed\":%ld,"
         "\"imu_integrator_processed\":%ld,\"cam_published\":%ld,\"cam_"
         "processed\":%ld,\"cam_skipped\":%ld,\"cam_"
         "dropped\":%ld,\"imu_overflow\":%ld,\"initialized\":%s,\"vio_poses\":%"
         "ld,\"probe_reads\":%ld,\"probe_"
         "repeated_vio\":%ld,\"probe_imu_advanced\":%ld,\"probe_missed_"
         "deadlines\":%ld,\"trace_overflow\":%ld,\"imu_"
         "vio_highwater\":%ld,\"imu_integrator_highwater\":%ld,\"cam_"
         "highwater\":%ld,\"history_highwater\":%ld,"
         "\"runtime_ns\":%lld,\"trace_export_start_ns\":%lld,\"trace_export_end_ns\":%lld,\"trace_export_ns\":%lld}\n",
         failed() ? "fail" : "pass", (long long)kEmbeddedDatasetOriginNs,
         online_harts, value(IMU_PUBLISHED), value(IMU_VIO),
         value(IMU_INTEGRATOR), value(CAM_PUBLISHED), value(CAM_VIO),
         value(CAM_SKIPPED), value(CAM_DROPPED), value(IMU_OVERFLOW),
         value(VIO_POSES) ? "true" : "false", value(VIO_POSES),
         value(PROBE_READS), value(PROBE_REPEATED_VIO),
         value(PROBE_IMU_ADVANCED), value(PROBE_MISSED), value(TRACE_OVERFLOW),
         value(IMU_VIO_HIGHWATER), value(IMU_INT_HIGHWATER),
         value(CAM_HIGHWATER), value(INTEGRATOR_HISTORY_HIGHWATER),
         (long long)runtime_ns, (long long)export_start_ns, (long long)export_end_ns,
         (long long)(export_end_ns - export_start_ns));
  simulator_exit(failed() ? 1 : 0);
  return 0;
}
