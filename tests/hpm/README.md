# Focused Rocket HPM regression

Run `python3 scripts/run_hpm_rtl.py --help` from the repository root. Supply the
newly elaborated target `.sfc.fir`, its matching compiler jar (`--classpath`), the
Chipyard checkout containing `tests/rtl_fixes/common.py`, and an empty `--work`
directory. Use `--jobs 4` or fewer and the repository resource guard.

The runner lowers the complete generated circuit, selects the complete Rocket
module dependency closure, and adds output-only observation ports. It does not
rewrite functional RTL. A small bare-metal instruction stream programs the
selectors through actual CSR instructions. External frontend, cache, and FPU
interfaces provide bubbles, backpressure, delayed load responses, and decode.

Eight seeds each run for 12,000 cycles. Checks cover cycle/retirement increments,
all thirteen event gates and registered counter inputs, counter updates, and
sustained instruction progress. Acceptance requires positive coverage for every
event, zero errors, and a completion record. Cache acquire pulses are external
protocol stimuli, not a measurement of a full cache. No Zephyr or ILLIXR workload
runs in this test; scheduler attribution is checked separately in firmware.

`--lowering-cache` may reuse an earlier lowering only after verifying source,
compiler, and generated-RTL hashes from `lowering-cache.json`. Every invocation
builds its observation harness and writes separate execution/provenance outputs.

The initial accepted HPM campaign produced 1,513,588 checks, zero failures, and
positive coverage for all thirteen events. FPGA workload acceptance remains
separate and must not be inferred from this focused test.
