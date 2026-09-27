#pragma once
#include <cstdint>
#include <zephyr/kernel.h>

namespace ILLIXR {
// Copy snapshots under a short lock. Mutable estimator state never escapes its owner.
template <class T> class LatestValue {
  public:
    LatestValue() { k_mutex_init(&mutex_); }
    void publish(const T &value) {
        k_mutex_lock(&mutex_, K_FOREVER);
        value_ = value;
        ++sequence_;
        k_mutex_unlock(&mutex_);
    }
    bool read(T &value, std::uint64_t &sequence) {
        k_mutex_lock(&mutex_, K_FOREVER);
        const bool valid = sequence_ != 0;
        if (valid)
            value = value_;
        sequence = sequence_;
        k_mutex_unlock(&mutex_);
        return valid;
    }

  private:
    k_mutex mutex_{};
    T value_{};
    std::uint64_t sequence_{};
};
} // namespace ILLIXR
