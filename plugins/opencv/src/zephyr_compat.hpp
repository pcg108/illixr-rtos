#pragma once
// Load Zephyr macros before OpenCV, then remove the conflicting public macro.
#include <zephyr/kernel.h>
#ifdef EMPTY
#undef EMPTY
#endif
