#include "../../src/threadloop.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/eye_tracking.hpp"
#include "../../src/gpu_pipeline.hpp"
using namespace ILLIXR;
K_THREAD_STACK_DEFINE(offline_eye_stack,8192);
class OfflineEye final : public threadloop {
public:
 explicit OfflineEye(phonebook_new &pb):threadloop(pb,"offline_eye",offline_eye_stack,K_THREAD_STACK_SIZEOF(offline_eye_stack)) {}
protected:
 skip_option _p_should_skip() override {
  return atomic_get(&replay::imu_done)&&atomic_get(&replay::cam_done)?skip_option::stop:skip_option::run;
 }
 void _p_one_iteration() override {
  auto &clock=get_global_relative_clock();
  if(!gpu_pipeline::sleep_until(slot_*gpu_pipeline::period_ns)||should_terminate())return;
  if(atomic_get(&replay::imu_done)&&atomic_get(&replay::cam_done))return;
  const auto now=clock.now_ns();
  eye_tracking::publish({eye_tracking::sample(),++sequence_,int64_t(slot_*gpu_pipeline::period_ns),now,replay::hart_id()});
  slot_=now/gpu_pipeline::period_ns+1;
 }
 uint64_t slot_{},sequence_{};
};
void start_offline_eye(phonebook_new &pb) {static OfflineEye instance(pb);instance.start();}
REGISTER_PLUGIN(offline_eye);
