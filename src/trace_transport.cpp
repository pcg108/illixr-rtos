#include "trace_output.hpp"
#include <zephyr/kernel.h>

extern "C" {
extern volatile std::uint64_t tohost, fromhost;
extern struct k_mutex htif_lock;
}

bool ILLIXR::trace_output::transport_write(const char *data, std::size_t size) {
  // FESVR syscall 64 (write). Runtime TSI memory reads are coherent target
  // accesses, not the DRAM-only loadmem path used to load the ELF.
  alignas(64) static volatile std::uint64_t request[8];
  k_mutex_lock(&htif_lock, K_FOREVER);
  bool ok = true;
  while (size && ok) {
    const auto deadline = k_uptime_get() + 5000;
    while (tohost && k_uptime_get() < deadline) k_yield();
    if (tohost) { ok = false; break; }
    fromhost = 0;
    request[0] = 64; request[1] = 1;
    request[2] = reinterpret_cast<std::uintptr_t>(data); request[3] = size;
    for (unsigned i = 4; i < 8; ++i) request[i] = 0;
    __asm__ volatile("fence rw,rw" ::: "memory");
    tohost = reinterpret_cast<std::uintptr_t>(request);
    while (!fromhost && k_uptime_get() < deadline) k_yield();
    __asm__ volatile("fence rw,rw" ::: "memory");
    const auto written = static_cast<std::int64_t>(request[0]);
    ok = fromhost == 1 && written > 0 && static_cast<std::uint64_t>(written) <= size;
    fromhost = 0;
    if (ok) { data += written; size -= static_cast<std::size_t>(written); }
  }
  k_mutex_unlock(&htif_lock);
  return ok;
}
