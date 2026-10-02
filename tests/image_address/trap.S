/* Isolated architectural regression. Caller masks interrupts. Restore mtvec,
 * mstatus, fcsr, and callee-saved registers before returning to Zephyr. */
.text
.balign 4
.global illixr_faulting_fdiv
.type illixr_faulting_fdiv, @function
illixr_faulting_fdiv:
    addi sp, sp, -80
    sd ra, 0(sp)
    sd s0, 8(sp)
    fsd fs0, 16(sp)
    fsd fs1, 24(sp)
    csrr t0, mtvec
    sd t0, 32(sp)
    frcsr t0
    sd t0, 40(sp)
    csrr t0, mstatus
    sd t0, 48(sp)
    mv s0, a0

    /* Leave a recognizable preceding divide in the FP operand pipeline. */
    li t0, 0x3ff0000000000000
    fmv.d.x fs0, t0
    fmv.d.x fs1, zero
    fdiv.d ft0, fs0, fs1
    frflags t0

    li t0, 0x401c000000000000 /* 7.0 */
    fmv.d.x fs0, t0
    li t0, 0x4000000000000000 /* 2.0 */
    fmv.d.x fs1, t0
    li t0, 0x4022000000000000 /* destination sentinel: 9.0 */
    fmv.d.x fa0, t0
    fsd fa0, 24(s0)
    frflags t0

    la t0, .Limage_fdiv_trap
    csrw mtvec, t0
    li t0, 0x6000
    csrc mstatus, t0
.global illixr_rejected_fdiv_pc
illixr_rejected_fdiv_pc:
    fdiv.d fa0, fs0, fs1
    /* Handler skips the rejected instruction, so fa0 must still be 9.0. */
    fsd fa0, 40(s0)
    ld t0, 32(sp)
    csrw mtvec, t0
    fld fs0, 16(sp)
    fld fs1, 24(sp)
    ld t0, 40(sp)
    fscsr t0
    ld t0, 48(sp)
    csrw mstatus, t0
    ld s0, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 80
    ret

.balign 4
.Limage_fdiv_trap:
    csrr t0, mcause
    sd t0, 0(s0)
    csrr t0, mepc
    sd t0, 8(s0)
    csrr t0, mtval
    sd t0, 16(s0)
    li t0, 0x6000
    csrs mstatus, t0
    li t0, 256
1:  addi t0, t0, -1
    bnez t0, 1b
    fsd fa0, 32(s0)
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0
    mret
.size illixr_faulting_fdiv, .-illixr_faulting_fdiv
