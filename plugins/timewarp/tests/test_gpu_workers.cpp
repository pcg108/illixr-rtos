#include "../../../src/gpu_pipeline.hpp"
#include "../transform.hpp"
#include "desktop_transform_reference.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

namespace ILLIXR {
RelativeClock &get_global_relative_clock() {
  static RelativeClock clock;
  return clock;
}
}
namespace gpu = ILLIXR::gpu_pipeline;

int main() {
  gpu::initialize();
  auto &clock = ILLIXR::get_global_relative_clock();
  clock.start();
  clock.set_dataset_origin(1403636579763555584LL);
  assert(gpu::static_image[0][0] == 255 && gpu::static_image[0][1] == 0);
  assert(gpu::static_image[1][0] == 0 && gpu::static_image[1][1] == 255);
  assert(gpu::next_display_boundary(0) == gpu::period_ns);
  assert(gpu::next_display_boundary(gpu::period_ns) == 2 * gpu::period_ns);

  // Absolute schedules and strictly future advancement, including GPU overruns.
  const auto P = gpu::period_ns;
  assert(gpu::render_wake(0) == 1000000);
  assert(gpu::warp_wake(1) == P - 2000000);
  for (uint64_t slot=1; slot<1000; ++slot) {
    assert(gpu::render_wake(slot)-gpu::render_wake(slot-1) == P);
    assert(gpu::warp_wake(slot+1)-gpu::warp_wake(slot) == P);
    assert(gpu::future_render_slot(gpu::render_wake(slot)) == slot+1);
    assert(gpu::future_warp_slot(gpu::warp_wake(slot)) == slot+1);
    const auto late = gpu::vsync(slot)+P/2;
    assert(gpu::warp_wake(gpu::future_warp_slot(late)) > late);
    assert(gpu::render_wake(gpu::future_render_slot(late)) > late);
  }
  // Startup is empty. A copied render pose stays immutable across replacement,
  // and repeated snapshots do not consume the retained latest image.
  gpu::Completion received;
  assert(!gpu::snapshot_frame(received));
  gpu::Frame first, second;
  first.frame_id=1; first.prediction.source_sequence=10;
  second.frame_id=2; second.prediction.source_sequence=20;
  gpu::publish_frame(first, 0);
  assert(gpu::snapshot_frame(received) && received.frame.frame_id==1);
  gpu::publish_frame(second, 1);
  assert(received.frame.frame_id==1 && received.frame.prediction.source_sequence==10);
  assert(gpu::snapshot_frame(received) && received.frame.frame_id==2);
  assert(gpu::snapshot_frame(received) && received.frame.frame_id==2);
  gpu::close_render();
  assert(gpu::snapshot_frame(received) && received.final && received.frame.frame_id==2);

  // Concurrent replacement cannot tear the saved pose/descriptor. Equal IDs
  // are expected when the independently scheduled consumer reuses an image.
  atomic_set(&gpu::render_closed, 0);
  gpu::frame_available=false;
  uint64_t consumed=0,last_received=0;
  constexpr uint64_t produced=10000;
  std::thread consumer{[&] {
    for (;;) {
      gpu::Completion c;
      if (gpu::snapshot_frame(c)) {
        assert(c.frame.frame_id>=last_received);
        assert(c.frame.prediction.source_sequence==c.frame.frame_id*10);
        last_received=c.frame.frame_id; ++consumed;
      }
      if (c.final) break;
      std::this_thread::yield();
    }
  }};
  for(uint64_t i=1;i<=produced;++i) {
    gpu::Frame f; f.frame_id=i; f.prediction.source_sequence=i*10;
    gpu::publish_frame(f,0);
  }
  gpu::close_render(); consumer.join();
  assert(consumed>0 && last_received==produced);

  // Delayed observation reconstructs each nominal boundary from publication
  // history. Finishing the GPU before vsync is insufficient if publication was
  // late. The original target remains slot 2 in that late record.
  gpu::warp_trace_size=2;
  auto &w1=gpu::warp_trace[0]; auto &w2=gpu::warp_trace[1];
  w1.warp_id=1; w1.frame.frame_id=1; w1.display_slot=2;
  w1.publication_ns=2*P+1; w1.observed_complete_ns=2*P-1;
  w2.warp_id=2; w2.frame.frame_id=1; w2.display_slot=4;
  w2.publication_ns=4*P; w2.frame.prediction.status=ILLIXR::PredictionStatus::Valid;
  w2.prediction.status=ILLIXR::PredictionStatus::Valid;
  gpu::observe_display_locked(5*P,0);
  assert(gpu::display_trace_size==5);
  assert(gpu::display_trace[0].warp_id==0 && gpu::display_trace[1].warp_id==0);
  assert(gpu::display_trace[2].warp_id==1 && !gpu::display_trace[2].fresh_on_time);
  assert(gpu::display_trace[3].warp_id==2 && gpu::display_trace[3].fresh_on_time);
  assert(gpu::display_trace[4].warp_id==2 && !gpu::display_trace[4].new_output);
  assert(gpu::new_outputs==2 && gpu::repeated_outputs==1 && gpu::no_outputs==2);
  // Final completion caps the modeled display timeline, even for a late observer.
  gpu::display_trace_size=0; gpu::next_display_slot=1;
  gpu::presentation_cursor=0; gpu::displayed_warp=0;
  atomic_set(&gpu::timewarp_done,1);
  gpu::observe_display_locked(10*P,0);
  assert(gpu::display_trace_size==4 && gpu::display_trace[3].warp_id==2);
  // No-frame EOS cannot wait indefinitely and still accounts for startup slots.
  gpu::warp_trace_size=0; gpu::display_trace_size=0; gpu::next_display_slot=1;
  gpu::presentation_cursor=0; gpu::displayed_warp=0; gpu::timewarp_done_ns=P+1;
  gpu::observe_display_locked(10*P,0);
  assert(gpu::display_trace_size==2 && gpu::display_trace[1].warp_id==0);

  // The wait helper invokes a blocking sleep, allows independent progress, and
  // does not report completion before its target-clock deadline.
  std::atomic<bool> done{};
  std::atomic<unsigned> progress{};
  std::thread independent{[&] {
    while (!done) { ++progress; std::this_thread::yield(); }
  }};
  const auto deadline = clock.now_ns() + 20000000;
  assert(gpu::sleep_until(deadline));
  assert(clock.now_ns() >= deadline && progress > 0 && host_test::sleep_calls > 0);
  done = true;
  independent.join();

  // Failure interrupts a long GPU wait within the bounded polling interval.
  std::thread failure{[] {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ILLIXR::replay::fail("test cancellation");
  }};
  const auto failure_start = clock.now_ns();
  assert(!gpu::sleep_until(failure_start + 1000000000));
  failure.join();
  assert(clock.now_ns() - failure_start < 200000000);

  // The reference function is extracted directly from the pinned desktop
  // source by run_native_tests.py, not copied from the RTOS port.
  Eigen::Matrix4f projection;
  ILLIXR::math_util::projection_fov(&projection, 45, 45, 45, 45, 0.1f, 20.0f);
  Eigen::internal::set_is_malloc_allowed(false);
  float maximum_error = 0;
  for (unsigned i = 0; i < 512; ++i) {
    const float angle = static_cast<float>(i) * 0.017f;
    const Eigen::Vector3f axis = Eigen::Vector3f{1.0f, float(i % 11) + 1, float(i % 7) - 3}.normalized();
    const Eigen::Quaternionf render{Eigen::AngleAxisf{angle, axis}};
    const Eigen::Quaternionf latest{Eigen::AngleAxisf{-angle * 0.3f, Eigen::Vector3f::UnitY()}};
    Eigen::Matrix4f render_view = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f new_view = Eigen::Matrix4f::Identity();
    render_view.block<3, 3>(0, 0) = render.toRotationMatrix();
    new_view.block<3, 3>(0, 0) = latest.toRotationMatrix();
    Eigen::Matrix4f reference;
    desktop_reference::calculate_timewarp_transform(reference, projection, render_view, new_view);
    const auto actual = ILLIXR::timewarp_math::transform(render, latest);
    const float error = (actual - reference).cwiseAbs().maxCoeff();
    maximum_error = std::max(maximum_error, error);
    assert(actual.allFinite() && error <= 1e-5f);
    const auto zero_relative = ILLIXR::timewarp_math::transform(render, render);
    assert(std::abs(zero_relative(0, 0) - 0.5f) < 1e-5f);
    assert(std::abs(zero_relative(0, 2) + 0.5f) < 1e-5f);
    assert(std::abs(zero_relative(1, 2) + 0.5f) < 1e-5f);
    assert(std::abs(zero_relative(2, 2) + 1.0f) < 1e-5f);
  }
  Eigen::internal::set_is_malloc_allowed(true);
  std::printf("{\"passed\":true,\"transform_cases\":512,\"transform_max_error\":%.9g,"
              "\"snapshot_produced\":%llu,\"snapshot_reads\":%llu,"
              "\"blocking_sleep_calls\":%u}\n", maximum_error,
              (unsigned long long)produced, (unsigned long long)consumed,
              host_test::sleep_calls.load());
}
