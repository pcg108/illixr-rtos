#include "hpm.hpp"
#if ILLIXR_HPM_PROFILE
#include "replay.hpp"
#include "trace_output.hpp"
#include <zephyr/kernel_structs.h>
#include <cstring>

namespace ILLIXR::hpm {
namespace {
constexpr unsigned harts=CONFIG_MP_MAX_NUM_CPUS;
struct Slot { k_tid_t tid{}; Context context{}; unsigned last_hart=~0u; uint64_t migrations{}; };
Slot slots[32];
// Register before a thread is started. Only its executing hart subsequently
// mutates the slot; Zephyr's context switch supplies the migration handoff.
unsigned slot_count{};
struct Hart {
  Accounting accounting;
  Snapshot last{},begin{},end{};
  Context current{},interrupted{};
  unsigned nested{};
  bool enabled{},configured{};
  uint64_t samples{},max_read_cycles{},errors{},begin_timer{},end_timer{};
};
Hart data[harts];
K_THREAD_STACK_ARRAY_DEFINE(control_stacks,harts,4096);
k_thread control_threads[harts];
k_sem commands[harts], completions[harts];
unsigned command[harts];
bool started{};
K_THREAD_STACK_ARRAY_DEFINE(test_stacks,harts,4096);
k_thread test_threads[harts];
k_sem test_ready[harts],test_go[harts];
unsigned test_harts[2][harts]{};
bool test_first_switch[harts]{};
unsigned hart() { unsigned long v;asm volatile("csrr %0,mhartid":"=r"(v));return unsigned(v); }
#define READ_CSR(csr, value) asm volatile("csrr %0," #csr : "=r"(value) :: "memory")
#define WRITE_CSR(csr, value) asm volatile("csrw " #csr ",%0" :: "r"(uint64_t(value)) : "memory")
Snapshot sample(Hart& h) {
  Snapshot s;
  READ_CSR(mcycle,s.values[0]);READ_CSR(minstret,s.values[1]);
#define R(n,i) READ_CSR(mhpmcounter##n,s.values[i]);
  R(3,2) R(4,3) R(5,4) R(6,5) R(7,6) R(8,7) R(9,8) R(10,9)
  R(11,10) R(12,11) R(13,12) R(14,13) R(15,14)
#undef R
  uint64_t end;READ_CSR(mcycle,end);
  const auto span=end-s.values[0];if(span>h.max_read_cycles) h.max_read_cycles=span;
  ++h.samples;return s;
}
Slot* slot(k_tid_t tid) {
  for(unsigned i=0,n=__atomic_load_n(&slot_count,__ATOMIC_ACQUIRE);i<n;++i) if(slots[i].tid==tid) return &slots[i];
  return nullptr;
}
Context owner(unsigned id) {
  // First switch-in runs before z_thread_entry initializes z_tls_current.
  // k_current_get() can therefore return null here with CURRENT_THREAD_USE_TLS.
  // Scheduler state already identifies the incoming thread at this boundary.
  auto tid=k_sched_current_thread_query();
  if(auto* s=slot(tid)) {
    if(s->last_hart!=~0u && s->last_hart!=id) ++s->migrations;
    s->last_hart=id;return s->context;
  }
  if(tid==_kernel.cpus[id].idle_thread) return {Owner::Idle};
  return {};
}
void boundary(Hart& h,Context next) {
  if(!h.enabled) return;
  const auto begin=sample(h);
  h.accounting.charge(h.current,h.last,begin);
  h.current=next;
  const auto end=sample(h);
  h.accounting.charge({Owner::Profiler},begin,end);
  h.last=end;
}
bool configure(Hart& h) {
  uint64_t value;
  // WARL readback detects legacy bitstreams whose unimplemented counters read 0.
  // Do not stop or reset mcycle/minstret (used elsewhere by the application).
  constexpr uint64_t mask=((uint64_t(1)<<16)-1)&~uint64_t(7);
  asm volatile("csrs mcountinhibit,%0"::"r"(mask):"memory");
  bool ok=true;
#define C(n,i) \
  WRITE_CSR(mhpmevent##n,selectors[i]);READ_CSR(mhpmevent##n,value);ok &= value==selectors[i]; \
  WRITE_CSR(mhpmcounter##n,0x12345);READ_CSR(mhpmcounter##n,value);ok &= value==0x12345; \
  WRITE_CSR(mhpmcounter##n,0);
  C(3,0) C(4,1) C(5,2) C(6,3) C(7,4) C(8,5) C(9,6) C(10,7)
  C(11,8) C(12,9) C(13,10) C(14,11) C(15,12)
#undef C
  asm volatile("csrc mcountinhibit,%0"::"r"(mask):"memory");
  const auto a=sample(h);
  asm volatile(".rept 32\naddi zero,zero,0\n.endr":::"memory");
  const auto b=sample(h);
  return ok && b.values[0]>a.values[0] && b.values[1]>a.values[1];
}
void controller(void* arg,void*,void*) {
  unsigned id=reinterpret_cast<uintptr_t>(arg);
  auto& h=data[id];
  if(hart()!=id) {++h.errors;k_sem_give(&completions[id]);return;}
  auto key=arch_irq_lock();h.configured=configure(h);arch_irq_unlock(key);
  k_sem_give(&completions[id]);
  for(;;) {
    k_sem_take(&commands[id],K_FOREVER);
    key=arch_irq_lock();
    if(command[id]==1) {
      h.accounting.size=0;h.accounting.errors=0;h.errors=0;h.samples=0;h.max_read_cycles=0;
      h.current={Owner::Profiler};h.nested=0;
      h.begin_timer=k_cycle_get_64();h.last=h.begin=sample(h);h.enabled=true;
    } else {
      h.end=sample(h);h.accounting.charge(h.current,h.last,h.end);
      h.end_timer=k_cycle_get_64();h.enabled=false;
    }
    arch_irq_unlock(key);k_sem_give(&completions[id]);
  }
}
void dispatch(unsigned op) {
  for(unsigned i=0;i<harts;++i){command[i]=op;k_sem_give(&commands[i]);}
  for(unsigned i=0;i<harts;++i)
    if(k_sem_take(&completions[i],K_SECONDS(5))) replay::fail("HPM rendezvous timed out");
}
void values(const Snapshot& v) {
  trace_output::print("[");
  for(unsigned i=0;i<counter_count;++i)
    trace_output::print("%s%llu",i?",":"",(unsigned long long)v.values[i]);
  trace_output::print("]");
}
}
void register_thread(k_tid_t tid,Owner name) {
  if(slot(tid)) {replay::fail("duplicate HPM thread registration");return;}
  if(slot_count==32){replay::fail("HPM thread registry full");return;}
  const auto index=slot_count;slots[index]={tid,{name}};
  __atomic_store_n(&slot_count,index+1,__ATOMIC_RELEASE);
}
void register_thread(k_tid_t tid,const char* name) {
  for(unsigned i=0;i<unsigned(Owner::Count);++i)
    if(!std::strcmp(name,owners[i])) {register_thread(tid,Owner(i));return;}
  replay::fail("unknown HPM plugin owner");
}
bool attribution_self_test();
bool initialize() {
  register_thread(k_current_get(),Owner::Main);
  for(unsigned i=0;i<harts;++i) {
    k_sem_init(&commands[i],0,1);k_sem_init(&completions[i],0,1);
    auto tid=k_thread_create(&control_threads[i],control_stacks[i],K_THREAD_STACK_SIZEOF(control_stacks[i]),
      controller,reinterpret_cast<void*>(uintptr_t(i)),nullptr,nullptr,K_PRIO_PREEMPT(0),0,K_FOREVER);
    register_thread(tid,Owner::Profiler);k_thread_name_set(tid,"hpm-control");
#ifdef CONFIG_SCHED_CPU_MASK
    if(k_thread_cpu_pin(tid,i)) return false;
#else
    static_assert(harts==1,"HPM control requires SMP affinity");
#endif
    k_thread_start(tid);
  }
  bool ok=true;
  for(unsigned i=0;i<harts;++i) {
    if(k_sem_take(&completions[i],K_SECONDS(5))) ok=false;
    ok &= data[i].configured && !data[i].errors;
    printf("ILLIXR_HPM_PREFLIGHT {\"version\":1,\"hart\":%u,\"passed\":%s,\"programmable_counters\":13}\n",i,data[i].configured?"true":"false");
  }
  return ok && attribution_self_test();
}
void start(){if(started){replay::fail("HPM duplicate start");return;}started=true;
  for(unsigned i=0;i<slot_count;++i){slots[i].migrations=0;slots[i].last_hart=~0u;}
  dispatch(1);}
void stop(){if(started){dispatch(2);started=false;}}
Context context() {
  auto key=arch_irq_lock();auto* s=slot(k_current_get());Context c=s?s->context:Context{};
  arch_irq_unlock(key);return c;
}
Context exchange(Context next) {
  auto key=arch_irq_lock();auto* s=slot(k_current_get());Context previous=s?s->context:Context{};
  if(s){boundary(data[hart()],next);s->context=next;}else replay::fail("HPM scope on unregistered thread");
  arch_irq_unlock(key);return previous;
}
namespace {
void attribution_test_worker(void* arg,void*,void*) {
  const unsigned index=reinterpret_cast<uintptr_t>(arg);
  auto key=arch_irq_lock();
  const auto* initial=slot(k_current_get());
  test_first_switch[index]=initial && initial->last_hart==hart() &&
    data[hart()].current.owner==Owner::System;
  arch_irq_unlock(key);
  Scope worker_owner({Owner::EyeTracking});
  for(unsigned stage=0;stage<2;++stage) {
    test_harts[stage][index]=hart();
    for(unsigned i=0;i<256;++i) asm volatile(".rept 64\naddi zero,zero,0\n.endr":::"memory");
    {Scope nested({Owner::Prediction,Phase::Default,Owner::EyeTracking});
     asm volatile(".rept 256\naddi zero,zero,0\n.endr":::"memory");}
    k_sem_give(&test_ready[index]);
    if(!stage) k_sem_take(&test_go[index],K_FOREVER);
  }
}
}
bool attribution_self_test() {
  for(unsigned i=0;i<harts;++i) {
    k_sem_init(&test_ready[i],0,1);k_sem_init(&test_go[i],0,1);
    auto tid=k_thread_create(&test_threads[i],test_stacks[i],K_THREAD_STACK_SIZEOF(test_stacks[i]),
      attribution_test_worker,reinterpret_cast<void*>(uintptr_t(i)),nullptr,nullptr,K_PRIO_PREEMPT(4),0,K_FOREVER);
    register_thread(tid,Owner::System);k_thread_name_set(tid,"hpm-test");
#ifdef CONFIG_SCHED_CPU_MASK
    if(k_thread_cpu_pin(tid,i)) return false;
#endif
  }
  start();
  for(unsigned i=0;i<harts;++i) k_thread_start(&test_threads[i]);
  bool ok=true;
  for(unsigned i=0;i<harts;++i) if(k_sem_take(&test_ready[i],K_SECONDS(5))) ok=false;
  // The measured workers block while another context retires >1M instructions.
  // Naive call-entry/exit subtraction would incorrectly charge this work.
  for(unsigned i=0;i<16384;++i) asm volatile(".rept 64\naddi zero,zero,0\n.endr":::"memory");
  k_msleep(5);
  for(unsigned i=0;i<harts;++i) {
#ifdef CONFIG_SCHED_CPU_MASK
    k_thread_suspend(&test_threads[i]);
    if(k_thread_cpu_pin(&test_threads[i],(i+1)%harts)) ok=false;
    k_thread_resume(&test_threads[i]);
#endif
    k_sem_give(&test_go[i]);
  }
  for(unsigned i=0;i<harts;++i) {
    if(k_sem_take(&test_ready[i],K_SECONDS(5))) ok=false;
    if(k_thread_join(&test_threads[i],K_SECONDS(5))) ok=false;
    ok &= test_harts[0][i]==i && test_harts[1][i]==(i+1)%harts;
    ok &= test_first_switch[i];
    if(harts>1) ok &= slot(&test_threads[i])->migrations>=1;
  }
  stop();
  uint64_t instructions=0,prediction=0,isr_instructions=0;unsigned seen=0;
  for(unsigned id=0;id<harts;++id) {
    auto& h=data[id];ok &= h.errors==0 && h.accounting.errors==0 && h.nested==0;
    Snapshot total;
    for(unsigned j=0;j<h.accounting.size;++j) {
      const auto& r=h.accounting.records[j];
      for(unsigned c=0;c<counter_count;++c)total.values[c]+=r.totals.values[c];
      if(r.context.owner==Owner::EyeTracking){instructions+=r.totals.values[1];seen|=1u<<id;}
      if(r.context.owner==Owner::Prediction && r.context.caller==Owner::EyeTracking) prediction+=r.totals.values[1];
      if(r.context.owner==Owner::Isr) isr_instructions+=r.totals.values[1];
    }
    for(unsigned c=0;c<counter_count;++c)ok &= total.values[c]==delta(h.begin.values[c],h.end.values[c],c);
  }
  // k_msleep above requires a timer wake. Conservation alone cannot detect
  // missing ISR callbacks: it would charge their instructions to a thread.
  ok &= seen==((1u<<harts)-1) && instructions>=32768*harts && instructions<250000*harts && prediction>=512*harts && isr_instructions>0;
  printf("ILLIXR_HPM_SELFTEST {\"version\":1,\"passed\":%s,\"hart_mask\":%u,\"worker_instructions\":%llu,\"prediction_instructions\":%llu,\"isr_instructions\":%llu,\"migration_required\":%s}\n",ok?"true":"false",seen,(unsigned long long)instructions,(unsigned long long)prediction,(unsigned long long)isr_instructions,harts>1?"true":"false");
  return ok;
}
void dump() {
  trace_output::print("ILLIXR_HPM_CONFIG {\"version\":1,\"enabled\":true,\"event_map\":\"rocket-hpm-v1\",\"harts\":%u,\"programmable_width\":40,\"basic_width\":64,\"events\":[",harts);
  for(unsigned i=0;i<counter_count;++i) trace_output::print("%s\"%s\"",i?",":"",events[i]);
  trace_output::print("],\"selectors\":[");
  for(unsigned i=0;i<programmable_count;++i) trace_output::print("%s%llu",i?",":"",(unsigned long long)selectors[i]);
  trace_output::print("],\"attribution\":\"scheduled_context_exclusive\",\"isr_boundary\":\"body_only\",\"sampling\":\"ordered_csr_reads_no_counter_freeze\",\"profiler_overhead\":\"measured_hook_body_lower_bound\"}\n");
  for(unsigned id=0;id<harts;++id){
    const auto& h=data[id];
    for(unsigned j=0;j<h.accounting.size;++j){const auto& r=h.accounting.records[j];
      trace_output::print("ILLIXR_HPM_WORK {\"version\":1,\"hart\":%u,\"plugin\":\"%s\",\"phase\":\"%s\",\"caller\":\"%s\",\"intervals\":%llu,\"counters\":",id,owners[unsigned(r.context.owner)],phases[unsigned(r.context.phase)],owners[unsigned(r.context.caller)],(unsigned long long)r.intervals);
      values(r.totals);trace_output::print("}\n");
    }
    Snapshot total;for(unsigned i=0;i<counter_count;++i)total.values[i]=delta(h.begin.values[i],h.end.values[i],i);
    trace_output::print("ILLIXR_HPM_HART {\"version\":1,\"hart\":%u,\"begin_timer\":%llu,\"end_timer\":%llu,\"samples\":%llu,\"max_read_cycles\":%llu,\"errors\":%llu,\"counters\":",id,(unsigned long long)h.begin_timer,(unsigned long long)h.end_timer,(unsigned long long)h.samples,(unsigned long long)h.max_read_cycles,(unsigned long long)(h.errors+h.accounting.errors));
    values(total);trace_output::print("}\n");
  }
  for(unsigned i=0;i<slot_count;++i)trace_output::print("ILLIXR_HPM_THREAD {\"version\":1,\"id\":%u,\"plugin\":\"%s\",\"migrations\":%llu}\n",i,owners[unsigned(slots[i].context.owner)],(unsigned long long)slots[i].migrations);
}
void switched(bool in) {
  auto key=arch_irq_lock();auto id=hart();auto& h=data[id];
  if(h.enabled) boundary(h,in?owner(id):Context{Owner::Switch});arch_irq_unlock(key);
}
void interrupt(bool enter) {
  // The RISC-V trap path calls these hooks with interrupts masked. Avoid a
  // redundant mstatus write in that path; retain locking if another caller
  // enters with interrupts enabled. READ_CSR supplies compiler ordering.
  unsigned long key;READ_CSR(mstatus,key);
  if(arch_irq_unlocked(key)) key=arch_irq_lock();
  auto& h=data[hart()];
  if(h.enabled){
    if(enter){if(h.nested++==0){h.interrupted=h.current;boundary(h,{Owner::Isr});}}
    else if(!h.nested) ++h.errors;
    else if(--h.nested==0) boundary(h,h.interrupted);
  }
  // Zephyr normally calls ISR hooks with interrupts already masked. Preserve
  // that state without issuing a redundant mstatus write with a zero mask.
  // Keep the compiler ordering boundary even when no CSR write is needed.
  if(arch_irq_unlocked(key)) arch_irq_unlock(key);
  else compiler_barrier();
}
}
extern "C" void sys_trace_thread_switched_in_user(){ILLIXR::hpm::switched(true);}
extern "C" void sys_trace_thread_switched_out_user(){ILLIXR::hpm::switched(false);}
extern "C" void sys_trace_isr_enter_user(){ILLIXR::hpm::interrupt(true);}
extern "C" void sys_trace_isr_exit_user(){ILLIXR::hpm::interrupt(false);}
#endif
