/* Single-hart diagnostic. Caller holds irq_lock. One CLINT timer interrupt
 * uses a private handler: no Zephyr exception code or vector context switch. */
.text
.option push
.option arch, +v
.global illixr_native_vector_irq
.type illixr_native_vector_irq,@function
/* a0=result; layout documented in the C++ caller. */
illixr_native_vector_irq:
    addi sp, sp, -64
    sd s0, 0(sp)
    sd ra, 8(sp)
    csrr t0, mtvec
    sd t0, 16(sp)
    csrr t0, mstatus
    sd t0, 24(sp)
    csrr t0, mie
    sd t0, 32(sp)
    li t0, 0x02004000
    ld t1, 0(t0)
    sd t1, 40(sp)
    csrr t0, fcsr
    sd t0, 48(sp)
    mv s0, a0
    csrw vstart, zero
    li t0, 8
    vsetvli zero, t0, e64, m2, tu, mu
    vmv.v.i v8, 0
    li t0, 1
    fcvt.d.w ft0, t0
    la t0, .Lnative_add
    sd t0, 24(s0)
    la t0, .Lnative_dec
    sd t0, 32(s0)
    la t0, .Lnative_branch
    sd t0, 40(s0)
    la t0, .Lnative_trap
    csrw mtvec, t0
    li t0, 0x80
    csrw mie, t0
    li t0, 0x0200bff8
    ld t1, 0(t0)
    addi t1, t1, 10
    li t0, 0x02004000
    sd t1, 0(t0)
    fence iorw,iorw
    li a2, 20000
    csrsi mstatus, 8
.Lnative_add:
    vfadd.vf v8, v8, ft0
.Lnative_dec:
    addi a2, a2, -1
.Lnative_branch:
    bnez a2, .Lnative_add
    csrci mstatus, 8
    addi t0, s0, 128
    vse64.v v8, (t0)
    ld t1, 40(sp)
    li t0, 0x02004000
    sd t1, 0(t0)
    fence iorw,iorw
    ld t0, 32(sp)
    csrw mie, t0
    ld t0, 16(sp)
    csrw mtvec, t0
    ld t0, 48(sp)
    csrw fcsr, t0
    ld t0, 24(sp)
    csrw mstatus, t0
    ld s0, 0(sp)
    ld ra, 8(sp)
    addi sp, sp, 64
    ret
.balign 4
.Lnative_trap:
    addi sp, sp, -32
    sd t0, 0(sp)
    sd t1, 8(sp)
    csrr t0, mcause
    sd t0, 0(s0)
    csrr t0, mepc
    sd t0, 8(s0)
    sd a2, 16(s0)
    csrr t0, vstart
    sd t0, 48(s0)
    ld t0, 56(s0)
    addi t0, t0, 1
    sd t0, 56(s0)
    /* Disable this timer source before returning. */
    li t0, 0x02004000
    li t1, -1
    sd t1, 0(t0)
    fence iorw,iorw
    addi t0, s0, 64
    vse64.v v8, (t0)
    ld t0, 0(sp)
    ld t1, 8(sp)
    addi sp, sp, 32
    mret
.size illixr_native_vector_irq, .-illixr_native_vector_irq
.option pop
