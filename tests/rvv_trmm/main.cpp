// Reproduce GCC 13.2's optimized TRMM odd-tail fault without timer interrupts.
// The full application self-test checks all triangular operation variants.
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
extern "C" volatile uint64_t tohost;
extern "C" int dtrmm_kernel_LN(long, long, long, double, double *, double *, double *, long, long);
alignas(64) double a[4096], b[4096], c[4096];

bool fixture(int m, int n, int k) {
  for (auto &v : a) v = 1.;
  for (auto &v : b) v = 1.;
  for (auto &v : c) v = -999.;
  const auto key = irq_lock();
  const int rc = dtrmm_kernel_LN(m, n, k, 1., a, b, c, 32, 0);
  irq_unlock(key);
  bool good = rc == 0;
  // Packed constant operands: each row block sums K minus its starting row.
  int start = 0;
  for (int width = 8; width; width /= 2) {
    while (m - start >= width) {
      for (int row = start; row < start + width; ++row)
        for (int col = 0; col < n; ++col)
          good = good && c[row + 32 * col] == k - start;
      start += width;
    }
  }
  for (int col = 0; col < 128; ++col)
    for (int row = (col < n ? m : 0); row < 32; ++row)
      good = good && c[row + 32 * col] == -999.;
  return good;
}

int main() {
  log_flush();
  printf("ILLIXR_TRMM_REGRESSION begin interrupts=disabled\n");
  fflush(stdout);
  bool good = true;
  for (int m : {15, 17})
    for (int n : {15, 17}) good = fixture(m, n, m) && good;
  printf("ILLIXR_TRMM_REGRESSION %s\n", good ? "pass" : "fail");
  fflush(stdout);
  tohost = good ? 1 : 3;
  for (;;) asm volatile("wfi");
}
