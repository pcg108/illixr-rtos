#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ILLIXR {

// Zephyr message queues copy raw bytes. Transport only scalar values; each
// subscriber constructs its own aligned Eigen message after receiving a copy.
struct ImuSample {
    std::int64_t timestamp_ns;
    double angular_v[3];
    double linear_a[3];
    std::size_t index;
};

static_assert(std::is_trivially_copyable_v<ImuSample>);
static_assert(std::is_standard_layout_v<ImuSample>);

} // namespace ILLIXR
