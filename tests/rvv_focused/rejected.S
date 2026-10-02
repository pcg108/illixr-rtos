/* Caller masks IRQs. Private trap skips one rejected 32-bit instruction. */
.text
.option push
.option arch, +v
.global illixr_rejected_vector
.type illixr_rejected_vector,@function
/* a0: result {cause,pc,tval,expected_pc,before[8],after[8]}, a1: mode. */
illixr_rejected_vector:
    addi sp, sp, -64
    sd s0, 0(sp)
    sd ra, 8(sp)
    csrr t0, mtvec
    sd t0, 16(sp)
    csrr t0, mstatus
    sd t0, 24(sp)
    csrr t0, fcsr
    sd t0, 32(sp)
    mv s0, a0
    li t0, 0x6600
    csrs mstatus, t0
    csrw vstart, zero
    li t0, 8
    vsetvli zero, t0, e64, m2, tu, mu
    li t0, 9
    fcvt.d.w ft0, t0
    vfmv.v.f v8, ft0
    li t0, 1
    fcvt.d.w ft0, t0
    vfmv.v.f v10, ft0
    addi t0, s0, 32
    vse64.v v8, (t0)
    fence rw,rw
    la t0, .Lvector_trap
    csrw mtvec, t0
    beqz a1, .Lfs_off
    li t0, 1
    beq a1, t0, .Lvs_off
    li t0, 2
    beq a1, t0, .Lboth_off
    la t0, .Lrejected_vv
    sd t0, 24(s0)
    li t0, 0x600
    csrc mstatus, t0
.Lrejected_vv:
    vfadd.vv v8, v8, v10
    j .Lvector_done
.Lfs_off:
    la t0, .Lrejected_fs
    sd t0, 24(s0)
    li t0, 0x6000
    csrc mstatus, t0
.Lrejected_fs:
    vfadd.vf v8, v8, ft0
    j .Lvector_done
.Lvs_off:
    la t0, .Lrejected_vs
    sd t0, 24(s0)
    li t0, 0x600
    csrc mstatus, t0
.Lrejected_vs:
    vfadd.vf v8, v8, ft0
    j .Lvector_done
.Lboth_off:
    la t0, .Lrejected_both
    sd t0, 24(s0)
    li t0, 0x6600
    csrc mstatus, t0
.Lrejected_both:
    vfadd.vf v8, v8, ft0
.Lvector_done:
    addi t0, s0, 96
    vse64.v v8, (t0)
    ld t0, 16(sp)
    csrw mtvec, t0
    ld t0, 32(sp)
    csrw fcsr, t0
    ld t0, 24(sp)
    csrw mstatus, t0
    ld s0, 0(sp)
    ld ra, 8(sp)
    addi sp, sp, 64
    ret
.balign 4
.Lvector_trap:
    csrr t0, mcause
    sd t0, 0(s0)
    csrr t0, mepc
    sd t0, 8(s0)
    csrr t0, mtval
    sd t0, 16(s0)
    li t0, 0x6600
    csrs mstatus, t0
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0
    mret
.size illixr_rejected_vector, .-illixr_rejected_vector
.option pop
