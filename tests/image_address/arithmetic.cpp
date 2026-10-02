#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <atomic>

struct Result { double numerator, quotient, rounded; int coordinate; };
extern "C" void illixr_image_arithmetic(const double *, int, Result *);
extern "C" void illixr_image_arithmetic_tight(const double *, int, Result *);
static uint64_t bits(double value) {
  uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
K_THREAD_STACK_DEFINE(noise_stack, 4096);
static k_thread noise_thread;
static std::atomic<bool> stop_noise{false};
static std::atomic<unsigned> noise_rounds{0};
static void noise(void *, void *, void *) {
  while (!stop_noise.load()) {
    const uint64_t value = 0x7ff8000000001234ULL;
    asm volatile("fmv.d.x fa0,%0\nfmv.d.x fa1,%0\nfmv.d.x fa2,%0\nfmv.d.x fa3,%0\n"
                 "fmv.d.x fa4,%0\nfmv.d.x fa5,%0\nfmv.d.x fa6,%0\nfmv.d.x fa7,%0\n"
                 "fmv.d.x ft0,%0\nfmv.d.x ft1,%0\nfmv.d.x ft2,%0\nfmv.d.x ft3,%0\n"
                 "fmv.d.x ft4,%0\nfmv.d.x ft5,%0\nfmv.d.x ft6,%0\nfmv.d.x ft7,%0\n"
                 "fmv.d.x ft8,%0\nfmv.d.x ft9,%0\nfmv.d.x ft10,%0\nfmv.d.x ft11,%0"
                 :: "r"(value) : "fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7",
                    "ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","ft8","ft9","ft10","ft11");
    noise_rounds.fetch_add(1);
    k_sleep(K_TICKS(1));
  }
}
extern "C" { volatile unsigned illixr_fpu_barrier_enabled = 0; }
extern "C" bool illixr_image_arithmetic_check() {
  k_thread_create(&noise_thread, noise_stack, K_THREAD_STACK_SIZEOF(noise_stack),
                  noise, nullptr, nullptr, nullptr, -1, 0, K_NO_WAIT);
  // Exact values captured from the scalar FPGA fault stack. The unchanged
  // expression rounds to -2030 at x=365 on the host.
  const uint64_t raw[] = {0xbf2f94031ecd246bULL, 0xbecb786d36bfc476ULL,
                          0x3fb4cf67bc94b329ULL};
  double coefficients[3];
  std::memcpy(coefficients, raw, sizeof(raw));
  unsigned total_errors = 0;
#ifdef ILLIXR_DIAGNOSTIC_FPU_BARRIER
  constexpr unsigned phases = 2;
#else
  constexpr unsigned phases = 1;
#endif
  for (unsigned phase = 0; phase < phases; ++phase) {
    illixr_fpu_barrier_enabled = phase;
    unsigned errors = 0;
  for (unsigned tight = 0; tight < 2; ++tight) {
  for (unsigned masked = 0; masked < 2; ++masked) {
    for (unsigned i = 0; i < 50000; ++i) {
      Result result{};
      const unsigned key = masked ? irq_lock() : 0;
      if (tight) illixr_image_arithmetic_tight(coefficients, 365, &result);
      else illixr_image_arithmetic(coefficients, 365, &result);
      if (masked) irq_unlock(key);
      if (result.coordinate != -2030 || result.rounded != -2030.0) {
        if (errors++ < 8) {
          std::printf("ILLIXR_IMAGE_ARITHMETIC_ERROR barrier=%u tight=%u masked=%u iteration=%u numerator=%016llx quotient=%016llx rounded=%016llx coordinate=%d\n",
                 phase, tight, masked, i, (unsigned long long)bits(result.numerator),
                 (unsigned long long)bits(result.quotient),
                 (unsigned long long)bits(result.rounded), result.coordinate);
        }
      }
    }
  }
  }
  std::printf("ILLIXR_IMAGE_ARITHMETIC_PHASE barrier=%u checks=200000 errors=%u noise_rounds=%u\n",
              phase, errors, noise_rounds.load());
  total_errors += errors;
  }
  stop_noise.store(true);
  k_thread_join(&noise_thread, K_FOREVER);
  std::printf("ILLIXR_IMAGE_ARITHMETIC passed=%d checks=%u errors=%u noise_rounds=%u\n", !total_errors, phases*200000, total_errors, noise_rounds.load());
  return !total_errors;
}

bool image_arithmetic_check_entry() { return illixr_image_arithmetic_check(); }
