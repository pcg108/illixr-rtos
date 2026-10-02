#include "vector_check.hpp"
#include <zephyr/kernel.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void illixr_vector_load(const void *, unsigned long);
extern "C" void illixr_vector_store(void *, unsigned long *);
extern "C" void illixr_vector_fp64(const double *, double *);
namespace ILLIXR::vector_check {
namespace {
unsigned hart_id() { unsigned long hart; asm volatile("csrr %0, mhartid" : "=r"(hart)); return hart; }
constexpr unsigned harts=CONFIG_MP_MAX_NUM_CPUS, workers=2*harts, rounds=32;
K_THREAD_STACK_ARRAY_DEFINE(stacks,workers,16384);
k_thread threads[workers];
atomic_t errors{}, seen{}, migrations{};
uint64_t checks[workers]{};
K_SEM_DEFINE(ready,0,1);
K_SEM_DEFINE(resume,0,1);
void check(bool good) { if(!good) atomic_inc(&errors); }
unsigned long vlenb() {
  unsigned long value;
  asm volatile("csrr %0, vlenb" : "=r"(value));
  return value;
}
void pattern(unsigned char *bytes,unsigned salt) {
  for(unsigned i=0;i<1024;++i) bytes[i]=(i*17+salt*31)&255;
}
bool verify(const unsigned char *expected,unsigned csr) {
  alignas(64) unsigned char actual[1024];
  unsigned long control[5];
  illixr_vector_store(actual,control);
  return !std::memcmp(actual,expected,sizeof(actual)) &&
         control[0]==32 && control[1]==3 && control[2]==24 &&
         control[3]==csr && control[4]==1;
}
void worker(void *arg,void *,void *) {
  const auto id=static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
  if(vlenb()!=32) { check(false); return; }
  alignas(64) const double input[]={1.,2.,3.,4.};
  alignas(64) double output[4]{};
  illixr_vector_fp64(input,output);
  for(unsigned i=0;i<4;++i) check(output[i]==2.*input[i]*input[i]);
  alignas(64) unsigned char expected[1024];
  for(unsigned r=0;r<rounds;++r) {
    pattern(expected,id*rounds+r);
    const unsigned csr=(id+r)&7;
    illixr_vector_load(expected,csr);
    // Remain runnable across timer ticks and competing vector workers.
    const auto start=k_cycle_get_64();
    while(k_cycle_get_64()-start<sys_clock_hw_cycles_per_sec()/500) {}
    check(verify(expected,csr));
    illixr_vector_load(expected,csr);
    k_sleep(K_TICKS(1));
    check(verify(expected,csr));
    illixr_vector_load(expected,csr);
    k_yield();
    check(verify(expected,csr));
    ++checks[id];
    atomic_or(&seen,1u<<hart_id());
  }
}
void migrating_worker(void *,void *,void *) {
  alignas(64) unsigned char expected[1024];
  for(unsigned r=0;r<4*harts;++r) {
    pattern(expected,500+r);
    const unsigned before=hart_id();
    illixr_vector_load(expected,r&7);
    k_sem_give(&ready);
    if(k_sem_take(&resume,K_SECONDS(2))) { check(false); return; }
    check(verify(expected,r&7));
    const unsigned after=hart_id();
    check(after==(r+1)%harts);
    if(after!=before) atomic_inc(&migrations);
  }
}
}
bool run() {
  for(unsigned i=0;i<workers;++i) {
    auto tid=k_thread_create(&threads[i],stacks[i],K_THREAD_STACK_SIZEOF(stacks[i]),
        worker,reinterpret_cast<void *>(uintptr_t(i)),nullptr,nullptr,4,0,K_FOREVER);
#ifdef CONFIG_SCHED_CPU_MASK
    check(k_thread_cpu_pin(tid,i%harts)==0);
#endif
  }
  for(auto &thread:threads) k_thread_start(&thread);
  for(auto &thread:threads) k_thread_join(&thread,K_FOREVER);
#ifdef CONFIG_SCHED_CPU_MASK
  if(!atomic_get(&errors) && harts>1) {
    auto tid=k_thread_create(&threads[0],stacks[0],K_THREAD_STACK_SIZEOF(stacks[0]),
        migrating_worker,nullptr,nullptr,nullptr,4,0,K_FOREVER);
    check(k_thread_cpu_pin(tid,0)==0);k_thread_start(tid);
    for(unsigned r=0;r<4*harts;++r) {
      if(k_sem_take(&ready,K_SECONDS(2))) { check(false);break; }
      k_thread_suspend(tid);
      check(k_thread_cpu_pin(tid,(r+1)%harts)==0);
      k_thread_resume(tid);k_sem_give(&resume);
    }
    k_thread_join(tid,K_FOREVER);
    check(atomic_get(&migrations)==4*harts);
  }
#endif
  check(atomic_get(&seen)==(1u<<harts)-1);
  printf("ILLIXR_VECTOR_CHECK {\"passed\":%s,\"hart_mask\":%u,\"vlenb\":32,\"fp64\":true,\"errors\":%u,\"migrations\":%u,\"worker_rounds\":[",
      atomic_get(&errors)?"false":"true",unsigned(atomic_get(&seen)),unsigned(atomic_get(&errors)),unsigned(atomic_get(&migrations)));
  for(unsigned i=0;i<workers;++i) printf("%s%llu",i?",":"",(unsigned long long)checks[i]);
  printf("]}\n");
  return atomic_get(&errors)==0;
}
}
