#include "../../src/gpu_pipeline.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/threadloop.hpp"

using namespace ILLIXR;
namespace gpu = ILLIXR::gpu_pipeline;

K_THREAD_STACK_DEFINE(render_loop_stack, 65536);

class RenderLoop final : public threadloop {
public:
  explicit RenderLoop(phonebook_new &pb)
      : threadloop{pb, "render_loop", render_loop_stack,
                   K_THREAD_STACK_SIZEOF(render_loop_stack), 5,
                   replay::requested_hart(replay::RENDER_WORKER)} {}

protected:
  skip_option _p_should_skip() override {
    return gpu::sources_done() ? skip_option::stop : skip_option::run;
  }

  void _p_one_iteration() override {
    auto &clock = get_global_relative_clock();
    // After an overrun, wait for a future offset; never burst to catch up.
    const auto before = clock.now_ns();
    if (next_slot_ && before >= gpu::render_wake(next_slot_)) {
      const auto future = gpu::future_render_slot(before);
      gpu::render_skipped_slots += future - next_slot_;
      next_slot_ = future;
    }
    const auto scheduled = gpu::render_wake(next_slot_);
    if (!gpu::sleep_until(scheduled) || should_terminate() || gpu::sources_done()) return;
    const auto actual = clock.now_ns();
    if (actual >= gpu::vsync(next_slot_ + 1)) {
      const auto future = gpu::future_render_slot(actual);
      gpu::render_skipped_slots += future - next_slot_;
      next_slot_ = future;
      return;
    }
    gpu::Frame frame;
    frame.frame_id = ++gpu::render_submitted;
    frame.slot = next_slot_++;
    frame.image = &gpu::static_image;
    frame.scheduled_wake_ns = scheduled;
    frame.actual_wake_ns = actual;
    frame.processing_hart = replay::hart_id();
    frame.presentation_ns = gpu::vsync(next_slot_);
    replay::record_placement(replay::RENDER_WORKER);
    frame.prediction = get_pose_prediction().predict(
        PredictionConsumer::Render, clock.dataset_origin_ns() + frame.presentation_ns);
    frame.submit_ns = clock.now_ns();
    frame.scheduled_complete_ns = frame.submit_ns + gpu::render_delay_ns;
    if (!gpu::sleep_until(frame.scheduled_complete_ns) || should_terminate())
      return;
    frame.observed_complete_ns = clock.now_ns();
    gpu::publish_frame(frame, replay::hart_id());
    gpu::record_render(frame);
    replay::record_placement(replay::RENDER_WORKER, true);
  }

  void _p_thread_teardown() override { gpu::close_render(); }

private:
  uint64_t next_slot_{};
};

void start_render_loop(phonebook_new &pb) {
  static RenderLoop instance{pb};
  instance.start();
}
REGISTER_PLUGIN(render_loop);
