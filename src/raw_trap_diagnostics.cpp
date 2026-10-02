// Opt-in, terminal fault capture. Never called on the normal execution path.
// Bypass printk/logging and HTIF's thread mutex: either may be unsafe in a trap.
#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/arch/riscv/exception.h>
#include <cstdint>
#include <cstddef>
extern "C" { extern volatile uint64_t tohost, fromhost; }
namespace {
unsigned owner;
bool ram(uintptr_t p, size_t size) { return p>=0x80000000UL && p<=0x90000000UL-size; }
[[noreturn]] void halt() { for (;;) asm volatile("wfi"); }
void ready() {
  while(tohost) { if(fromhost) fromhost=0; }
  asm volatile("fence rw,rw" ::: "memory");
}
void put(char c) { ready();tohost=(1ULL<<56)|(1ULL<<48)|static_cast<unsigned char>(c); }
void text(const char *s) { while(*s) put(*s++); }
void hex(uintptr_t n) { for(int i=15;i>=0;--i) put("0123456789abcdef"[(n>>(i*4))&15]); }
void field(const char *name,uintptr_t n) { text(" ");text(name);text("=");hex(n); }
[[noreturn]] void capture(unsigned entry,unsigned reason,const arch_esf *frame) {
  uintptr_t cause,value,pc,status,hart;
  asm volatile("csrr %0, mcause" : "=r"(cause));
  asm volatile("csrr %0, mtval" : "=r"(value));
  asm volatile("csrr %0, mepc" : "=r"(pc));
  asm volatile("csrr %0, mstatus" : "=r"(status));
  asm volatile("csrr %0, mhartid" : "=r"(hart));
  // Only after a fatal exception: prevent recursive interrupts during export.
  asm volatile("csrci mstatus, 8" ::: "memory");
  unsigned expected=0;
  if(!__atomic_compare_exchange_n(&owner,&expected,1,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) halt();
  text("\nILLIXR_TRAP");field("entry",entry);field("reason",reason);
  field("hart",hart);field("mcause",cause);field("mtval",value);field("mepc",pc);field("mstatus",status);field("esf",reinterpret_cast<uintptr_t>(frame));put('\n');
  const auto cpu=arch_curr_cpu();
  const auto thread=ram(reinterpret_cast<uintptr_t>(cpu),sizeof(*cpu)) ? cpu->current : nullptr;
  const auto tp=reinterpret_cast<uintptr_t>(thread);
  text("ILLIXR_TRAP_THREAD");field("ptr",tp);
  if(ram(tp,sizeof(*thread)) && !(tp&(alignof(k_thread)-1))) {
    field("prio",static_cast<uintptr_t>(thread->base.prio));
#ifdef CONFIG_THREAD_NAME
    text(" name=");for(unsigned i=0;i<sizeof(thread->name) && thread->name[i];++i) {
      const unsigned char c=thread->name[i];put(c>=32 && c<127?c:'?');
    }
#endif
#ifdef CONFIG_THREAD_STACK_INFO
    field("stack_start",thread->stack_info.start);field("stack_size",thread->stack_info.size);
#endif
  }
  put('\n');
  if(ram(reinterpret_cast<uintptr_t>(frame),sizeof(*frame)) && !(reinterpret_cast<uintptr_t>(frame)&7)) {
    text("ILLIXR_TRAP_FRAME");field("pc",frame->mepc);field("status",frame->mstatus);field("ra",frame->ra);field("sp",reinterpret_cast<uintptr_t>(frame)+sizeof(*frame));put('\n');
    text("ILLIXR_TRAP_ARGS");field("a0",frame->a0);field("a1",frame->a1);field("a2",frame->a2);field("a3",frame->a3);field("a4",frame->a4);field("a5",frame->a5);field("a6",frame->a6);field("a7",frame->a7);put('\n');
    text("ILLIXR_TRAP_TEMPS");field("t0",frame->t0);field("t1",frame->t1);field("t2",frame->t2);field("t3",frame->t3);field("t4",frame->t4);field("t5",frame->t5);field("t6",frame->t6);put('\n');
    const auto sp=reinterpret_cast<uintptr_t>(frame)+sizeof(*frame);
    if(ram(sp,512)) for(unsigned i=0;i<64;i+=4) {
      auto words=reinterpret_cast<const volatile uintptr_t *>(sp);
      text("ILLIXR_TRAP_STACK");field("at",sp+i*8);
      for(unsigned j=0;j<4;++j) { put(' ');hex(words[i+j]); } put('\n');
    }
  }
  text("ILLIXR_TRAP_END\n");ready();tohost=3;halt();
}
}
extern "C" void __wrap_z_riscv_fault(arch_esf *esf) { capture(1,0,esf); }
extern "C" void __wrap_z_riscv_fatal_error(unsigned reason,const arch_esf *esf) { capture(2,reason,esf); }
extern "C" void __wrap_z_fatal_error(unsigned reason,const arch_esf *esf) { capture(3,reason,esf); }
