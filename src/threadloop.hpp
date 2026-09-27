#pragma once
#include "plugin.hpp"
#include "replay.hpp"
#include "stoplight.hpp"
#include <exception>

namespace ILLIXR {
class threadloop : public Plugin {
public:
  threadloop(phonebook_new &pb, const char *name, k_thread_stack_t *stack,
             size_t stack_size, int priority = 5, int cpu = -1)
      : Plugin{pb, name}, stack_{stack},
        stack_size_{stack_size}, priority_{priority}, cpu_{cpu} {
    if (worker_count < 20)
      workers[worker_count++] = this;
    else
      replay::fail("too many worker threads");
  }
  void start() override {
    tid_ = k_thread_create(&thread_, stack_, stack_size_, thread_entry, this,
                           nullptr, nullptr, K_PRIO_PREEMPT(priority_), 0,
                           K_FOREVER);
    if (tid_) {
      k_thread_name_set(tid_, node_.name());
      if (cpu_ >= 0) {
        if (cpu_ >= CONFIG_MP_MAX_NUM_CPUS)
          replay::fail("plugin affinity outside configured harts");
#ifdef CONFIG_SCHED_CPU_MASK
        else if (k_thread_cpu_pin(tid_, cpu_) != 0)
          replay::fail("cannot pin plugin worker");
#else
        else if (CONFIG_MP_MAX_NUM_CPUS != 1 || cpu_ != 0)
          replay::fail("plugin affinity unavailable");
#endif
      }
      // Even on configuration failure, run teardown so every producer publishes
      // EOS.
      k_thread_start(tid_);
    } else {
      replay::fail("thread creation failed");
      atomic_set(&done_, 1);
    }
  }
  void stop() override { atomic_set(&stop_flag_, 1); }
  bool should_terminate() const {
    return atomic_get(&stop_flag_) || replay::failed();
  }
  bool done() const { return atomic_get(&done_) != 0; }
  void join() {
    if (tid_)
      k_thread_join(tid_, K_FOREVER);
  }
  inline static threadloop *workers[20]{};
  inline static std::size_t worker_count{};

protected:
  enum class skip_option { run, skip_and_spin, skip_and_yield, stop };
  virtual void _p_thread_setup() {}
  virtual void _p_thread_teardown() {}
  virtual skip_option _p_should_skip() { return skip_option::run; }
  virtual void _p_one_iteration() = 0;
  size_t iteration_no{}, skip_no{};

private:
  void run() {
    try {
      _p_thread_setup();
      k_sem_give(&stoplight_ready);
      k_sem_take(&stoplight_start, K_FOREVER);
      while (!should_terminate()) {
        const auto action = _p_should_skip();
        if (action == skip_option::stop)
          break;
        if (action == skip_option::run) {
          _p_one_iteration();
          ++iteration_no;
          skip_no = 0;
        } else {
          ++skip_no;
          k_msleep(1);
        }
      }
    } catch (const std::exception &e) {
      replay::fail(e.what());
    } catch (...) {
      replay::fail("worker exception");
    }
    _p_thread_teardown();
    atomic_set(&done_, 1);
  }
  static void thread_entry(void *self, void *, void *) {
    static_cast<threadloop *>(self)->run();
  }
  k_thread_stack_t *stack_;
  size_t stack_size_;
  int priority_;
  int cpu_;
  mutable atomic_t stop_flag_{}, done_{};
  k_thread thread_{};
  k_tid_t tid_{};
};
} // namespace ILLIXR
