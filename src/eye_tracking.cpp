#include "hpm.hpp"
#include "eye_tracking.hpp"
#include "relative_clock.hpp"
#include "replay.hpp"
#include "trace_output.hpp"
#include "../third_party/ritnet/port/ritnet.h"
#include "../third_party/ritnet/reference/expected.h"
#include <zephyr/kernel.h>
#ifdef RITNET_DIAGNOSTICS
#include "../third_party/ritnet/port/diagnostics.h"
namespace ILLIXR::ritnet_diagnostics { void dump(); }
#endif
namespace ILLIXR::eye_tracking {
namespace {
constexpr unsigned capacity=4096;
Image publications[capacity];
struct Record { Result result; uint64_t cycles; };
struct Read { uint64_t warp_id,display_slot,inference_id; int64_t begin_ns,end_ns,age_ns; unsigned hart; bool reused; };
Record records[capacity];
Read reads[capacity];
unsigned publication_count{},record_count{},read_count{};
uint64_t previous_read{};
K_THREAD_STACK_DEFINE(stack,65536);
k_thread thread;
// Held only while copying a result, never during accelerator work.
K_MUTEX_DEFINE(result_lock);
K_SEM_DEFINE(image_ready,0,1);
Result latest_result;
bool started{};
atomic_t stopping{};
int64_t now() { return get_global_relative_clock().now_ns(); }
uint64_t cycle() { uint64_t c; asm volatile("rdcycle %0":"=r"(c));return c; }
void worker(void*,void*,void*) {
 uint64_t last_image{};
 for (;;) {
  k_sem_take(&image_ready,K_FOREVER);
  if(atomic_get(&stopping) || replay::failed()) break;
  Image input{};uint64_t sequence{};Result output{};
  output.snapshot_begin_ns=now();
  const bool available=latest_image.read(input,sequence);
  output.request_ns=now();
  if(!available || input.sequence==last_image) continue;
#ifdef RITNET_DIAGNOSTICS
  // Diagnostic observation is bounded; retain the final published result while
  // the rest of a full pipeline continues. Production has no inference limit.
  if(record_count>=ritnet_diag_inferences) continue;
#endif
  if(record_count>=capacity) { replay::fail("eye inference trace overflow");break; }
  last_image=input.sequence;
  output.image_sequence=input.sequence;output.image_ns=input.published_ns;
  output.inference_id=record_count+1;
  output.start_ns=now();output.accelerator_hart=replay::hart_id();
  if(output.accelerator_hart!=0) { replay::fail("RITNet executed outside hart zero");break; }
  ritnet_result result{};
  const auto begin=cycle();
  const int status=ritnet_infer(input.pixels,&result);
  const auto execution_cycles=cycle()-begin;
  output.completion_ns=now();
  output.valid=!status && result.valid;output.x=result.x;output.y=result.y;
  output.output_hash=result.output_hash;
  if(status) replay::fail("RITNet inference failed");
  if(output.output_hash!=RITNET_EXPECTED_HASH) {
#ifdef RITNET_DIAGNOSTICS
   rd_final_failure_snapshot();
#endif
   replay::fail("RITNet reference mismatch");
  }
  if(!output.valid) replay::fail("RITNet produced empty foreground");
  k_mutex_lock(&result_lock,K_FOREVER);
  output.publication_ns=now();output.publication_hart=replay::hart_id();
  latest_result=output;
  k_mutex_unlock(&result_lock);
  records[record_count++]={output,execution_cycles};
  // Publications during inference coalesce into one pending wake. The next
  // iteration snapshots the newest image, without building an image backlog.
 }
}
}
const int8_t *sample() { return ritnet_sample(); }
void initialize() {
 if(started) { replay::fail("duplicate eye tracking initialization");return; }
 auto tid=k_thread_create(&thread,stack,K_THREAD_STACK_SIZEOF(stack),worker,
                         nullptr,nullptr,nullptr,K_PRIO_PREEMPT(5),0,K_FOREVER);
 if(!tid) { replay::fail("RITNet thread creation failed");return; }
#ifdef CONFIG_SCHED_CPU_MASK
 if(k_thread_cpu_pin(tid,0)) { replay::fail("RITNet affinity failed");k_thread_abort(tid);return; }
#else
 static_assert(CONFIG_MP_MAX_NUM_CPUS==1,"RITNet requires CPU affinity on SMP");
#endif
 hpm::register_thread(tid,hpm::Owner::EyeTracking);
 k_thread_name_set(tid,"ritnet");started=true;k_thread_start(tid);
}
void publish(Image image) {
 if(publication_count>=capacity) { replay::fail("eye publication trace overflow");return; }
 publications[publication_count++]=image;
 latest_image.publish(image);
 k_sem_give(&image_ready);
}
Result latest() {
 k_mutex_lock(&result_lock,K_FOREVER);
 const auto result=latest_result;
 k_mutex_unlock(&result_lock);
 return result;
}
Result read_latest(uint64_t warp_id,uint64_t display_slot) {
 const auto begin=now();
 const auto result=latest();
 const auto end=now();
 // Only the timewarp worker writes these records.
 if(read_count>=capacity) { replay::fail("eye read trace overflow");return result; }
 reads[read_count++]={warp_id,display_slot,result.inference_id,begin,end,
                     result.inference_id?end-result.publication_ns:0,replay::hart_id(),
                     result.inference_id!=0 && result.inference_id==previous_read};
 previous_read=result.inference_id;
 return result;
}
void shutdown() {
 if(started) {
  atomic_set(&stopping,1);k_sem_give(&image_ready);
  // Finish and publish any in-flight inference; no new work starts afterward.
  k_thread_join(&thread,K_FOREVER);started=false;
 }
}
void dump() {
#ifdef RITNET_DIAGNOSTICS
 ritnet_diagnostics::dump();
 const bool diagnostics = true;
 const unsigned inference_limit = ritnet_diag_inferences;
#else
 const bool diagnostics = false;
 const unsigned inference_limit = 0; // No diagnostic cap; normal end-of-stream controls shutdown.
#endif
 trace_output::print("ILLIXR_EYE_CONFIG {\"version\":2,\"enabled\":true,\"hz\":120,\"width\":240,\"height\":160,\"precision\":\"int8\",\"opcode\":2,\"accelerator_hart\":0,\"synchronous\":false,\"completion_fence_policy\":\"every_operation\",\"completion_fences_per_inference\":64,\"diagnostics\":%s,\"inference_limit\":%u,\"workspace_bytes\":%zu,\"publications\":%u,\"requests\":%u,\"reads\":%u}\n",diagnostics?"true":"false",inference_limit,ritnet_workspace_size(),publication_count,record_count,read_count);
 for(unsigned i=0;i<publication_count;i++) {
  const auto &p=publications[i];
  trace_output::print("ILLIXR_EYE_IMAGE {\"sequence\":%llu,\"scheduled_ns\":%lld,\"published_ns\":%lld,\"hart\":%u}\n",(unsigned long long)p.sequence,(long long)p.scheduled_ns,(long long)p.published_ns,p.publication_hart);
 }
 for(unsigned i=0;i<record_count;i++) {
  const auto &r=records[i];const auto &s=r.result;
  trace_output::print("ILLIXR_EYE_RESULT {\"inference_id\":%llu,\"image_sequence\":%llu,\"image_ns\":%lld,\"snapshot_begin_ns\":%lld,\"request_ns\":%lld,\"start_ns\":%lld,\"completion_ns\":%lld,\"publication_ns\":%lld,\"valid\":%s,\"x\":%.17g,\"y\":%.17g,\"output_hash\":%llu,\"accelerator_hart\":%u,\"publication_hart\":%u,\"cycles\":%llu}\n",
   (unsigned long long)s.inference_id,(unsigned long long)s.image_sequence,(long long)s.image_ns,(long long)s.snapshot_begin_ns,(long long)s.request_ns,(long long)s.start_ns,(long long)s.completion_ns,(long long)s.publication_ns,s.valid?"true":"false",s.x,s.y,(unsigned long long)s.output_hash,s.accelerator_hart,s.publication_hart,(unsigned long long)r.cycles);
 }
 for(unsigned i=0;i<read_count;i++) {
  const auto &s=reads[i];
  trace_output::print("ILLIXR_EYE_READ {\"warp_id\":%llu,\"display_slot\":%llu,\"inference_id\":%llu,\"begin_ns\":%lld,\"end_ns\":%lld,\"age_ns\":%lld,\"hart\":%u,\"reused\":%s}\n",(unsigned long long)s.warp_id,(unsigned long long)s.display_slot,(unsigned long long)s.inference_id,(long long)s.begin_ns,(long long)s.end_ns,(long long)s.age_ns,s.hart,s.reused?"true":"false");
 }
}
}
