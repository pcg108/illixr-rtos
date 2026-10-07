Corrected Rocket: ILLIXR RTOS fence matrix
==========================================

Status: complete; 12/12 audited workloads; 4/4 accepted preflights.

All cases use 50 stereo pairs, 501 IMUs, scheduler-managed placement, 10 kHz ticks,
the modeled 1 GHz CPU / 1 MHz timer operating point, and batched trace export.
Operation-31-only and all-64-boundary drains use same-layout paired full-workload ELFs.
Eye inference is diagnostic and capped at 32; reaching the cap is not steady-state
eye-tracker throughput or a claim of fresh eye estimates until end of replay.

.. list-table:: Audited full workloads
   :header-rows: 1

   * - Hardware / VIO backend / fences
     - VIO / eye outputs
     - Mean eye ms
     - App / export s
     - Render / warp misses
     - Fresh presentations
   * - dual-single / openblas_rvv / op31
     - 11 / 17
     - 162.527
     - 6.050015 / 1.409519
     - 0 / 0
     - 130
   * - dual-single / openblas_rvv / all64
     - 11 / 17
     - 163.028
     - 6.041715 / 1.354668
     - 0 / 0
     - 130
   * - dual-single / openblas_gemmini_fp32 / op31
     - 11 / 18
     - 147.632
     - 5.366684 / 1.332876
     - 0 / 0
     - 125
   * - dual-single / openblas_gemmini_fp32 / all64
     - 10 / 18
     - 149.701
     - 6.733384 / 1.555666
     - 0 / 0
     - 107
   * - dual-quad / openblas_rvv / op31
     - 16 / 32
     - 60.312
     - 5.500101 / 1.688284
     - 0 / 0
     - 202
   * - dual-quad / openblas_rvv / all64
     - 16 / 32
     - 60.824
     - 5.525104 / 1.658822
     - 0 / 0
     - 204
   * - dual-quad / openblas_gemmini_fp32 / op31
     - 15 / 32
     - 60.302
     - 5.700081 / 1.695402
     - 0 / 0
     - 201
   * - dual-quad / openblas_gemmini_fp32 / all64
     - 15 / 32
     - 60.632
     - 5.616692 / 1.696042
     - 0 / 0
     - 200
   * - int8-single / openblas_rvv / op31
     - 11 / 17
     - 162.527
     - 6.050015 / 1.421559
     - 0 / 0
     - 130
   * - int8-single / openblas_rvv / all64
     - 11 / 17
     - 163.028
     - 6.041715 / 1.435868
     - 0 / 0
     - 130
   * - int8-quad / openblas_rvv / op31
     - 16 / 32
     - 60.312
     - 5.500101 / 1.735114
     - 0 / 0
     - 202
   * - int8-quad / openblas_rvv / all64
     - 16 / 32
     - 60.824
     - 5.525104 / 1.742492
     - 0 / 0
     - 204

Interpretation
--------------

Each passing case has normal HTIF exit, all 501 IMUs delivered to both consumers,
accounting for all 50 camera pairs, exact repeated-image eye outputs, initialized
VIO, native pose agreement within 1 mm / 0.001 rad, prediction/transform comparisons,
valid frame/presentation accounting, complete traces, and fresh on-time presentation.
The audit rechecks console records and firmware/hardware hashes. Actual plugin hart
counters are in completion-audit.json; online-hart count alone is not placement evidence.

These are single paired trials. Eye latency includes preemption. Fence timing can
alter delivered camera sequences and scheduler interleavings, so total-application
differences do not isolate the cost of the fence itself. Physical trajectory accuracy
is separate from native runtime equivalence. FASED memory timing is not DRAMSim2.

Preserved failure and recovery
------------------------------

The original INT8-only preflight lacked the diagnostic operation-31 drain and failed
on its second inference. It remains a recorded failure. A controlled same-layout
unfenced diagnostic reproduced the known operation-32 tensor on inference 7;
the fenced ELF differed by one drain-mask byte and passed 32/32 with interrupts enabled.
The 149-byte differing tensor capture is identical to the earlier failure on old hardware.
Completed inputs and producer output matched the reference; direct DMA read timing
has not been captured. This supports the known RITNet ordering explanation separately
from the corrected Rocket cache coherence defect. No production fence or RTL change
was made during this recovery. INT8-only preflights now explicitly test 32 fenced
inferences, timer preemption, vector context, and hart-zero accelerator ownership.

Evidence
--------

* completion-audit.json: per-case metrics, placement, hashes, remaining cases.
* comparison.json: full analyzer summaries, native comparisons, queues and timing.
* preflight-debug/control-comparison.json: same-layout control identity.
* preflight-debug/controls/runtime/: complete control traces and captured tensor.
* preflight-debug/before-resume/: preserved original stopped matrix state.
* control/runtime-state.json: accepted runs and retained failed attempt.
* hardware-*/control/hardware-*.json: timing, boot ROM, RTL and FPGA provenance.
