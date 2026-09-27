#pragma once

#include <cstdint>

// Declarations only: the transport test compiles the real relative_clock.hpp,
// but must never call Zephyr timing services. Such a call will fail to link.
std::uint64_t k_cycle_get_64();
std::uint64_t k_cyc_to_ns_floor64(std::uint64_t cycles);
std::int64_t k_uptime_ticks();
std::uint64_t k_ns_to_ticks_ceil64(std::uint64_t nanoseconds);
