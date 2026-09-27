#pragma once
#include "../openvins/openvins_queues.hpp"

// Independent IMU subscriber queue; each record is copied into the queue.
extern struct k_msgq imu_integrator_queue;  // ILLIXR::ImuSample by value
