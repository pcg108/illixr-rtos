#pragma once
// Deferred printf records. Only used after workers join; JSON formatting is
// performed by the host. Wire integers and IEEE doubles are little-endian.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#ifndef ILLIXR_BATCH_TRACE
#define ILLIXR_BATCH_TRACE 0
#endif

namespace ILLIXR::trace_output {
bool transport_write(const char *, std::size_t);
inline constexpr std::size_t capacity = 16384, dictionary_capacity = 256;
inline unsigned char buffer[capacity];
inline char encoded[capacity * 4 / 3 + 128];
inline const char *dictionary[dictionary_capacity];
inline std::size_t used{}, dictionary_size{};
inline std::uint64_t sequence{}, total_bytes{}, calls{};
inline bool active{}, good{true};

inline std::uint32_t crc32(const unsigned char *data, std::size_t size) {
  std::uint32_t crc = ~0u;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
inline void flush() {
  if (!used || !good) return;
  const auto checksum = crc32(buffer, used);
  auto n = static_cast<std::size_t>(std::snprintf(encoded, sizeof(encoded),
      "ILLIXR_BATCH_V1 %llu %zu %08x ", static_cast<unsigned long long>(sequence), used, checksum));
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (std::size_t i = 0; i < used; i += 3) {
    unsigned word = unsigned(buffer[i]) << 16;
    if (i + 1 < used) word |= unsigned(buffer[i + 1]) << 8;
    if (i + 2 < used) word |= buffer[i + 2];
    encoded[n++] = alphabet[(word >> 18) & 63]; encoded[n++] = alphabet[(word >> 12) & 63];
    encoded[n++] = i + 1 < used ? alphabet[(word >> 6) & 63] : '=';
    encoded[n++] = i + 2 < used ? alphabet[word & 63] : '=';
  }
  encoded[n++] = '\n';
  good = transport_write(encoded, n);
  total_bytes += used; ++sequence; used = 0;
}
inline void byte(unsigned value) {
  if (!good) return;
  if (used == capacity) flush();
  if (good) buffer[used++] = static_cast<unsigned char>(value);
}
inline void integer(std::uint64_t value, unsigned width) {
  for (unsigned i = 0; i < width; ++i) byte(unsigned(value >> (8 * i)) & 255);
}
inline void string(const char *value) {
  if (!value) { good = false; return; }
  const auto size = std::strlen(value);
  if (size > 4096) { good = false; return; }
  integer(size, 2);
  for (std::size_t i = 0; i < size; ++i) byte(value[i]);
}
inline void argument(const char *value) { byte('s'); string(value); }
template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
inline void argument(T value) {
  byte(std::is_signed_v<T> ? 'i' : 'u'); integer(static_cast<std::uint64_t>(value), 8);
}
template<class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
inline void argument(T value) {
  const double scalar = value;
  std::uint64_t bits;
  static_assert(sizeof(bits) == sizeof(scalar));
  std::memcpy(&bits, &scalar, sizeof(bits)); byte('d'); integer(bits, 8);
}
template<class... Args> inline void print(const char *format, Args... args) {
  if (!active) { std::printf(format, args...); return; }
  if (!good) return;
  static_assert(sizeof...(args) < 256);
  std::size_t id = 0;
  for (; id < dictionary_size; ++id)
    if (dictionary[id] == format || !std::strcmp(dictionary[id], format)) break;
  if (id == dictionary_size) {
    if (dictionary_size == dictionary_capacity) { good = false; return; }
    dictionary[dictionary_size++] = format;
    byte(1); integer(id, 2); string(format);
  }
  byte(2); integer(id, 2); byte(sizeof...(args)); (argument(args), ...); ++calls;
}
inline void begin() {
  if constexpr (ILLIXR_BATCH_TRACE) {
    used = dictionary_size = sequence = total_bytes = calls = 0; good = true;
    std::printf("ILLIXR_BATCH_BEGIN 1\n"); std::fflush(stdout); active = true;
  }
}
inline bool finish() {
  if (!active) return true;
  flush(); active = false;
  std::printf("ILLIXR_BATCH_END %llu %llu %llu %s\n",
      static_cast<unsigned long long>(sequence), static_cast<unsigned long long>(total_bytes),
      static_cast<unsigned long long>(calls), good ? "ok" : "error");
  std::fflush(stdout);
  return good;
}
} // namespace ILLIXR::trace_output
