#pragma once
#include <zephyr/kernel.h>
// One startup barrier only. Sensor deadlines are independent thereafter.
extern struct k_sem stoplight_ready;
extern struct k_sem stoplight_start;
