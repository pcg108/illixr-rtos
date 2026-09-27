#pragma once
#include "pose_prediction.hpp"
#include "replay.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#ifndef ILLIXR_RENDER_DELAY_NS
#define ILLIXR_RENDER_DELAY_NS 6944445LL
#endif
#ifndef ILLIXR_DISPLAY_HZ
#define ILLIXR_DISPLAY_HZ 120
#endif
#ifndef ILLIXR_TIMEWARP_DELAY_NS
#define ILLIXR_TIMEWARP_DELAY_NS 1000000LL
#endif
#ifndef ILLIXR_RENDER_OFFSET_NS
#define ILLIXR_RENDER_OFFSET_NS 1000000LL
#endif
#ifndef ILLIXR_TIMEWARP_MARGIN_NS
#define ILLIXR_TIMEWARP_MARGIN_NS 1000000LL
#endif
namespace ILLIXR::gpu_pipeline {
inline constexpr unsigned trace_version = 2;
static_assert(ILLIXR_DISPLAY_HZ > 0 && ILLIXR_DISPLAY_HZ <= 1000000000LL);
inline constexpr int64_t period_ns = 1000000000LL / ILLIXR_DISPLAY_HZ;
inline constexpr int64_t render_delay_ns = ILLIXR_RENDER_DELAY_NS;
inline constexpr int64_t timewarp_delay_ns = ILLIXR_TIMEWARP_DELAY_NS;
inline constexpr int64_t render_offset_ns = ILLIXR_RENDER_OFFSET_NS;
inline constexpr int64_t timewarp_margin_ns = ILLIXR_TIMEWARP_MARGIN_NS;
inline constexpr int64_t timewarp_lead_ns = timewarp_delay_ns + timewarp_margin_ns;
static_assert(render_delay_ns > 0 && timewarp_delay_ns > 0);
static_assert(render_offset_ns >= 0 && render_offset_ns < period_ns);
static_assert(timewarp_margin_ns >= 0 && timewarp_lead_ns < period_ns);
inline constexpr int64_t vsync(uint64_t slot) { return slot * period_ns; }
inline constexpr int64_t render_wake(uint64_t slot) { return vsync(slot) + render_offset_ns; }
inline constexpr int64_t warp_wake(uint64_t slot) { return vsync(slot) - timewarp_lead_ns; }
// Strictly future opportunities: completion/wakeup lateness cannot shift phase.
inline constexpr uint64_t future_render_slot(int64_t now) {
  return now < render_offset_ns ? 0 : (now - render_offset_ns) / period_ns + 1;
}
inline constexpr uint64_t future_warp_slot(int64_t now) { return (now + timewarp_lead_ns) / period_ns + 1; }
inline constexpr int64_t boundary_at_or_after(int64_t now) { return ((now + period_ns - 1) / period_ns) * period_ns; }
inline constexpr unsigned image_width = 64, image_height = 64, eye_count = 2;
using StereoImage = std::array<std::array<uint8_t, image_width * image_height * 4>, eye_count>;
inline StereoImage static_image{};
struct Frame {
  uint64_t frame_id{}, slot{};
  const StereoImage *image{};
  PredictionResult prediction{};
  int64_t scheduled_wake_ns{}, actual_wake_ns{}, presentation_ns{}, submit_ns{};
  int64_t scheduled_complete_ns{}, observed_complete_ns{}, publication_ns{};
  unsigned processing_hart{}, publication_hart{};
};
struct Completion {
  Frame frame{};
  PredictionResult prediction{};
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  uint64_t warp_id{}, display_slot{};
  int64_t scheduled_wake_ns{}, actual_wake_ns{}, selection_ns{}, submit_ns{};
  int64_t scheduled_complete_ns{}, observed_complete_ns{}, publication_ns{};
  unsigned processing_hart{}, publication_hart{};
  bool reused{}, final{};
};
enum class Opportunity : unsigned { Submitted, Empty, Missed };
struct WarpSlot { uint64_t first{}, last{}; int64_t observed_ns{}; Opportunity outcome{}; bool final{}; };
struct Display {
  uint64_t slot{}, warp_id{}, frame_id{};
  int64_t observed_ns{}, publication_ns{};
  unsigned hart{};
  bool new_output{}, fresh_on_time{};
};
inline constexpr std::size_t trace_capacity = 8192;
inline Frame render_trace[trace_capacity];
inline Completion warp_trace[trace_capacity];
inline WarpSlot opportunity_trace[trace_capacity];
inline Display display_trace[trace_capacity];
inline std::size_t render_trace_size{}, warp_trace_size{}, opportunity_trace_size{}, display_trace_size{};
// Frame snapshot and closure share a lock. Completion publication and the
// observer share another. No GPU wait or prediction runs with either held.
inline k_mutex frame_mutex, completion_mutex;
inline Frame retained_frame;
inline bool frame_available{};
inline atomic_t render_closed{}, timewarp_done{};
inline int64_t render_closed_ns{}, timewarp_done_ns{};
inline uint64_t render_submitted{}, render_completed{}, render_skipped_slots{}, render_next_slot{};
inline uint64_t timewarp_submitted{}, timewarp_completed{}, distinct_selected{}, repeated_uses{};
inline uint64_t empty_opportunities{}, missed_opportunities{}, last_selected_frame{};
inline uint64_t render_deadlines_missed{}, timewarp_deadlines_missed{}, fresh_warp_completed{};
inline uint64_t new_outputs{}, repeated_outputs{}, no_outputs{}, fresh_on_time_presentations{};
inline atomic_t trace_overflow{};
inline std::size_t presentation_cursor{};
inline uint64_t displayed_warp{}, next_display_slot{1};
inline void initialize() {
  k_mutex_init(&frame_mutex);
  k_mutex_init(&completion_mutex);
  // Immutable once worker startup barriers are released.
  for (unsigned eye = 0; eye < eye_count; ++eye)
    for (unsigned y = 0; y < image_height; ++y)
      for (unsigned x = 0; x < image_width; ++x) {
        auto &image = static_image[eye];
        const unsigned i = 4 * (y * image_width + x);
        image[i] = static_cast<uint8_t>(255 * x / (image_width - 1));
        image[i + 1] = static_cast<uint8_t>(255 * y / (image_height - 1));
        image[i + 2] = static_cast<uint8_t>(((x / 8 + y / 8) & 1) ? 224 : 32);
        image[i + 3] = 255;
        if (x < 8 && y < 8) {
          image[i] = eye == 0 ? 255 : 0;
          image[i + 1] = eye == 1 ? 255 : 0;
          image[i + 2] = 0;
        }
      }
}
inline bool sources_done() {
  return atomic_get(&replay::imu_done) && atomic_get(&replay::cam_done) &&
         atomic_get(&replay::vio_done) && atomic_get(&replay::integrator_done);
}

// This models elapsed GPU time. It releases the CPU and never advances the
// shared clock or busy-waits on an instruction/cycle counter.
inline bool sleep_until(int64_t deadline_ns) {
  auto &clock = get_global_relative_clock();
  while (!replay::failed()) {
    const int64_t now = clock.now_ns();
    if (now >= deadline_ns)
      return true;
    const int64_t wake = (deadline_ns - now > 10000000LL) ? now + 10000000LL : deadline_ns;
    k_sleep(K_TIMEOUT_ABS_TICKS(clock.absolute_ticks(wake)));
  }
  return false;
}

inline int64_t next_display_boundary(int64_t runtime_ns) {
  return (runtime_ns / period_ns + 1) * period_ns;
}


inline void publish_frame(Frame &frame, unsigned hart) {
  k_mutex_lock(&frame_mutex, K_FOREVER);
  frame.publication_hart = hart;
  frame.publication_ns = get_global_relative_clock().now_ns();
  retained_frame = frame;
  frame_available = true;
  k_mutex_unlock(&frame_mutex);
}
// Copy ownership, not image ownership: the static dummy pixels never change.
inline bool snapshot_frame(Completion &completion) {
  k_mutex_lock(&frame_mutex, K_FOREVER);
  completion.selection_ns = get_global_relative_clock().now_ns();
  completion.final = atomic_get(&render_closed);
  if (frame_available) completion.frame = retained_frame;
  const bool available = frame_available;
  k_mutex_unlock(&frame_mutex);
  return available;
}
inline void close_render() {
  k_mutex_lock(&frame_mutex, K_FOREVER);
  render_closed_ns = get_global_relative_clock().now_ns();
  atomic_set(&render_closed, 1);
  k_mutex_unlock(&frame_mutex);
}
inline void finish_timewarp() {
  k_mutex_lock(&completion_mutex, K_FOREVER);
  if (!atomic_get(&timewarp_done)) {
    timewarp_done_ns = get_global_relative_clock().now_ns();
    atomic_set(&timewarp_done, 1);
  }
  k_mutex_unlock(&completion_mutex);
}
inline void overflow() { atomic_add(&trace_overflow, 1); replay::fail("GPU trace capacity exceeded"); }
inline void record_render(const Frame &frame) {
  ++render_completed;
  render_deadlines_missed += frame.publication_ns > frame.presentation_ns;
  if (render_trace_size < trace_capacity) render_trace[render_trace_size++] = frame;
  else overflow();
}
inline void record_opportunity(uint64_t first, uint64_t last, int64_t observed, Opportunity outcome, bool final = false) {
  if (last < first) return;
  if (outcome == Opportunity::Empty) empty_opportunities += last - first + 1;
  if (outcome == Opportunity::Missed) missed_opportunities += last - first + 1;
  if (opportunity_trace_size < trace_capacity)
    opportunity_trace[opportunity_trace_size++] = {first, last, observed, outcome, final};
  else overflow();
}
inline void publish_warp(Completion &completion, unsigned hart) {
  completion.reused = completion.frame.frame_id == last_selected_frame;
  if (completion.reused) ++repeated_uses; else ++distinct_selected;
  last_selected_frame = completion.frame.frame_id;
  k_mutex_lock(&completion_mutex, K_FOREVER);
  completion.publication_hart = hart;
  completion.publication_ns = get_global_relative_clock().now_ns();
  ++timewarp_completed;
  timewarp_deadlines_missed += completion.publication_ns > vsync(completion.display_slot);
  fresh_warp_completed += completion.frame.prediction.valid() && completion.prediction.valid();
  if (warp_trace_size < trace_capacity) warp_trace[warp_trace_size++] = completion;
  else overflow();
  if (completion.final) { timewarp_done_ns = completion.publication_ns; atomic_set(&timewarp_done, 1); }
  k_mutex_unlock(&completion_mutex);
}
// Caller holds completion_mutex. Timestamp filtering is essential: an observer
// running late must never retroactively present an output published in its future.
inline void observe_display_locked(int64_t now, unsigned hart) {
  int64_t end = now;
  if (atomic_get(&timewarp_done)) {
    const int64_t final = boundary_at_or_after(warp_trace_size ? warp_trace[warp_trace_size-1].publication_ns : timewarp_done_ns);
    if (end > final) end = final;
  }
  while (vsync(next_display_slot) <= end && !replay::failed()) {
    if (display_trace_size == trace_capacity) { overflow(); break; }
    const int64_t boundary = vsync(next_display_slot);
    while (presentation_cursor < warp_trace_size && warp_trace[presentation_cursor].publication_ns <= boundary)
      ++presentation_cursor;
    const Completion *selected = presentation_cursor ? &warp_trace[presentation_cursor-1] : nullptr;
    Display d;
    d.slot = next_display_slot++;
    d.observed_ns = now;
    d.hart = hart;
    if (selected) {
      d.warp_id = selected->warp_id; d.frame_id = selected->frame.frame_id;
      d.publication_ns = selected->publication_ns;
      d.new_output = displayed_warp != d.warp_id;
      d.fresh_on_time = d.new_output && selected->display_slot == d.slot &&
                       selected->prediction.valid() && selected->frame.prediction.valid();
      if (d.new_output) ++new_outputs; else ++repeated_outputs;
      fresh_on_time_presentations += d.fresh_on_time;
      displayed_warp = d.warp_id;
    } else ++no_outputs;
    display_trace[display_trace_size++] = d;
  }
}
inline void observe_display(unsigned hart) {
  k_mutex_lock(&completion_mutex, K_FOREVER);
  observe_display_locked(get_global_relative_clock().now_ns(), hart);
  k_mutex_unlock(&completion_mutex);
}
// Invoked only after worker joins. Normal EOS has one final closed-snapshot
// opportunity (possibly empty), so this wait is bounded by one display period.
inline void finish_presentation(unsigned hart) {
  const int64_t end = boundary_at_or_after(warp_trace_size ? warp_trace[warp_trace_size-1].publication_ns : timewarp_done_ns);
  if (sleep_until(end)) observe_display(hart);
}
inline const char *status_name(PredictionStatus status) {
  switch (status) {
  case PredictionStatus::Valid: return "valid";
  case PredictionStatus::Fallback: return "fallback";
  case PredictionStatus::Stale: return "stale";
  case PredictionStatus::Invalid: return "invalid";
  }
  return "invalid";
}

inline void print_prediction(const PredictionResult &prediction) {
  printf("\"target_ns\":%lld,\"source_ns\":%lld,\"source_sequence\":%llu,"
         "\"prediction_status\":\"%s\",\"prediction_horizon_ns\":%lld,"
         "\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g]",
         (long long)prediction.target_timestamp_ns, (long long)prediction.source_timestamp_ns,
         (unsigned long long)prediction.source_sequence, status_name(prediction.status),
         (long long)prediction.horizon_ns,
         (double)prediction.position.x(), (double)prediction.position.y(), (double)prediction.position.z(),
         (double)prediction.orientation.w(), (double)prediction.orientation.x(),
         (double)prediction.orientation.y(), (double)prediction.orientation.z());
}


inline void print_schedule(int64_t scheduled, int64_t actual, int64_t publication, unsigned processing, unsigned publishing) {
  printf("\"scheduled_wake_ns\":%lld,\"actual_wake_ns\":%lld,\"publication_ns\":%lld,\"processing_hart\":%u,\"publication_hart\":%u,",
         (long long)scheduled, (long long)actual, (long long)publication, processing, publishing);
}
inline void dump() {
  for (std::size_t i=0; i<render_trace_size; ++i) {
    const auto &f = render_trace[i];
    printf("ILLIXR_GPU_EVENT {\"version\":2,\"stage\":\"render\",\"frame_id\":%llu,\"slot\":%llu,"
           "\"submit_ns\":%lld,\"scheduled_complete_ns\":%lld,\"observed_complete_ns\":%lld,\"presentation_ns\":%lld,\"hart\":%u,",
           (unsigned long long)f.frame_id,(unsigned long long)f.slot,(long long)f.submit_ns,
           (long long)f.scheduled_complete_ns,(long long)f.observed_complete_ns,(long long)f.presentation_ns,f.publication_hart);
    print_schedule(f.scheduled_wake_ns,f.actual_wake_ns,f.publication_ns,f.processing_hart,f.publication_hart);
    print_prediction(f.prediction); printf("}\n");
  }
  for (std::size_t i=0; i<warp_trace_size; ++i) {
    const auto &c=warp_trace[i]; const auto &f=c.frame;
    printf("ILLIXR_GPU_EVENT {\"version\":2,\"stage\":\"timewarp\",\"frame_id\":%llu,\"slot\":%llu,\"warp_id\":%llu,\"display_slot\":%llu,"
           "\"submit_ns\":%lld,\"scheduled_complete_ns\":%lld,\"observed_complete_ns\":%lld,\"presentation_ns\":%lld,\"hart\":%u,"
           "\"selection_ns\":%lld,\"frame_publication_ns\":%lld,\"frame_age_ns\":%lld,\"reused\":%s,\"final\":%s,"
           "\"render_source_sequence\":%llu,\"render_prediction_status\":\"%s\",\"render_orientation\":[%.9g,%.9g,%.9g,%.9g],",
           (unsigned long long)f.frame_id,(unsigned long long)f.slot,(unsigned long long)c.warp_id,(unsigned long long)c.display_slot,
           (long long)c.submit_ns,(long long)c.scheduled_complete_ns,(long long)c.observed_complete_ns,(long long)f.presentation_ns,c.publication_hart,
           (long long)c.selection_ns,(long long)f.publication_ns,(long long)(c.selection_ns-f.publication_ns),c.reused?"true":"false",c.final?"true":"false",
           (unsigned long long)f.prediction.source_sequence,status_name(f.prediction.status),(double)f.prediction.orientation.w(),
           (double)f.prediction.orientation.x(),(double)f.prediction.orientation.y(),(double)f.prediction.orientation.z());
    print_schedule(c.scheduled_wake_ns,c.actual_wake_ns,c.publication_ns,c.processing_hart,c.publication_hart);
    print_prediction(c.prediction); printf(",\"transform\":[");
    for(int row=0;row<4;++row) for(int col=0;col<4;++col) printf("%s%.9g",row||col?",":"",(double)c.transform(row,col));
    printf("]}\n");
  }
  for (std::size_t i=0;i<opportunity_trace_size;++i) {
    const auto &o=opportunity_trace[i];
    printf("ILLIXR_WARP_SLOT {\"version\":2,\"first_slot\":%llu,\"last_slot\":%llu,\"scheduled_wake_ns\":%lld,\"observed_ns\":%lld,\"outcome\":\"%s\",\"final\":%s}\n",
           (unsigned long long)o.first,(unsigned long long)o.last,(long long)warp_wake(o.first),(long long)o.observed_ns,
           o.outcome==Opportunity::Submitted?"submitted":o.outcome==Opportunity::Empty?"empty":"missed",o.final?"true":"false");
  }
  for (std::size_t i=0;i<display_trace_size;++i) {
    const auto &d=display_trace[i];
    printf("ILLIXR_DISPLAY {\"version\":2,\"display_slot\":%llu,\"boundary_ns\":%lld,\"observed_ns\":%lld,\"observer_lateness_ns\":%lld,"
           "\"warp_id\":%llu,\"frame_id\":%llu,\"publication_ns\":%lld,\"outcome\":\"%s\",\"fresh_on_time\":%s,\"hart\":%u}\n",
           (unsigned long long)d.slot,(long long)vsync(d.slot),(long long)d.observed_ns,(long long)(d.observed_ns-vsync(d.slot)),
           (unsigned long long)d.warp_id,(unsigned long long)d.frame_id,(long long)d.publication_ns,
           !d.warp_id?"none":d.new_output?"new":"repeated",d.fresh_on_time?"true":"false",d.hart);
  }
  printf("ILLIXR_GPU_RESULT {\"version\":2");
#define GPU_COUNT(name) printf(",\"" #name "\":%llu", (unsigned long long)name)
  GPU_COUNT(render_submitted); GPU_COUNT(render_completed); GPU_COUNT(render_skipped_slots); GPU_COUNT(render_next_slot);
  GPU_COUNT(timewarp_submitted); GPU_COUNT(timewarp_completed); GPU_COUNT(distinct_selected); GPU_COUNT(repeated_uses);
  GPU_COUNT(empty_opportunities); GPU_COUNT(missed_opportunities);
  GPU_COUNT(render_deadlines_missed); GPU_COUNT(timewarp_deadlines_missed); GPU_COUNT(fresh_warp_completed);
  GPU_COUNT(new_outputs); GPU_COUNT(repeated_outputs); GPU_COUNT(no_outputs); GPU_COUNT(fresh_on_time_presentations);
  GPU_COUNT(trace_overflow); GPU_COUNT(render_closed_ns); GPU_COUNT(timewarp_done_ns);
  GPU_COUNT(render_delay_ns); GPU_COUNT(timewarp_delay_ns); GPU_COUNT(period_ns); GPU_COUNT(render_offset_ns); GPU_COUNT(timewarp_margin_ns);
#undef GPU_COUNT
  printf(",\"never_selected\":%llu,\"display_slots\":%llu,\"render_closed\":%s,\"timewarp_done\":%s}\n",
         (unsigned long long)(render_completed-distinct_selected),(unsigned long long)display_trace_size,
         atomic_get(&render_closed)?"true":"false",atomic_get(&timewarp_done)?"true":"false");
}
inline void validate() {
  if (!atomic_get(&render_closed) || !atomic_get(&timewarp_done)) replay::fail("GPU pipeline did not shut down");
  if (render_submitted!=render_completed || timewarp_submitted!=timewarp_completed ||
      render_next_slot!=render_completed+render_skipped_slots ||
      timewarp_completed!=distinct_selected+repeated_uses || distinct_selected>render_completed ||
      display_trace_size!=new_outputs+repeated_outputs+no_outputs) replay::fail("GPU accounting mismatch");
  if (!fresh_warp_completed || !fresh_on_time_presentations) replay::fail("no fresh on-time modeled presentation");
}
} // namespace ILLIXR::gpu_pipeline
