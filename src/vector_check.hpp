#pragma once
namespace ILLIXR::vector_check {
#ifdef CONFIG_RISCV_ISA_EXT_V
bool run();
#else
inline bool run() { return true; }
#endif
}
