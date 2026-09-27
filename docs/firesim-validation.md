# FireSim Rocket validation

The application IMU transport update completed validation on 2026-09-26:
**all five workloads and all three platform preflights passed** on the original
Zephyr kernel and existing bitstreams/drivers. Both formerly incomplete cases
(dual pinned and quad scheduler-managed) now finish with all 501 IMUs at both
consumers and exact native pose agreement. See the
[implementation and results](imu-value-transport.md) and
[new comparison report](/scratch/prashanth_illixr_firesim_20260926/application-imu-values/comparison.md).
Estimator math, calibration, clocks, dataset, and queue capacities remain
unchanged. The following build and diagnostic history preserves the original
allocation-based firmware's results; those artifacts and records are retained.

The FireSim task workspace is
[`/scratch/prashanth_illixr_firesim_20260926`](/scratch/prashanth_illixr_firesim_20260926).
Its [consolidated comparison](/scratch/prashanth_illixr_firesim_20260926/comparison.md)
and [sequence status](/scratch/prashanth_illixr_firesim_20260926/status.json)
describe recorded progress. A generated bitstream, platform preflight, and full
ILLIXR workload are separate acceptance stages. Pending stages are not passes.

The first single-core build was stopped by the approved resource guard at
07:40:03 UTC on 2026-09-26, before hardware elaboration. Memory PSI `full avg10`
reached 0.72%, exceeding the 0.1% threshold. Available memory was 108,473 MiB,
the largest build process used 8,679 MiB RSS, and no OOM kill occurred. Owned
build processes were stopped, and the stop record and compilation outputs
were preserved. No bitstream or workload
result was produced by that attempt. The user subsequently authorized changing
PSI to warning-only and resuming the build sequence. The original stop record
is archived with the [resume authorization](/scratch/prashanth_illixr_firesim_20260926/control/resume-authorization.json).
The original Verilator workload was later stopped at the user's request after
the new FireSim matrix passed; its partial results remain incomplete and preserved
as described in [the Verilator status](rocket-results.md).

All three bitstreams subsequently built and passed hardware and routed-timing
checks. Initial single/dual preflights exhausted the cycle limit without firmware
startup output; the matching quad attempt was interrupted and preserved as
incomplete for diagnosis. The generated runtime configuration incorrectly set
the outstanding-request limits to 16, which truncated to zero in the models'
four-bit inputs and blocked target memory requests. The correction uses the
compiled hardware default of 10 for every configuration and adds range checks.
Bitstreams, firmware, and estimator code are unchanged by this correction.
An isolated quad-core host-driver diagnostic confirmed matching readback at
eight ELF/HTIF locations and actual firmware console output with the corrected
limit. This establishes loading and basic startup, not a full platform or
workload pass.
The following bounded diagnostic enabled DRAM initialization and the shared
host service cadence described below. It exited normally after 32,520,002
target cycles, with all four harts, successful atomics, monotonic clocks, the
expected timer/core ratio, and zero observed AXI error latches across 33 samples.
It remains diagnostic evidence; the production-driver matrix must pass
independently.

The first complete production single-core and scheduler-managed dual-core
workloads passed, producing 9 and 11 VIO poses respectively. Each delivered all
501 IMUs to both consumers and accounted for all 50 camera pairs. Native replay
matched the delivered sequences within the existing tolerances. Actual plugin
work counters show hart 0 for the single-core case and both harts for every
plugin in the scheduler-managed dual-core case. The production quad-core pinned
case also passed: 501 IMUs at both consumers, 13 cameras processed, 18 skipped,
19 dropped, and 11 VIO poses matching native replay. Actual processing and
publication remained exclusively on harts 0/1/2/3 for IMU/camera/VIO/integrator.
Its application runtime was 10.158454 target seconds; total case host time was
381.544 seconds, including platform setup.

The production dual-core pinned attempt stopped generating DRAM traffic after
approximately 0.64 simulated seconds and remained without a complete firmware
trace beyond 70 simulated seconds. It was deliberately interrupted to collect
bounded diagnostic evidence and is preserved as incomplete, not passed. A
separate host driver reads kernel and plugin state through serialized coherent
TSI transactions, using the original pinned ELF and bitstream. These sequential
reads perturb execution timing and are not atomic snapshots; saved thread
return addresses are not live program counters. Diagnostic results never count
as matrix acceptance. Evidence is under
[`diagnostics/pinned-stall`](/scratch/prashanth_illixr_firesim_20260926/diagnostics/pinned-stall).
Those five diagnostic snapshots showed continuing processing and did not
reproduce the original stall. The diagnostic stopped at its separate three
billion cycle cap, with no full-workload acceptance.

The production quad-core scheduler-managed case similarly stopped generating
DRAM traffic around 1.172–1.174 target seconds. It reached the full 100-billion
cycle limit without a final trace and remains incomplete. The resulting
production matrix has three accepted workloads and two incomplete workloads;
all three platform preflights passed. A separate quad diagnostic defers its
first coherent read until two billion target cycles (four target seconds),
after the observed stall point. Its preparation and evidence are under
[`diagnostics/quad-stall`](/scratch/prashanth_illixr_firesim_20260926/diagnostics/quad-stall).
The delayed-read quad diagnostic reproduced the identical traffic plateau
before any inspection reads. Both captures, at two and 2.5 billion cycles,
contained identical bytes across all 36 regions. They show 219 IMUs published
and integrated, 11 consumed by VIO, one completed camera update, no VIO poses,
and held scheduler and global mutex spinlocks. The latter is owned by the IMU
worker on hart 2; scheduler-lock owner metadata is zero. No FPU-flush IPI was
pending. This establishes persistent lack of progress during the captures,
but does not identify the blocked instruction or prove a particular lock
cycle. Inspection did not restore progress. The run ended at its separate
three-billion-cycle limit and remains diagnostic-only. See the
[assessment](/scratch/prashanth_illixr_firesim_20260926/diagnostics/quad-stall/quad-scheduler-late-state-1-assessment.md).
A following bounded quad stack capture authenticated a preserved null-store
exception in `z_thread_prio_set`: the IMU worker's malloc mutex contention was
boosting VIO priority from 5 to 3 when removal of its null-linked scheduler node
faulted. Independently matched call frames and assertion arguments identify a
scheduler spinlock unlock-validation assertion on idle hart 1. These are saved
exception/call-chain findings, not sampled live PCs. A source audit found a
plausible timeslice/remote-priority race and a relevant upstream helper change;
neither the observed cross-core event order nor a corrective patch has yet
been validated. See the
[authenticated frame evidence](/scratch/prashanth_illixr_firesim_20260926/diagnostics/quad-stall/stacks/authenticated-exception-and-assert.md)
and [source audit](/scratch/prashanth_illixr_firesim_20260926/diagnostics/quad-stall/source-rootcause/README.md).

A delayed-read dual pinned diagnostic also reproduced its exact production
traffic plateau at 0.640 target seconds before inspection. Both complete
captures were identical: 113 IMUs published/integrated, one consumed by VIO,
and no poses. Scheduler-lock storage held invalid pointer/code values matching
logging data; its write origin and any shared cause with quad remain unproven.
It stopped at its diagnostic cycle cap with verified cleanup, and does not
change the incomplete production result. See the
[dual diagnostic review](/scratch/prashanth_illixr_firesim_20260926/diagnostics/pinned-stall/late/independent-review.md).

An initial diagnostic compiler check omitted its guard ownership tag and
created a setup-error latch. Review found a 116,599 MiB available-memory
preflight, no threshold breach, and completed child cleanup. The exact error
record was archived for a deliberate corrected retry. The guard now rejects
missing tags before creating a child or resource latch, preserves existing
latches, and retains every memory/RSS/OOM limit. This setup failure did not
change any accepted firmware, driver, or bitstream.

## Platform and inputs

Three isolated FireSim configurations adapt the existing single-, dual-, and
quad-core Rocket machines to the local Alveo U250. Target clocks remain 500 MHz,
CLINT remains 500 kHz, and RAM remains 256 MiB at `0x80000000`. The requested
physical FPGA host clock is 30 MHz with the `NORETIMING` build strategy. FireSim
uses FASED latency-bandwidth memory timing, with 30 target-cycle read/write
latency and 10 outstanding requests of each kind; the Verilator runs use
DRAMSim2, so their memory timing is not equivalent.

The existing Rocket ELF files and embedded 50-pair / 501-IMU dataset are reused
unchanged. TSI/loadmem loads each bare-metal ELF, and HTIF carries console output
and completion. No Linux root filesystem is used. Estimator math,
initialization, calibration, sample pacing, queues, and affinity mappings retain
their existing behavior. Known physical trajectory drift remains unresolved.

Host HTIF service uses `+fesvr-step-size=10000`, `+idle-counts=1`, and
`+fesvr-wait-ticks=8` for every configuration. The old default service period of
2,004,765 target cycles required at least 113/154 billion cycles to emit just
the saved single/dual Spike trace payloads, exceeding the 100-billion-cycle
limit. These runtime settings change host service cadence; the target clock
and CLINT frequency remain unchanged. The startup wait of 80,000 target cycles
exceeds the generated reset bridge's maximum of 1,023 cycles. Runtime manifests
and driver bundles record and validate the same memory and host service settings.

Every case also enables FireSim's existing `zero_out_dram` option before ELF
loading. This initializes the modeled 256 MiB through the loadmem bridge. The
actual U250 DDR controller uses ECC; each ELF leaves 48 bytes of its final HTIF
cache line unwritten. The initial diagnostic latched an AXI read error, while
the initialized run completed with zero observed errors. Firmware and embedded
dataset bytes remain unchanged. Case fingerprints include DRAM initialization
and the memory sampling interval.

The matrix contains three platform preflights followed by five workload cases:
single-core baseline, dual scheduler-managed, dual pinned, quad
scheduler-managed, and quad pinned. Execution is grouped by FPGA image, so each
image is built, checked, programmed, and tested before proceeding to the next.

## Isolation and resource controls

All source and build outputs are under the scratch workspace. Working source
files, including existing required local changes, were copied from Chipyard.
Git metadata is independent; immutable Git objects are read through alternates
to the original stores. Two absolute source symlinks were retargeted into the
snapshot. SDK/tool installations are reused. The original Chipyard checkout,
traffic-generator configuration, stopped Radiance projects, and ongoing
Verilator matrix are preserved.

Builds use eight allowed CPUs at nice 10, up to eight build/Vivado threads, and
a 16 GiB Java heap. The guard stops only owned build processes when available
memory falls below 48 GiB, a process reaches 64 GiB RSS, or the OOM kill counter
increases. Memory PSI (`some avg10 >= 1`, `full avg10 >= 0.1`) is logged as a
warning only, following the user's explicit policy correction. These system-wide
stall readings alone cannot identify which workload caused pressure. A hard
resource stop creates a persistent latch; the sequence never clears it or
retries automatically. Ordinary setup/build failures are preserved and require
a deliberate retry after the cause is corrected.

## Execution and checks

The durable coordinator is `control/coordinator.py` in the scratch workspace.
It invokes `run-build.py`, `finalize-hardware.py`, driver packaging, and
[`run_firesim_matrix.py`](/home/prashanth/illixr_example_zephyr/scripts/run_firesim_matrix.py)
in that order for each core count. The runner supports hardware-free
`--prepare-only`, `--report-only`, and `--package-drivers` stages. A hardware run
requires completed hardware and routed-timing verification plus unchanged
artifact hashes and an idle board.
The coordinator's `--cores 4` option can continue only the remaining quad-core
configuration while retaining the single/dual records. Overall success still
requires all three configurations to pass; selecting a subset cannot hide an
incomplete pinned case. Start the report observer after the coordinator has
written a running state.

Each run samples the FASED memory interface every million target cycles and
preserves the CSV evidence. Observed nonzero AXI error latches reject a result;
missing or insufficient samples leave it incomplete. The existing driver does
not take an exit sample, leaving the final sampling interval uncovered. The
generated write-error latch also captures read-response bits when a write error
occurs, so a zero write-error latch alone cannot prove absence of write errors.

The idle check uses the XDMA module reference count, open device handles, and
programmer/simulator process inventory. Cleanup targets only processes owned by
the current attempt. An unsuccessful infrastructure setup places a persistent
hold on further FPGA operations until programming completion and board idleness
can be verified.

Hardware verification checks generated DTS, actual clock-bridge frequencies,
hart IDs, ISA/FPU, memory/interrupt mappings, boot-ROM identity, TSI/FASED
connectivity, and ELF compatibility. Routed timing must pass setup, hold, and
pulse-width checks; routing must be complete without routing errors.

Preflights require masks `0x1`, `0x3`, and `0xF`, monotonic clocks, shared atomic
operations, the expected core/timer ratio, and normal HTIF exit. Workloads
require all IMUs at both consumers, complete camera accounting, initialized
finite VIO poses, normalized quaternions, ordered timestamps, independent
consumer progress, complete traces, and valid plugin processing/publication
placement records. Pinned runs must obey their assigned harts.

Each successful delivered input sequence is replayed through the existing
native port-estimator harness, with 1 mm / 0.001 rad tolerances. This is runtime
equivalence, not agreement with desktop OpenVINS or proof of physical accuracy.
The per-case host watchdog is 24 hours and the target-cycle limit is 100 billion.
Timeouts and interruptions are incomplete results. A successful manager command
without a complete accepted firmware trace does not pass.

The isolated FireSim host driver also fixes `+max-cycles` parsing: its previous
32-bit `atoi` conversion truncated the 100-billion-cycle limit. The replacement
parses an unsigned 64-bit value and rejects malformed or overflowing input.
Boundary tests cover the requested limit. This changes the host driver only;
the Rocket firmware and estimator remain unchanged.

## Evidence

The workspace preserves configuration/source fingerprints, build attempts and
memory telemetry, bitstream/driver/firmware/boot-ROM hashes, generated hardware,
timing/utilization reports, raw and normalized consoles, native output,
placement counters, per-case analysis, and comparison reports. The consolidated
report combines the existing Spike and Verilator evidence with newly completed
FireSim cases. Build completion and correctness must be read from those saved
records rather than inferred from a live process.
