#pragma once

// Host test adapter for the small clock/lock/semaphore API used by the actual
// gpu_pipeline.hpp. It does not emulate Zephyr scheduling or hart migration.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace host_test {
using Clock = std::chrono::steady_clock;
inline const auto epoch = Clock::now();
inline std::atomic<unsigned> sleep_calls{};
}
struct k_timeout_t { int64_t ticks; };
#define K_FOREVER k_timeout_t{-1}
#define K_NO_WAIT k_timeout_t{0}
#define K_MSEC(value) k_timeout_t{value}
#define K_TIMEOUT_ABS_TICKS(value) k_timeout_t{value}
struct k_mutex { std::mutex value; };
inline int k_mutex_init(k_mutex *) { return 0; }
inline int k_mutex_lock(k_mutex *mutex, k_timeout_t) { mutex->value.lock(); return 0; }
inline int k_mutex_unlock(k_mutex *mutex) { mutex->value.unlock(); return 0; }
struct k_sem {
  std::mutex mutex;
  std::condition_variable cv;
  unsigned count{}, limit{};
};
inline int k_sem_init(k_sem *sem, unsigned initial, unsigned limit) {
  sem->count = initial; sem->limit = limit; return 0;
}
inline void k_sem_give(k_sem *sem) {
  std::lock_guard<std::mutex> guard{sem->mutex};
  if (sem->count < sem->limit) ++sem->count;
  sem->cv.notify_one();
}
inline int k_sem_take(k_sem *sem, k_timeout_t timeout) {
  std::unique_lock<std::mutex> guard{sem->mutex};
  if (!sem->cv.wait_for(guard, std::chrono::milliseconds{timeout.ticks}, [&] { return sem->count != 0; })) return -1;
  --sem->count; return 0;
}
inline uint64_t k_cycle_get_64() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(host_test::Clock::now() - host_test::epoch).count();
}
inline uint64_t k_cyc_to_ns_floor64(uint64_t cycles) { return cycles; }
inline int64_t k_uptime_ticks() { return k_cycle_get_64() / 1000000; }
inline uint64_t k_ns_to_ticks_ceil64(uint64_t ns) { return (ns + 999999) / 1000000; }
inline int k_sleep(k_timeout_t timeout) {
  ++host_test::sleep_calls;
  std::this_thread::sleep_until(host_test::epoch + std::chrono::milliseconds{timeout.ticks});
  return 0;
}
