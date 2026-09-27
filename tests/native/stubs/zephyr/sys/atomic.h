#pragma once

#include <cstdint>

// Native declarations match the RV64 scalar widths. There is intentionally no
// implementation: clock/atomic runtime behavior is outside this transport test.
using atomic_t = std::intptr_t;
using atomic_val_t = std::intptr_t;
atomic_val_t atomic_set(atomic_t* target, atomic_val_t value);
atomic_val_t atomic_get(const atomic_t* target);
bool atomic_cas(atomic_t* target, atomic_val_t expected, atomic_val_t desired);
