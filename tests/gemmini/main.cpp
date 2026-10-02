#include "blas_backend.hpp"
#include "gemmini_backend.hpp"
#include "vector_check.hpp"
#include "clock_check.hpp"
#include <zephyr/logging/log_ctrl.h>
extern "C" volatile uint64_t tohost;
#ifdef ILLIXR_GEMMINI_EDGE_CASES_ONLY
bool gemmini_edge_cases();
#endif
int main() {
 using namespace ILLIXR;
#ifdef ILLIXR_GEMMINI_EDGE_CASES_ONLY
 // A supplemental fixture, not a substitute for the complete platform/vector
 // gate. Drain the banner synchronously; no timed logging sleep is needed.
 log_panic();replay::initialize();blas_backend::initialize();
 bool good=gemmini_edge_cases();
 gemmini_backend::dump("edge",false);
#else
 log_flush();replay::initialize();
 const auto harts=clock_check::run();clock_check::platform(harts);
 blas_backend::initialize();
 bool good=!replay::failed() && vector_check::run();
 if(good)good=blas_backend::self_test();
#endif
 gemmini_backend::shutdown();
#ifdef ILLIXR_GEMMINI_EDGE_CASES_ONLY
 printf("ILLIXR_GEMMINI_EDGE_END %s\n",good?"pass":"fail");
#else
 printf("ILLIXR_GEMMINI_STANDALONE_END %s\n",good?"pass":"fail");
#endif
 fflush(stdout);
 while(tohost)k_yield();
 asm volatile("fence rw,rw" ::: "memory");tohost=good?1:3;
 for(;;)asm volatile("wfi");
}
