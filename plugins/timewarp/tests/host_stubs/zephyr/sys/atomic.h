#pragma once
#include <cstdint>
using atomic_t = std::intptr_t;
using atomic_val_t = std::intptr_t;
inline atomic_val_t atomic_get(const atomic_t *value) { return __atomic_load_n(value, __ATOMIC_SEQ_CST); }
inline atomic_val_t atomic_set(atomic_t *value, atomic_val_t desired) { return __atomic_exchange_n(value, desired, __ATOMIC_SEQ_CST); }
inline atomic_val_t atomic_add(atomic_t *value, atomic_val_t increment) { return __atomic_fetch_add(value, increment, __ATOMIC_SEQ_CST); }
inline bool atomic_cas(atomic_t *value, atomic_val_t expected, atomic_val_t desired) {
  return __atomic_compare_exchange_n(value, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
