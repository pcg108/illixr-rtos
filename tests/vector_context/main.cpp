#include "../../src/vector_check.hpp"
#include <cstdio>
#include <cstdint>
#include <zephyr/logging/log_ctrl.h>
extern "C" volatile uint64_t tohost;
int main() {
  log_flush();
  const bool good=ILLIXR::vector_check::run();
  printf("ILLIXR_VECTOR_PREFLIGHT_END %s\n",good?"pass":"fail");
  fflush(stdout);
  tohost=good?1:3;
  for(;;) asm volatile("wfi");
}
