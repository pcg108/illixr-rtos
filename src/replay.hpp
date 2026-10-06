#include "trace_output.hpp"
#pragma once
#include "data_format.hpp"
#include "latest_value.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#ifndef ILLIXR_PIN_PLUGINS
#define ILLIXR_PIN_PLUGINS 0
#endif

namespace ILLIXR::replay {
inline atomic_t imu_done{}, cam_done{}, vio_done{}, integrator_done{},
    failure{};
inline char failure_reason[128]{};
enum Counter {
  IMU_PUBLISHED,
  IMU_VIO,
  IMU_INTEGRATOR,
  CAM_PUBLISHED,
  CAM_SKIPPED,
  CAM_DROPPED,
  CAM_VIO,
  IMU_OVERFLOW,
  IMU_VIO_HIGHWATER,
  IMU_INT_HIGHWATER,
  CAM_HIGHWATER,
  VIO_POSES,
  PROBE_READS,
  PROBE_REPEATED_VIO,
  PROBE_IMU_ADVANCED,
  PROBE_MISSED,
  TRACE_OVERFLOW,
  INTEGRATOR_HISTORY_HIGHWATER,
  COUNTER_COUNT
};
inline atomic_t counters[COUNTER_COUNT]{};
inline void count(Counter c, std::size_t n = 1) { atomic_add(&counters[c], n); }
inline long value(Counter c) { return atomic_get(&counters[c]); }
inline void highwater(Counter c, std::size_t n) {
  atomic_val_t old = atomic_get(&counters[c]);
  while (old < static_cast<atomic_val_t>(n) &&
         !atomic_cas(&counters[c], old, n))
    old = atomic_get(&counters[c]);
}
inline bool failed() { return atomic_get(&failure) != 0; }
inline void fail(const char *why) {
  if (atomic_cas(&failure, 0, 1)) {
    std::strncpy(failure_reason, why, sizeof(failure_reason) - 1);
  }
}
inline LatestValue<PoseMsg> slow_pose, propagated_pose;
inline double max_observed_propagated_position_norm{};
inline LatestValue<ImuIntegratorInput> vio_baseline;
inline unsigned hart_id() {
  unsigned long id;
  __asm__ volatile("csrr %0, mhartid" : "=r"(id));
  return static_cast<unsigned>(id);
}
enum Worker {
  OFFLINE_IMU,
  OFFLINE_CAM,
  OPENVINS,
  IMU_INTEGRATOR_WORKER,
#if ILLIXR_GPU_PIPELINE
  RENDER_WORKER,
  TIMEWARP_WORKER,
#endif
  WORKER_COUNT
};
inline constexpr const char *worker_names[] = {"offline_imu", "offline_cam",
                                               "openvins", "imu_integrator"
#if ILLIXR_GPU_PIPELINE
                                               , "render_loop", "timewarp"
#endif
};
inline constexpr unsigned hart_count = CONFIG_MP_MAX_NUM_CPUS;
static_assert(hart_count == 1 || hart_count == 2 || hart_count == 4,
              "supported replay core counts: 1, 2, 4");
inline int requested_hart(Worker worker) {
#if ILLIXR_GPU_PIPELINE
  // The downstream experiment uses scheduler placement; retain the existing
  // four-plugin affinity mapping without accidentally assigning hart 4 or 5.
  if (worker == RENDER_WORKER || worker == TIMEWARP_WORKER)
    return -1;
#endif
  if (!ILLIXR_PIN_PLUGINS)
    return -1;
  if (hart_count == 1)
    return 0;
  if (hart_count == 2)
    return worker == OPENVINS ? 1 : 0;
  return static_cast<int>(worker);
}
struct Placement {
  // Each record has one worker writer; main reads only after every worker
  // joins.
  uint64_t work_counts[hart_count]{};
  uint64_t publication_counts[hart_count]{};
  unsigned hart_mask{};
};
inline Placement placements[WORKER_COUNT];
inline void record_placement(Worker worker, bool publication = false) {
  const auto hart = hart_id();
  if (hart >= hart_count) {
    fail("work observed on unexpected hart");
    return;
  }
  auto &record = placements[worker];
  record.hart_mask |= 1u << hart;
  ++(publication ? record.publication_counts[hart] : record.work_counts[hart]);
  const int requested = requested_hart(worker);
  if (requested >= 0 && static_cast<unsigned>(requested) != hart)
    fail("plugin affinity violation");
}
inline void dump_placement() {
  for (unsigned worker = 0; worker < WORKER_COUNT; ++worker) {
    const auto &record = placements[worker];
    trace_output::print("ILLIXR_PLACEMENT "
           "{\"plugin\":\"%s\",\"requested_hart\":%d,\"hart_mask\":%u,\"work_"
           "counts\":[",
           worker_names[worker], requested_hart(static_cast<Worker>(worker)),
           record.hart_mask);
    for (unsigned hart = 0; hart < hart_count; ++hart)
      trace_output::print("%s%llu", hart ? "," : "",
             (unsigned long long)record.work_counts[hart]);
    trace_output::print("],\"publication_counts\":[");
    for (unsigned hart = 0; hart < hart_count; ++hart)
      trace_output::print("%s%llu", hart ? "," : "",
             (unsigned long long)record.publication_counts[hart]);
    trace_output::print("]}\n");
  }
}

/**
 * A trace entry represents a single event in the system's execution.
 * Used for logging/recording events for analysis
 */
struct TraceEntry {
  char kind{};
  std::uint64_t index{};
  std::int64_t time{};
  double pose[7]{};
  std::uint64_t fields[5]{};
};
inline constexpr std::size_t trace_capacity = 32768;
inline TraceEntry trace_entries[trace_capacity];
inline std::size_t trace_size{};
inline struct k_mutex trace_mutex;
inline std::size_t current_cam_index{}; // VIO worker only.
inline void initialize() { k_mutex_init(&trace_mutex); }
inline void trace(const TraceEntry &entry) {
  k_mutex_lock(&trace_mutex, K_FOREVER);
  if (trace_size < trace_capacity)
    trace_entries[trace_size++] = entry;
  else {
    count(TRACE_OVERFLOW);
    fail("trace capacity exceeded");
  }
  k_mutex_unlock(&trace_mutex);
}
inline void trace_imu(std::size_t index, std::int64_t ts) {
  TraceEntry e{};
  e.kind = 'I';
  e.index = index;
  e.time = ts;
  trace(e);
}
inline void trace_cam(std::size_t index, std::int64_t ts) {
  current_cam_index = index;
  TraceEntry e{};
  e.kind = 'C';
  e.index = index;
  e.time = ts;
  trace(e);
}
////

inline bool valid_pose(const PoseMsg &p) {
  return p.position.allFinite() && p.orientation.coeffs().allFinite() &&
         std::abs(p.orientation.norm() - 1.0f) < 1e-3f;
}
inline void publish_vio(const PoseMsg &p, const ImuIntegratorInput &baseline) {
  if (!valid_pose(p)) {
    fail("invalid VIO pose");
    return;
  }
  slow_pose.publish(p);
  vio_baseline.publish(baseline);
  record_placement(OPENVINS, true);
  count(VIO_POSES);
  TraceEntry e{};
  e.kind = 'P';
  e.index = current_cam_index;
  e.time = p.timestamp.time_since_epoch().count();
  e.pose[0] = p.position.x();
  e.pose[1] = p.position.y();
  e.pose[2] = p.position.z();
  e.pose[3] = p.orientation.w();
  e.pose[4] = p.orientation.x();
  e.pose[5] = p.orientation.y();
  e.pose[6] = p.orientation.z();
  trace(e);
}
inline bool sleep_until_dataset(std::int64_t timestamp) {
  auto &clock = get_global_relative_clock();
  while (!failed()) {
    const auto left = timestamp - clock.dataset_now_ns();
    if (left <= 0)
      return true;
    // Deadline stays absolute; short waits bound failure response latency.
    const auto wake = clock.now_ns() + (left < 10000000 ? left : 10000000);
    k_sleep(K_TIMEOUT_ABS_TICKS(clock.absolute_ticks(wake)));
  }
  return false;
}

/*
 * Sample latest available poses, check validity, and record whether estimation pipeline is making progress 
*/
inline void observe_pose() {
  static std::uint64_t previous_slow{}, previous_fast{};
  PoseMsg slow{}, fast{};
  std::uint64_t ss{}, fs{};
  const bool have_slow = slow_pose.read(slow, ss),
             have_fast = propagated_pose.read(fast, fs);
  if ((have_slow && !valid_pose(slow)) || (have_fast && !valid_pose(fast)))
    fail("invalid observed pose");
  count(PROBE_READS);
  if (have_slow && ss == previous_slow) {
    count(PROBE_REPEATED_VIO);
    if (fs > previous_fast)
      count(PROBE_IMU_ADVANCED);
  }
  if (have_fast) {
    const double radius = fast.position.cast<double>().norm();
    if (radius > max_observed_propagated_position_norm)
      max_observed_propagated_position_norm = radius;
  }
  previous_slow = ss;
  previous_fast = fs;
  TraceEntry e{};
  e.kind = 'R';
  e.time = get_global_relative_clock().now_ns();
  e.index = ss;
  e.fields[0] = have_slow ? slow.timestamp.time_since_epoch().count() : 0;
  e.fields[1] = fs;
  e.fields[2] = have_fast ? fast.timestamp.time_since_epoch().count() : 0;
  e.fields[3] = hart_id();
  trace(e);
}
inline void trace_delay(bool begin) {
  TraceEntry e{};
  e.kind = 'D';
  e.index = begin ? 1 : 0;
  e.time = get_global_relative_clock().now_ns();
  e.fields[0] = value(IMU_PUBLISHED);
  e.fields[1] = value(CAM_PUBLISHED);
  e.fields[2] = value(PROBE_READS);
  trace(e);
}
inline void dump_trace() {
  for (std::size_t i = 0; i < trace_size; ++i) {
    const auto &e = trace_entries[i];
    if (e.kind == 'I' || e.kind == 'C')
      trace_output::print("ILLIXR_TRACE %s %llu\n", e.kind == 'I' ? "IMU" : "CAM",
             (unsigned long long)e.index);
    else if (e.kind == 'P')
      trace_output::print(
          "ILLIXR_POSE %llu %lld %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
          (unsigned long long)e.index, (long long)e.time, e.pose[0], e.pose[1],
          e.pose[2], e.pose[3], e.pose[4], e.pose[5], e.pose[6]);
    else if (e.kind == 'D')
      trace_output::print("ILLIXR_DELAY %s %lld %llu %llu %llu\n", e.index ? "BEGIN" : "END",
             (long long)e.time, (unsigned long long)e.fields[0],
             (unsigned long long)e.fields[1], (unsigned long long)e.fields[2]);
    else if (e.kind == 'R')
      trace_output::print("ILLIXR_PROBE %lld %llu %llu %llu %llu %llu\n", (long long)e.time,
             (unsigned long long)e.index, (unsigned long long)e.fields[0],
             (unsigned long long)e.fields[1], (unsigned long long)e.fields[2],
             (unsigned long long)e.fields[3]);
  }
}
} // namespace ILLIXR::replay
