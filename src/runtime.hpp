#pragma once
#include "embedded_dataset.hpp"
#include "phonebook_new.hpp"
#include "plugin_registry.hpp"
#include "threadloop.hpp"

namespace ILLIXR {
class Runtime {
  public:
    explicit Runtime(phonebook_new &pb) : pb_{pb} {}
    void initialize(const char *, const char *) {}
    void start_all_plugins() {
        // look up registered plugins and call their startup functions 
        // before main() ran, each compiled plugin's REGISTER_PLUGIN() registered its name and startup function
        const auto &reg = get_plugin_registry();
        for (const auto &entry : reg)
            entry.start_fn(pb_);
        // Service plugins register no thread and do not participate in the
        // worker rendezvous (e.g. on-demand pose prediction).
        // collect stoplight_ready semaphore from all worker threads
        const auto worker_count = threadloop::worker_count;
        for (size_t i = 0; i < worker_count; ++i) {
            while (k_sem_take(&stoplight_ready, K_MSEC(10)) != 0) {
                if (replay::failed())
                    break;
            }
            if (replay::failed())
                break;
        }
        // associate dataset starting timestamp with current target timer reading 
        auto &clock = get_global_relative_clock();
        clock.set_dataset_origin(kEmbeddedDatasetOriginNs);
        hpm::start();
        clock.start();

        // release start permission per worker, allowing workers blocked on stoplight_start to proceed
        for (size_t i = 0; i < worker_count; ++i)
            k_sem_give(&stoplight_start);
    }
    bool finished() const {
        for (size_t i = 0; i < threadloop::worker_count; ++i)
            if (!threadloop::workers[i]->done())
                return false;
        return true;
    }
    void shutdown() {
        for (size_t i = 0; i < threadloop::worker_count; ++i)
            threadloop::workers[i]->stop();
        for (size_t i = 0; i < threadloop::worker_count; ++i)
            threadloop::workers[i]->join();
    }

  private:
    phonebook_new &pb_;
};
} // namespace ILLIXR
