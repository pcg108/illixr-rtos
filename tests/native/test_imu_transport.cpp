#include "data_format.hpp"
#include "imu_sample.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
using ILLIXR::ImuMsg;
using ILLIXR::ImuSample;

static_assert(std::is_trivially_copyable_v<ImuSample>);
static_assert(std::is_standard_layout_v<ImuSample>);
static_assert(sizeof(std::size_t) != 8 || sizeof(ImuSample) == 64,
              "RV64 transport records must have the expected bounded footprint");

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "IMU transport regression: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

std::uint64_t bits(double value) {
    std::uint64_t result;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void verify(const ImuMsg& actual, const ImuSample& expected) {
    require(actual.time.time_since_epoch().count() == expected.timestamp_ns,
            "timestamp nanoseconds changed or lost precision");
    require(actual.index == expected.index, "sample index changed or was truncated");
    for (std::size_t axis = 0; axis != 3; ++axis) {
        require(bits(actual.angular_v[axis]) == bits(expected.angular_v[axis]),
                "angular velocity component changed or moved to another axis");
        require(bits(actual.linear_a[axis]) == bits(expected.linear_a[axis]),
                "linear acceleration component changed or moved to another axis");
    }
}

ImuSample sample(std::int64_t timestamp, std::size_t index) {
    return {timestamp,
            {0.0, -0.0, 0x1.123456789abcp-17},
            {-9.80665, std::numeric_limits<double>::denorm_min(),
             std::numeric_limits<double>::max()},
            index};
}

void exact_conversion() {
    // Adjacent nanoseconds above 2^53 expose accidental conversion through
    // double; include signed boundaries and full-width sample indices too.
    constexpr std::array<std::int64_t, 7> timestamps{
        1403715523912143104LL, 1403715523912143105LL,
        (std::int64_t{1} << 53) + 1, 0, -1,
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max()};
    const std::array<std::size_t, 4> indices{
        0, 500, std::numeric_limits<std::size_t>::max() / 2,
        std::numeric_limits<std::size_t>::max()};
    for (auto timestamp : timestamps) {
        for (auto index : indices) {
            const ImuSample original = sample(timestamp, index);
            verify(ILLIXR::imu_message(original), original);
        }
    }
}

void byte_copy_fanout() {
    // Zephyr queues copy raw record bytes. Model only that transport contract,
    // not scheduler/overflow behavior: each subscriber owns an independent copy.
    using RecordBytes = std::array<unsigned char, sizeof(ImuSample)>;
    RecordBytes vio_slot{}, integrator_slot{};
    const ImuSample expected = sample(1403715523912143105LL, 501);
    ImuSample publisher = expected;
    std::memcpy(vio_slot.data(), &publisher, sizeof(publisher));
    std::memcpy(integrator_slot.data(), &publisher, sizeof(publisher));

    // Reuse the publisher's stack storage before either subscriber receives.
    publisher = sample(42, 7);
    publisher.angular_v[0] = 3.25;
    ImuSample vio_sample{};
    std::memcpy(&vio_sample, vio_slot.data(), sizeof(vio_sample));
    ImuMsg vio = ILLIXR::imu_message(vio_sample);
    verify(vio, expected);
    vio_sample.timestamp_ns = 8;
    vio_sample.angular_v[1] = 10.0;
    verify(vio, expected);  // Converted Eigen values cannot alias queue storage.
    vio.angular_v[2] = -99.0;
    vio.linear_a[0] = 22.0;
    vio.index = 0;

    // The delayed consumer must still see the published sample, unaffected by
    // producer reuse or the first consumer's conversion and subsequent writes.
    ImuSample integrator_sample{};
    std::memcpy(&integrator_sample, integrator_slot.data(), sizeof(integrator_sample));
    verify(ILLIXR::imu_message(integrator_sample), expected);
}
} // namespace

int main() {
    // Fixed-size Eigen construction must not silently become heap allocation.
    Eigen::internal::set_is_malloc_allowed(false);
    exact_conversion();
    byte_copy_fanout();
    std::printf("IMU transport: exact conversion and independent byte-copy fanout passed (%zu-byte record)\n",
                sizeof(ImuSample));
}
