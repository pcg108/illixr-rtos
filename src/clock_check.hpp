#pragma once
#include "replay.hpp"
#include <zephyr/kernel.h>

#ifndef ILLIXR_CORE_HZ
#define ILLIXR_CORE_HZ 0
#endif

namespace ILLIXR::clock_check {
inline constexpr unsigned cpu_count = CONFIG_MP_MAX_NUM_CPUS;
inline constexpr unsigned test_workers = cpu_count < 2 ? 2 : cpu_count;
K_THREAD_STACK_ARRAY_DEFINE(stacks, test_workers, 4096);
inline k_thread threads[test_workers];
inline k_sem turns[test_workers];
inline LatestValue<uint64_t> mailbox;
inline atomic_t seen_harts{};
inline unsigned char byte_lock{};
inline unsigned protected_count{}, protected_inverse{~0u};

/*
 * Worker in each test thread to perform the clock checks.
*/
inline void worker(void *arg, void *, void *) {
  const auto index = reinterpret_cast<uintptr_t>(arg);
  // record which hart this worker reached
  atomic_or(&seen_harts, 1ul << replay::hart_id());

  // perform the clock checks
  // take the turns semaphore, publish the current time, and pass the turn to the next thread
  // check that the clock is monotonic across different harts
  uint64_t previous{};
  for (unsigned i = 0; i < 64; ++i) {
    if (k_sem_take(&turns[index], K_SECONDS(1)) != 0) {
      replay::fail("clock test handoff timed out");
      return;
    }
    uint64_t value{}, seq{};
    mailbox.read(value, seq);
    const auto now = k_cycle_get_64();
    if (now < previous || (seq && now < value))
      replay::fail("clock not monotonic across harts");
    previous = now;
    mailbox.publish(now);
    k_sem_give(&turns[(index + 1) % test_workers]);
  }

  // perform atomic operations, ensuring mutual exclusion
  for (unsigned i = 0; i < 1024; ++i) {
    while (__atomic_exchange_n(&byte_lock, 1, __ATOMIC_SEQ_CST))
      k_yield();
    if (protected_inverse != ~protected_count)
      replay::fail("atomic publication ordering failed");
    ++protected_count;
    protected_inverse = ~protected_count;
    __atomic_store_n(&byte_lock, 0, __ATOMIC_SEQ_CST);
  }
}

/*
 * Create temporary test threads, to verify behavior across different harts.
 * Validate results, return how many harts were online
*/
inline unsigned run() {

  for (unsigned i = 0; i < test_workers; ++i) {

    k_sem_init(&turns[i], i == 0 ? 1 : 0, 1);

    // create each thread without starting it 
    auto tid = k_thread_create(
        &threads[i], stacks[i], K_THREAD_STACK_SIZEOF(stacks[i]), worker,
        reinterpret_cast<void *>(static_cast<uintptr_t>(i)), nullptr, nullptr,
        3, 0, K_FOREVER);

#ifdef CONFIG_SCHED_CPU_MASK
    // if we enabled CPU affinity, pin the threads to specific CPUs
    if (k_thread_cpu_pin(tid, i % cpu_count) != 0)
      replay::fail("cannot pin clock test workers");
#endif

  }

  // start all the threads 
  for (unsigned i = 0; i < test_workers; ++i)
    k_thread_start(&threads[i]);

  // block calling main thread until all test threads are done
  for (unsigned i = 0; i < test_workers; ++i)
    k_thread_join(&threads[i], K_FOREVER);

  // each worker should have performed 1024 atomic operations
  if (protected_count != 1024 * test_workers)
    replay::fail("atomic mutual exclusion failed");

  // check that all harts were online
  const unsigned expected = (1u << cpu_count) - 1;
  const unsigned seen = atomic_get(&seen_harts);
  if (seen != expected)
    replay::fail("incorrect online hart mask");
  printf("ILLIXR_CLOCK "
         "{\"hart_mask\":%u,\"atomic_exchanges\":%u,\"timer_hz\":%llu,"
         "\"monotonic\":%s}\n",
         seen, protected_count,
         (unsigned long long)sys_clock_hw_cycles_per_sec(),
         replay::failed() ? "false" : "true");
  unsigned online = 0;
  for (unsigned bits = seen; bits; bits >>= 1)
    online += bits & 1;
  return online;
}

inline uint64_t core_cycles() {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value));
  return value;
}

/*
 * Check the platform's clock and timer behavior across different harts.
 * ensure that hardware timer advances and its relationship to CPU cycles matches configured frequencies.
 * RISC-V mtime counter exposed through CLINT 
 */
inline void platform(unsigned online_harts) {
  // Stay runnable to prevent WFI clock gating from distorting the core/timer ratio.
  // Keep the two core-counter readings on the same hart. Prevents ordinary thread preemption during measurement
  k_sched_lock(); 
  const auto start_hart = replay::hart_id();
  // core_cycles reads rdcycle
  const auto start_core = core_cycles();
  // k_cycle_get_64 checks mtime
  const auto start_timer = k_cycle_get_64();

  // wait for approximately 10ms via hardware timer increments
  const uint64_t timer_hz = sys_clock_hw_cycles_per_sec();
  const uint64_t target_ticks = timer_hz / 100; 
  const uint64_t cycle_limit = ILLIXR_CORE_HZ ? ILLIXR_CORE_HZ / 10 : 100000000;
  while (k_cycle_get_64() - start_timer < target_ticks) {
    if (core_cycles() - start_core > cycle_limit) {
      replay::fail("platform timer did not advance");
      break;
    }
  }

  // measure elapsed counts and release scheduling lock
  const uint64_t ticks = k_cycle_get_64() - start_timer;
  const uint64_t cycles = core_cycles() - start_core;
  const auto end_hart = replay::hart_id();
  k_sched_unlock();

  // compare observed ratio against configuration expectation
  // e.g. on 1 GHz CPU and 1 MHz timer, 10ms -> 10000 timer increments, 10000000 cycles -> 1000 cycles/increment
  bool checked_ratio = false;
  if (ILLIXR_CORE_HZ && start_hart == end_hart && ticks > 0) {
    checked_ratio = true;
    const uint64_t measured = cycles * timer_hz;
    const uint64_t expected = ticks * static_cast<uint64_t>(ILLIXR_CORE_HZ);
    const uint64_t difference =
        measured > expected ? measured - expected : expected - measured;
    if (difference >
        expected / 20) // 5% allows MMIO reads and interrupt overhead.
      replay::fail("platform core/timer frequency mismatch");
  }
  if (ILLIXR_CORE_HZ && !checked_ratio)
    replay::fail("platform timer ratio check was not completed");
  printf("ILLIXR_PLATFORM "
         "{\"status\":\"%s\",\"online_harts\":%u,\"timer_hz\":%llu,"
         "\"elapsed_timer_ticks\":%llu,\"elapsed_core_cycles\":%llu,\"core_"
         "hz\":%llu,"
         "\"ratio_checked\":%s,\"start_hart\":%u,\"end_hart\":%u}\n",
         replay::failed() ? "fail" : "pass", online_harts,
         (unsigned long long)timer_hz, (unsigned long long)ticks,
         (unsigned long long)cycles, (unsigned long long)ILLIXR_CORE_HZ,
         checked_ratio ? "true" : "false", start_hart, end_hart);
}
} // namespace ILLIXR::clock_check
