// Runtime synchronization only; OpenCV image algorithms remain unchanged.
#include <opencv2/core/utility.hpp>
#include <zephyr/kernel.h>

namespace cv {
Mutex::Mutex() : state_(new k_mutex) {
    k_mutex_init(static_cast<k_mutex*>(state_));
}
Mutex::~Mutex() { delete static_cast<k_mutex*>(state_); }
void Mutex::lock() { k_mutex_lock(static_cast<k_mutex*>(state_), K_FOREVER); }
void Mutex::unlock() { k_mutex_unlock(static_cast<k_mutex*>(state_)); }
bool Mutex::try_lock() { return k_mutex_lock(static_cast<k_mutex*>(state_), K_NO_WAIT) == 0; }
}
