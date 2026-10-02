#include "gemmini_backend.hpp"
#include "gemmini_packing.hpp"
#include "replay.hpp"
#include "trace_output.hpp"
#include <zephyr/kernel.h>
#include <cstdio>
#include <algorithm>
// Include after C++ standard headers: the upstream header defines C macros.
#include "gemmini/gemmini.h"

extern "C" void *blas_memory_alloc(int);
extern "C" void blas_memory_free(void *);
namespace ILLIXR::gemmini_backend {
namespace {
constexpr size_t capacity=32u*1024u*1024u;
static_assert(DIM==4 && BANK_NUM==4 && BANK_ROWS==512 && ACC_ROWS==512);
static_assert(sizeof(elem_t)==4 && sizeof(acc_t)==4);
K_THREAD_STACK_DEFINE(worker_stack,16384);
k_thread worker;
K_SEM_DEFINE(ready,0,1);
K_SEM_DEFINE(done,0,1);
Request request{};
float *storage=nullptr;
bool started=false,stopping=false;
struct Stats {
 uint64_t calls=0,submissions=0,pack_ns=0,unpack_ns=0,queue_ns=0,execute_ns=0,cycles=0;
 uint64_t caller_harts[4]{};
 size_t high_water=0,max_m=0,max_n=0,max_k=0;
 unsigned accelerator_mask=0;
} stats[4];
uint64_t submitted=0;
uint64_t now() { return k_cycle_get_64(); }
uint64_t ns(uint64_t ticks) { return k_cyc_to_ns_floor64(ticks); }
[[noreturn]] void fail(const char *reason) {
 printf("ILLIXR_GEMMINI_ERROR %s\n",reason); k_panic(); __builtin_unreachable();
}
void run(void*,void*,void*) {
 while(true) {
  k_sem_take(&ready,K_FOREVER);
  if(stopping) break;
  if(replay::hart_id()!=0) fail("accelerator worker escaped hart zero");
  const auto &r=request;
  auto &s=stats[r.operation];
  const auto start=now(); s.queue_ns+=ns(start-submitted);
  float *a=storage,*b=a+r.m*r.k,*c=b+r.k*r.n;
  if(r.is_double) pack<double>(r,a,b,c); else pack<float>(r,a,b,c);
  const auto packed=now(); s.pack_ns+=ns(packed-start);
  uint64_t cycle0,cycle1;
  asm volatile("rdcycle %0":"=r"(cycle0));
  asm volatile("fence rw,rw" ::: "memory");
  gemmini_flush(0);
  tiled_matmul_auto(r.m,r.n,r.k,a,b,c,c,r.k,r.n,r.n,r.n,
      float(r.alpha),1.f,float(r.beta),NO_ACTIVATION,ACC_SCALE_IDENTITY,0,false,
      false,false,false,false,0,WS);
  gemmini_fence();
  asm volatile("fence rw,rw" ::: "memory");
  asm volatile("rdcycle %0":"=r"(cycle1));
  const auto executed=now(); s.execute_ns+=ns(executed-packed); s.cycles+=cycle1-cycle0;
  ++s.submissions; s.accelerator_mask|=1u<<replay::hart_id();
  if(r.is_double) unpack<double>(r,c); else unpack<float>(r,c);
  s.unpack_ns+=ns(now()-executed);
  k_sem_give(&done);
 }
}
void submit(Request r) {
 if(!started || stopping) fail("service is not accepting calls");
 if(r.operation>=4 || r.caller_hart>=CONFIG_MP_MAX_NUM_CPUS) fail("invalid request identity");
 auto &s=stats[r.operation]; ++s.calls; ++s.caller_harts[r.caller_hart];
 s.max_m=std::max(s.max_m,r.m);s.max_n=std::max(s.max_n,r.n);s.max_k=std::max(s.max_k,r.k);
 if(!r.m || !r.n) return;
 if(!r.k || r.alpha==0) {
  if(r.is_double) scale_only<double>(r); else scale_only<float>(r);
  return;
 }
 size_t bytes;
 if(!workspace_bytes(r,bytes) || bytes>capacity) fail("packing arena exhausted");
 s.high_water=std::max(s.high_water,bytes);
 storage=static_cast<float*>(blas_memory_alloc(0));
 request=r; submitted=now(); k_sem_give(&ready); k_sem_take(&done,K_FOREVER);
 blas_memory_free(storage);storage=nullptr;
}
}
void initialize() {
 if(started) fail("duplicate initialization");
 auto tid=k_thread_create(&worker,worker_stack,K_THREAD_STACK_SIZEOF(worker_stack),run,
                          nullptr,nullptr,nullptr,K_PRIO_PREEMPT(5),0,K_FOREVER);
#ifdef CONFIG_SMP
 if(k_thread_cpu_pin(tid,0)!=0) fail("cannot pin accelerator worker");
#endif
 k_thread_name_set(tid,"gemmini"); started=true;k_thread_start(tid);
}
void shutdown() {
 if(!started || stopping) return;
 stopping=true; k_sem_give(&ready);
 if(k_thread_join(&worker,K_SECONDS(1))) fail("worker shutdown timeout");
}
void dump(const char *phase,bool reset) {
 const char *names[]={"sgemm","dgemm","sgemv","dgemv"};
 for(unsigned i=0;i<4;++i) {
  const auto &s=stats[i];
  trace_output::print("ILLIXR_GEMMINI {\"phase\":\"%s\",\"name\":\"%s\",\"precision\":\"fp32\",\"calls\":%llu,\"submissions\":%llu,\"accelerator_hart_mask\":%u,\"caller_harts\":[%llu,%llu,%llu,%llu],\"packing_ns\":%llu,\"unpacking_ns\":%llu,\"queue_ns\":%llu,\"execution_ns\":%llu,\"cycles\":%llu,\"scratch_high_water\":%zu,\"max_m\":%zu,\"max_n\":%zu,\"max_k\":%zu}\n",
      phase,names[i],(unsigned long long)s.calls,(unsigned long long)s.submissions,s.accelerator_mask,
      (unsigned long long)s.caller_harts[0],(unsigned long long)s.caller_harts[1],(unsigned long long)s.caller_harts[2],(unsigned long long)s.caller_harts[3],
      (unsigned long long)s.pack_ns,(unsigned long long)s.unpack_ns,(unsigned long long)s.queue_ns,(unsigned long long)s.execute_ns,(unsigned long long)s.cycles,s.high_water,s.max_m,s.max_n,s.max_k);
 }
 if(reset) for(auto &s:stats)s={};
}
}
extern "C" void illixr_gemmini_gemm(int d,int ta,int tb,long m,long n,long k,
 double alpha,const void *a,long lda,const void *b,long ldb,double beta,void *c,long ldc) {
 using namespace ILLIXR::gemmini_backend;
 submit({bool(d),unsigned(d),size_t(m),size_t(n),size_t(k),alpha,beta,a,b,c,
         ta?lda:1,ta?1:lda,tb?ldb:1,tb?1:ldb,1,ldc,ILLIXR::replay::hart_id()});
}
extern "C" void illixr_gemmini_gemv(int d,int t,long m,long n,double alpha,
 const void *a,long lda,const void *x,long incx,double beta,void *y,long incy) {
 using namespace ILLIXR::gemmini_backend;
 const long nx=t?m:n,ny=t?n:m;
 const size_t width=d?sizeof(double):sizeof(float);
 if(incx<0) x=static_cast<const char*>(x)+(1-nx)*incx*ptrdiff_t(width);
 if(incy<0) y=static_cast<char*>(y)+(1-ny)*incy*ptrdiff_t(width);
 submit({bool(d),unsigned(2+d),size_t(ny),1,size_t(nx),alpha,beta,a,x,y,
         t?lda:1,t?1:lda,incx,0,incy,0,ILLIXR::replay::hart_id()});
}
