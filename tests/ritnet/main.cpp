#include "eye_tracking.hpp"
#include "blas_backend.hpp"
#include "gemmini_backend.hpp"
#include "clock_check.hpp"
#include "vector_check.hpp"
#include "trace_output.hpp"
#include "../../third_party/ritnet/port/ritnet.h"
#include "ritnet_reference.h"
#ifdef RITNET_DIAGNOSTICS
#include "../../third_party/ritnet/port/diagnostics.h"
#endif
#include <zephyr/logging/log_ctrl.h>
#include <cstring>
#include <cmath>
using namespace ILLIXR;
extern "C" volatile uint64_t tohost;
K_THREAD_STACK_DEFINE(heartbeat_stack,4096);
static k_thread heartbeat_thread;
static atomic_t heartbeat_count{},heartbeat_stop{};
static void heartbeat(void*,void*,void*) {
 while(!atomic_get(&heartbeat_stop)) { k_msleep(1); atomic_inc(&heartbeat_count); }
}
#ifdef ILLIXR_USE_GEMMINI_BLAS
static bool fp32() {
 // Exact binary fractions, signed inputs, and nontrivial beta distinguish
 // FP32 execution from an accidentally selected INT8 array.
 const int m=3,n=2,k=5,one=1;const char no='N';const float alpha=.5f,beta=.25f;
 float a[m*k],b[k*n],c[m*n],expected[m*n];
 for(int i=0;i<m*k;i++)a[i]=(i-7)*.125f;
 for(int i=0;i<k*n;i++)b[i]=(4-i)*.25f;
 for(int j=0;j<n;j++)for(int i=0;i<m;i++) {
  c[j*m+i]=.5f;float v=beta*.5f;
  for(int q=0;q<k;q++)v+=alpha*a[q*m+i]*b[j*k+q];expected[j*m+i]=v;
 }
 printf("RITNET_ROUTE_PHASE fp32_begin\n");
 sgemm_(&no,&no,&m,&n,&k,&alpha,a,&m,b,&k,&beta,c,&m);
 bool good=true;for(int i=0;i<m*n;i++)good&=c[i]==expected[i];
 float x[k],y[m],want[m];for(int q=0;q<k;q++)x[q]=(q-2)*.5f;
 for(int i=0;i<m;i++){y[i]=.25f;want[i]=beta*y[i];for(int q=0;q<k;q++)want[i]+=alpha*a[q*m+i]*x[q];}
 sgemv_(&no,&m,&k,&alpha,a,&m,x,&one,&beta,y,&one);
 for(int i=0;i<m;i++)good&=y[i]==want[i];
 printf("RITNET_ROUTE_PHASE fp32_end %s\n",good?"pass":"fail");return good;
}
K_THREAD_STACK_DEFINE(fp32_caller_stack,16384);
static k_thread fp32_caller;
static bool overlap_pass;
static void overlap(void*,void*,void*) { overlap_pass=fp32(); }
#else
static bool fp32(){return true;}
#endif
int main() {
 log_flush();replay::initialize();
 const auto harts=clock_check::run();clock_check::platform(harts);
 blas_backend::initialize();
#ifdef ILLIXR_RITNET_VECTOR_PREFLIGHT
 if(!vector_check::run())replay::fail("RITNet platform vector check failed");
#endif
 auto &clock=get_global_relative_clock();clock.set_dataset_origin(0);clock.start();
 eye_tracking::initialize();
 auto heartbeat_tid=k_thread_create(&heartbeat_thread,heartbeat_stack,K_THREAD_STACK_SIZEOF(heartbeat_stack),heartbeat,nullptr,nullptr,nullptr,K_PRIO_PREEMPT(4),0,K_NO_WAIT);
 bool good=!replay::failed()&&!eye_tracking::latest().valid&&fp32();
#ifdef RITNET_DIAGNOSTICS
 const unsigned iterations=ritnet_diag_inferences;
#else
 const unsigned iterations=2;
#endif
 for(unsigned i=0;i<iterations&&good;i++) {
  eye_tracking::publish({eye_tracking::sample(),i+1,clock.now_ns(),clock.now_ns(),replay::hart_id()});
  const auto ticks_before=atomic_get(&heartbeat_count);
  printf("RITNET_ROUTE_PHASE int8_begin %u\n",i);
#ifdef ILLIXR_USE_GEMMINI_BLAS
  k_tid_t concurrent=nullptr;
  if(i==1) concurrent=k_thread_create(&fp32_caller,fp32_caller_stack,K_THREAD_STACK_SIZEOF(fp32_caller_stack),overlap,nullptr,nullptr,nullptr,K_PRIO_PREEMPT(5),0,K_NO_WAIT);
#endif
  // Test-only polling: production timewarp performs exactly one snapshot.
  eye_tracking::Result result{};
  const auto timeout=clock.now_ns()+30000000000LL;
  do {
   result=eye_tracking::latest();
   if(result.inference_id==i+1 || replay::failed()) break;
   k_msleep(1);
  } while(clock.now_ns()<timeout);
  const auto retained=eye_tracking::latest();
  if(result.inference_id!=i+1 || retained.inference_id!=result.inference_id ||
     retained.output_hash!=result.output_hash) replay::fail("eye publisher/retained snapshot failed");
#ifdef ILLIXR_USE_GEMMINI_BLAS
  if(concurrent) { k_thread_join(&fp32_caller,K_FOREVER); if(!overlap_pass)replay::fail("overlapped FP32 fixture failed"); }
#endif
  size_t differences=0;for(size_t j=0;j<sizeof(ritnet_reference);j++)differences+=ritnet_output()[j]!=ritnet_reference[j];
  const auto ticks_after=atomic_get(&heartbeat_count);
  good=result.valid&&differences==0&&!replay::failed()&&ticks_after>ticks_before;
  printf("RITNET_INTERRUPT_PROGRESS %u %ld\n",i,long(ticks_after-ticks_before));
  printf("RITNET_ROUTE_PHASE int8_end %u mismatches=%zu valid=%d hash=%llu\n",i,differences,result.valid,(unsigned long long)result.output_hash);
  if(good)good=fp32();
 }
 atomic_set(&heartbeat_stop,1);k_thread_join(heartbeat_tid,K_FOREVER);
 eye_tracking::shutdown();
#ifdef ILLIXR_USE_GEMMINI_BLAS
 gemmini_backend::shutdown();
#endif
 trace_output::begin();eye_tracking::dump();blas_backend::dump();trace_output::finish();
 printf("ILLIXR_RITNET_STANDALONE_END %s\n",good?"pass":"fail");fflush(stdout);
 while(tohost)k_yield();asm volatile("fence rw,rw" ::: "memory");tohost=good?1:3;
 for(;;)asm volatile("wfi");
}
