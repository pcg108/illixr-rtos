#pragma once
#include <cstddef>
#include <cstdint>

namespace ILLIXR::hpm {
inline constexpr unsigned counter_count = 15;
inline constexpr unsigned programmable_count = 13;
inline constexpr unsigned capacity = 128;
enum class Owner : unsigned { System, OfflineImu, OfflineCam, Openvins, Integrator,
  Prediction, OfflineEye, EyeTracking, Render, Timewarp, Main, Idle, Isr, Switch, Profiler, Count };
enum class Phase : unsigned { Default, Packing, Accelerator, Unpacking, Count };
inline constexpr const char* owners[] = {"system", "offline_imu", "offline_cam", "openvins",
  "imu_integrator", "pose_prediction", "offline_eye", "eye_tracking", "render_loop", "timewarp",
  "main_probe", "idle", "isr_body", "switch_gap", "profiler"};
inline constexpr const char* phases[] = {"default", "packing", "accelerator", "unpacking"};
inline constexpr const char* events[] = {"cycles", "instructions", "load_use_interlock", "long_latency_interlock",
  "csr_interlock", "icache_blocked", "dcache_blocked", "branch_direction_mispredict",
  "control_target_mispredict", "pipeline_flush", "pipeline_replay", "mul_div_interlock", "fp_interlock",
  "icache_acquire", "dcache_acquire"};
inline constexpr uint64_t selectors[] = {0x101,0x201,0x401,0x801,0x1001,0x2001,0x4001,
  0x8001,0x10001,0x20001,0x40001,0x102,0x202};
struct Context {
  Owner owner = Owner::System;
  Phase phase = Phase::Default;
  Owner caller = Owner::System;
  bool operator==(const Context& b) const { return owner==b.owner && phase==b.phase && caller==b.caller; }
};
struct Snapshot { uint64_t values[counter_count]{}; };
inline uint64_t delta(uint64_t before, uint64_t after, unsigned index) {
  return (after-before) & (index<2 ? UINT64_MAX : ((uint64_t(1)<<40)-1));
}
struct Record { Context context; Snapshot totals; uint64_t intervals{}; };
struct Accounting {
  Record records[capacity]{};
  unsigned size{};
  uint64_t errors{};
  void charge(Context context, const Snapshot& before, const Snapshot& after) {
    unsigned i=0;
    while(i<size && !(records[i].context==context)) ++i;
    if(i==size) {
      if(size==capacity) { ++errors; return; }
      auto& fresh=records[size++];fresh.context=context;fresh.totals={};fresh.intervals=0;
    }
    auto& r=records[i]; ++r.intervals;
    for(unsigned c=0;c<counter_count;++c) r.totals.values[c]+=delta(before.values[c],after.values[c],c);
  }
};
}
