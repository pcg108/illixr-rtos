# Batched trace export

`ILLIXR_BATCH_TRACE=ON` is the firmware default. After all workers join, the
existing dump functions emit deferred formatting records into a fixed 16 KiB
buffer. Each record carries a format-dictionary ID and typed integer, string,
or IEEE-754 double arguments. The host reconstructs the existing trace text,
including JSON, before the estimator and prediction references run. No trace
collection, estimator math, queue capacity, or render/timewarp schedule changes.

The total fixed export workspace is about 40 KiB, including the ASCII envelope
and 256-entry format dictionary. No dynamically growing firmware export buffer
is used. Format strings are static application literals. Numeric formatting
retains the existing 9- and 17-significant-digit conventions.

## Transport and integrity

Transport version 1 is independent of GPU trace version 2:

1. A short `ILLIXR_BATCH_BEGIN 1` console record starts export.
2. Each `ILLIXR_BATCH_V1` envelope contains a consecutive sequence number,
   byte length, CRC32, and base64-encoded binary payload. Base64 protects the
   payload from PTY newline/control-character transformations.
3. `ILLIXR_BATCH_END` records the batch count, binary byte count, formatting-call
   count, and success/failure status.

The firmware issues HTIF device-0 syscall 64 (`write`) for a complete envelope,
rather than issuing one console transaction per character. FESVR fetches target
memory in chunks and writes it to its stdout. Runtime reads follow the existing
TSI target-memory path; this does not use the DRAM-only loadmem shortcut that
could bypass dirty cache data. Fences and the HTIF mutex protect request handoff;
the buffer is reused only after acknowledgement. Partial host writes are retried,
and a failed or timed-out transfer cannot produce an accepted complete trace.

The accepted FireSim bitstream, driver, memory model, 10,000-cycle host-service
interval, modeled 1 GHz / 1 MHz clock settings, and 10 kHz Zephyr ticks remain
unchanged. Startup checks and the small final result/exit messages still use
the normal character console. Spike and Verilator use the same batch protocol.

## Host decoding

`scripts/trace_batches.py` validates packet ordering, checksums, dictionary
definitions, typed arguments, protocol version, size bounds, and final counts.
It rejects missing/duplicate/reordered packets, malformed records, unsupported
format specifiers, and incomplete exports. Legacy console traces pass through
unchanged. The reconstructed `console.log` remains the input to existing analyzers
and independent native references.

FireSim preserves `uartlog.raw`; Spike and Verilator preserve
`console.batched.log`. `trace-transfer.json` records the transfer size and packet
counts. A formatting call can be a fragment of a logical trace record, so its
count differs from the analyzer's sensor, pose, and GPU-event counts.

To decode a standalone captured console:

```sh
python3 scripts/trace_batches.py /path/to/console.log
```

To build a diagnostic firmware with the previous export path, pass
`-DILLIXR_BATCH_TRACE=OFF` to the build script. Existing saved ELFs are unchanged.

Application time ends before export. `trace_export_ns` includes serialization,
transmission, and waiting for acknowledgements; host JSON formatting occurs
after simulator exit and is measured separately as `host_decode_seconds`.
FireSim's recorded host case time includes deployment and target execution;
it is captured before host decoding and native analysis.

## Validation and measured improvement

Dual- and quad-core Spike, quad-core FireSim preflight, and the complete quad-core
FireSim GPU workload all passed. The native suite passed 155 tests, including
exact native-printf reconstruction, packet corruption/incompleteness rejection,
raw-capture preservation, and rejection at the FireSim analyzer boundary.

| Quad-core FireSim metric | Previous character export | Batched export |
| --- | ---: | ---: |
| Modeled application seconds | 7.991723 | 7.983355 |
| Modeled export seconds | 42.831100 | 1.270321 |
| Host case seconds, including deployment | 1,998.735 | 498.568 |
| Total target cycles | 50,903,900,002 | 9,334,910,002 |
| VIO poses | 16 | 16 |
| Render deadline misses / completions | 0 / 958 | 0 / 957 |
| Timewarp deadline misses / completions | 0 / 958 | 0 / 957 |
| Fresh on-time presentations | 209 | 209 |

Export used **33.7× less modeled time**, and the full host case was **4.0× faster**
(33.3 minutes to 8.3 minutes). The firmware sent 2,772,238 binary bytes in 170
batches. ASCII framing produced about 3.71 MB on the wire, reconstructed into
about 4.10 MB of console text in 0.99 host seconds. Most of the gain comes from
eliminating character-by-character host handshakes and moving formatting off the
target, rather than from reducing the transferred byte count.

All 501 IMUs reached both consumers; 18 camera pairs were processed and 32
dropped. All six workers executed on all four harts. Native VIO replay matched
all 16 poses with zero reported error, and independent prediction/transform
comparisons passed. The delivered sequence differs from the previous firmware
run, despite identical sample counts; each run is checked against its own native
replay. These are individual runs, and small scheduling differences remain.
Physical trajectory accuracy is unchanged in scope and is not established by
runtime equivalence.

[Detailed comparison](/scratch/prashanth_illixr_firesim_20260926/batch-traces-20260927T071032Z/results/trace-transfer-comparison.md)
includes both Spike results, transfer sizes, deadlines, and links to all logs.
Prior baseline artifacts are preserved; the accepted hardware and driver hashes
are unchanged. Build commands and source/configuration provenance are saved
under the same scratch directory.
