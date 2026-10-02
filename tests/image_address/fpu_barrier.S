/* Diagnostic adapter only. FCSR reads serialize outstanding FP operations on
 * Shuttle. Compare identical arithmetic with this barrier disabled/enabled
 * before Zephyr snapshots FS and relinquishes the floating-point context. */
.text
.align 2
.global __wrap_z_riscv_fpu_enter_exc
.type __wrap_z_riscv_fpu_enter_exc, @function
__wrap_z_riscv_fpu_enter_exc:
    la t0, illixr_fpu_barrier_enabled
    lw t0, 0(t0)
    beqz t0, 1f
    csrr t0, mstatus
    li t1, 0x6000
    and t0, t0, t1
    beqz t0, 1f
    frflags t0
1:  tail __real_z_riscv_fpu_enter_exc
.size __wrap_z_riscv_fpu_enter_exc, .-__wrap_z_riscv_fpu_enter_exc
