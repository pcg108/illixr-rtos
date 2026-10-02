#include <zephyr/kernel.h>
#include <cstdint>
#include <cstdio>
struct TrapResult { uint64_t cause, pc, value, before, handler, after; };
extern "C" void illixr_faulting_fdiv(TrapResult *);
extern "C" char illixr_rejected_fdiv_pc[];
bool image_fpu_trap_check_entry() {
  TrapResult result{};
  const auto key = irq_lock();
  illixr_faulting_fdiv(&result);
  irq_unlock(key);
  const bool passed = result.cause == 2 &&
    result.pc == reinterpret_cast<uintptr_t>(illixr_rejected_fdiv_pc) &&
    result.before == 0x4022000000000000ULL &&
    result.handler == result.before && result.after == result.before;
  std::printf("ILLIXR_REJECTED_FDIV passed=%d cause=%llu pc=%llx instruction=%llx before=%016llx handler=%016llx after=%016llx\n",
              passed, (unsigned long long)result.cause, (unsigned long long)result.pc,
              (unsigned long long)result.value, (unsigned long long)result.before,
              (unsigned long long)result.handler, (unsigned long long)result.after);
  return passed;
}
