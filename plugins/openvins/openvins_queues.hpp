#pragma once
#include <zephyr/kernel.h>
#include "../../src/imu_sample.hpp"

// IMU queues own independent scalar copies; no heap allocation is involved.
// Camera pointers retain cv::Mat ownership: a successful put transfers that
// ownership to the consumer; a failed put does not.
#ifndef ILLIXR_IMU_QUEUE_CAPACITY
#define ILLIXR_IMU_QUEUE_CAPACITY 4096
#endif
#ifndef ILLIXR_CAM_QUEUE_CAPACITY
#define ILLIXR_CAM_QUEUE_CAPACITY 8
#endif

static_assert(ILLIXR_IMU_QUEUE_CAPACITY > 0, "IMU queue must be nonempty");
static_assert(ILLIXR_CAM_QUEUE_CAPACITY > 0, "camera queue must be nonempty");

extern struct k_msgq openvins_imu_queue;  // ILLIXR::ImuSample by value
extern struct k_msgq openvins_cam_queue;  // CamMsg*
