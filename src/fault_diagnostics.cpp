// Diagnostic-only fatal path: avoid dropping the first register messages from
// Zephyr's small deferred log buffer. Normal execution is unaffected.
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/exception.h>
extern "C" void __real_z_riscv_fault(arch_esf *);
extern "C" void __wrap_z_riscv_fault(arch_esf *frame) {
  unsigned long cause, value, pc;
  asm volatile("csrr %0, mcause" : "=r"(cause));
  asm volatile("csrr %0, mtval" : "=r"(value));
  asm volatile("csrr %0, mepc" : "=r"(pc));
  // Emit the trap location before draining deferred logs: a fault during
  // multicore logging must not hide these registers behind log_panic().
  printk("ILLIXR_CPU_FAULT mcause=%lx mtval=%lx mepc=%lx\n", cause, value, pc);
  log_panic();
  // Preserve the original faulting code layout: inspect only after a fatal
  // exception, without injecting calls into the image-processing hot path.
  // This diagnostic board has 256 MiB of RAM at 0x80000000.
  const auto sp = reinterpret_cast<unsigned long>(frame) + sizeof(*frame);
  if (sp >= 0x80000000UL && sp <= 0x90000000UL - 2048) {
    const auto *words = reinterpret_cast<const volatile unsigned long *>(sp);
    for (unsigned i = 0; i < 256; i += 4) {
      printk("ILLIXR_FAULT_STACK %lx %016lx %016lx %016lx %016lx\n",
             sp + i * sizeof(*words), words[i], words[i+1], words[i+2], words[i+3]);
    }
  }
  __real_z_riscv_fault(frame);
}
