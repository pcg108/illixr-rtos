#include "../../src/gpu_pipeline.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/threadloop.hpp"
#include "transform.hpp"

using namespace ILLIXR;
namespace gpu = ILLIXR::gpu_pipeline;

K_THREAD_STACK_DEFINE(timewarp_stack, 65536);

class Timewarp final : public threadloop {
public:
  explicit Timewarp(phonebook_new &pb)
      : threadloop{pb, "timewarp", timewarp_stack,
                   K_THREAD_STACK_SIZEOF(timewarp_stack), 5,
                   replay::requested_hart(replay::TIMEWARP_WORKER)} {}

protected:
  skip_option _p_should_skip() override {
    return finished_ ? skip_option::stop : skip_option::run;
  }

  void _p_one_iteration() override {
    auto &clock = get_global_relative_clock();
    const auto before = clock.now_ns();
    if (next_slot_ > 1 && before >= gpu::warp_wake(next_slot_)) {
      const auto future = gpu::future_warp_slot(before);
      gpu::record_opportunity(next_slot_, future-1, before, gpu::Opportunity::Missed);
      next_slot_ = future;
    }
    gpu::Completion completion;
    completion.display_slot = next_slot_;
    completion.scheduled_wake_ns = gpu::warp_wake(next_slot_);
    if (!gpu::sleep_until(completion.scheduled_wake_ns) || should_terminate()) return;
    completion.actual_wake_ns = clock.now_ns();
    if (completion.actual_wake_ns >= gpu::vsync(next_slot_)) {
      const auto future = gpu::future_warp_slot(completion.actual_wake_ns);
      gpu::record_opportunity(next_slot_, future-1, completion.actual_wake_ns, gpu::Opportunity::Missed);
      next_slot_ = future;
      return;
    }
    ++next_slot_;
    if (!gpu::snapshot_frame(completion)) {
      gpu::record_opportunity(completion.display_slot, completion.display_slot,
                              completion.actual_wake_ns, gpu::Opportunity::Empty, completion.final);
      finished_ = completion.final;
      return;
    }
    completion.warp_id = ++gpu::timewarp_submitted;
    completion.processing_hart = replay::hart_id();
    gpu::record_opportunity(completion.display_slot, completion.display_slot,
                            completion.actual_wake_ns, gpu::Opportunity::Submitted, completion.final);
    replay::record_placement(replay::TIMEWARP_WORKER);
    completion.prediction = get_pose_prediction().predict(
        PredictionConsumer::Timewarp, clock.dataset_origin_ns() + gpu::vsync(completion.display_slot));
    completion.transform = timewarp_math::transform(
        completion.frame.prediction.orientation, completion.prediction.orientation);
    if (!completion.transform.allFinite()) {
      replay::fail("nonfinite timewarp transform");
      return;
    }
    // The desktop start/end transforms are equal. GPU shader work is modeled
    // solely by elapsed time, and the immutable frame's pixels stay unchanged.
    completion.submit_ns = clock.now_ns();
    completion.scheduled_complete_ns = completion.submit_ns + gpu::timewarp_delay_ns;
    if (!gpu::sleep_until(completion.scheduled_complete_ns) || should_terminate())
      return;
    completion.observed_complete_ns = clock.now_ns();
    gpu::publish_warp(completion, replay::hart_id());
    finished_ = completion.final;
    replay::record_placement(replay::TIMEWARP_WORKER, true);
  }

  void _p_thread_teardown() override { gpu::finish_timewarp(); }
private:
  uint64_t next_slot_{1};
  bool finished_{};
};

void start_timewarp(phonebook_new &pb) {
  static Timewarp instance{pb};
  instance.start();
}
REGISTER_PLUGIN(timewarp);
