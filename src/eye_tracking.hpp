#pragma once
#include "latest_value.hpp"
#include <cstdint>
namespace ILLIXR::eye_tracking {
struct Image {
 const int8_t *pixels{};
 uint64_t sequence{};
 int64_t scheduled_ns{},published_ns{};
 unsigned publication_hart{};
};
struct Result {
 bool valid{};
 double x{},y{};
 uint64_t image_sequence{},inference_id{},output_hash{};
 int64_t image_ns{},snapshot_begin_ns{},request_ns{},start_ns{},completion_ns{},publication_ns{};
 unsigned accelerator_hart{},publication_hart{};
};
inline LatestValue<Image> latest_image;
void initialize();
void shutdown();
void publish(Image image);
// A retained snapshot; never starts or waits for inference. Invalid until the
// first result is published. The caller may observe the same result repeatedly.
Result latest();
Result read_latest(uint64_t warp_id, uint64_t display_slot);
void dump();
const int8_t *sample();
}
