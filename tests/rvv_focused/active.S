/* Active FP64 vector state across asynchronous interrupts. Exact integer sums. */
.text
.option push
.option arch, +v
.global illixr_vector_active
.type illixr_vector_active,@function
/* a0=output[8], a1=CSR output[5], a2=iterations, a3=vl (1/3/8). */
illixr_vector_active:
    csrr t3, fcsr
    csrw fcsr, zero
    csrw vstart, zero
    li t0, 3
    csrw vcsr, t0
    vsetvli zero, a3, e64, m2, tu, mu
    vmv.v.i v8, 0
    li t0, 1
    fcvt.d.w ft0, t0
1:
    vfadd.vf v8, v8, ft0
    addi a2, a2, -1
    bnez a2, 1b
    csrr t0, vl
    sd t0, 0(a1)
    csrr t0, vtype
    sd t0, 8(a1)
    csrr t0, vstart
    sd t0, 16(a1)
    csrr t0, vcsr
    sd t0, 24(a1)
    csrr t0, fcsr
    sd t0, 32(a1)
    /* Bound the store even if the state under test was damaged. */
    csrw vstart, zero
    vsetvli zero, a3, e64, m2, tu, mu
    vse64.v v8, (a0)
    csrw fcsr, t3
    ret
.size illixr_vector_active, .-illixr_vector_active
.option pop
