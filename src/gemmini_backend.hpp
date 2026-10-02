#pragma once
namespace ILLIXR::gemmini_backend {
void initialize();
void dump(const char *phase, bool reset = false);
bool self_test(unsigned &checks);
void shutdown();
}
