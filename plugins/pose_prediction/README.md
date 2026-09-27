# RTOS pose prediction service

This plugin ports the RK4 predictor from `pcg108/ILLIXR` commit
`c9f4b6864d058211cb555a96bffa0e8c689001ca`. It registers an on-demand
service and creates no worker thread. Render and timewarp call
`get_pose_prediction().predict(consumer, target_dataset_ns)` independently.

The IMU integrator publishes a coherent `PredictionState` through
`prediction_state`. It contains the propagated state and two bias-corrected
IMU measurements, with the gyro pair converted for the predictor. The RTOS
integrator stores an Eigen Hamilton G-to-I orientation `q_H`; publication
conjugates it to the desktop predictor's Hamilton I-to-G/JPL G-to-I
representation. It also exports both gyro endpoints as `-R_H * corrected_gyro`,
where `R_H` is the current snapshot's rotation matrix. The shared pure helper
`src/prediction_adapter.hpp` implements this conversion for both production
publication and native tests. Integration itself is unchanged.

The rate conversion is necessary because the existing integrator evolves
`R_dot = R_H [gyro]x`, while the desktop predictor evolves
`R_dot = -[predictor_gyro]x R_H`. Conjugating the quaternion alone changes the
predicted rotation direction relative to the integrator; negating gyro alone
is insufficient when the orientation and rotation axis do not commute.
The native test compares the actual adapter's prediction with an independent
closed-form integrator continuation across 120 orientation/rate/horizon cases
(1–50 ms), and verifies each endpoint separately. It also verifies that the
quaternion-only and negation-only alternatives fail this continuity check.

The RK4 arithmetic, fixed +9.81 gravity subtraction, EuRoC-to-graphics axis
conversion, and first-valid orientation-offset ordering follow the desktop
source. Fixed-size Eigen matrices replace dynamic 3x3 temporaries. Prediction
uses a single snapshot for both its interval and kernel, eliminating the
upstream two-read inconsistency. A zero interval returns the state without
performing the desktop kernel's division by zero. Negative intervals are
invalid. Intervals above 50 ms freeze the caller's last valid prediction and
mark it stale; before any valid prediction that fallback is identity.

The existing RTOS integrator's gravity convention and trajectory drift remain
unchanged. Agreement with the copied desktop kernel is a runtime/algorithm
check, not a claim that the RTOS integrator matches desktop GTSAM integration
or the physical trajectory. The angular adapter ensures instantaneous and
constant-rate continuity; time-varying measurements retain the kernels'
different integration/interpolation schemes. It does not modify accelerometer
measurements, position, velocity, or the existing gravity inconsistency.

RK4 runs outside locks. A short Zephyr mutex protects the shared orientation
offset, separate render/timewarp freeze caches, placement counters, and a
fixed 8192-entry trace. No per-call allocation or logging occurs. After worker
shutdown, `dump()` emits `ILLIXR_PREDICTION`,
`ILLIXR_PREDICTION_PLACEMENT`, and `ILLIXR_PREDICTION_SUMMARY` records.
Trace overflow and invalid predictions make `validate()` fail.

Native checks:

```sh
# Build targets with the existing native CMake project, or compile the pure test:
g++ -std=c++17 -O2 -ffp-contract=off -DEIGEN_DONT_VECTORIZE \
  -DEIGEN_RUNTIME_NO_MALLOC -I/usr/include/eigen3 -pthread \
  tests/native/test_prediction.cpp -o /tmp/test_prediction
/tmp/test_prediction
python tests/native/test_prediction_reference.py

# This oracle compiles its pinned reference helper beside the output report:
tests/native/prediction_reference.py --trace console.log --output prediction-native.json
```

The reference helper contains separately retained desktop prediction and CPU
timewarp-transform code. It does not include the RTOS prediction or timewarp
math headers. Valid predictions must agree within 1 mm / 0.001 rad; transforms
use 1e-5 absolute-plus-relative tolerance. Raw RK4 state and policy/placement
accounting are also checked. Upstream licensing is retained in
`src/prediction-LICENSE.txt`.
