.text
.option push
.option arch, +v
.global illixr_vector_store_load
.type illixr_vector_store_load,@function
/* a0=aligned/padded buffer, a1=fence mode, fa0=expected and returned scalar. */
illixr_vector_store_load:
    sd zero, 0(a0)
    sd zero, 8(a0)
    sd zero, 16(a0)
    sd zero, 24(a0)
    sd zero, 32(a0)
    sd zero, 40(a0)
    sd zero, 48(a0)
    sd zero, 56(a0)
    csrw vstart, zero
    li t0, 8
    vsetvli zero, t0, e64, m2, tu, mu
    vfmv.v.f v8, fa0
    fence rw,rw
    bnez a1, .Lmemory_fenced
    vse64.v v8, (a0)
    fld fa0, 56(a0)
    ret
.Lmemory_fenced:
    vse64.v v8, (a0)
    fence rw,rw
    fld fa0, 56(a0)
    ret
.size illixr_vector_store_load, .-illixr_vector_store_load

.global illixr_vector_store_load_delayed
.type illixr_vector_store_load_delayed,@function
/* a0=buffer, a1=fence mode, a2=0..96 NOPs, fa0=value/result.
 * Sweep the scalar load across the vector-store completion boundary. Compute
 * the jump target before the store; every mode uses the same jump and load.
 */
illixr_vector_store_load_delayed:
    sd zero, 0(a0)
    sd zero, 8(a0)
    sd zero, 16(a0)
    sd zero, 24(a0)
    sd zero, 32(a0)
    sd zero, 40(a0)
    sd zero, 48(a0)
    sd zero, 56(a0)
    csrw vstart, zero
    li t0, 8
    vsetvli zero, t0, e64, m2, tu, mu
    vfmv.v.f v8, fa0
    la t1, .Lmemory_delay_load
    slli t0, a2, 2
    sub t1, t1, t0
    fence rw,rw
    bnez a1, .Lmemory_delay_fenced
    vse64.v v8, (a0)
    jr t1
.Lmemory_delay_fenced:
    vse64.v v8, (a0)
    fence rw,rw
    jr t1
    .option push
    .option norvc
    .rept 96
    nop
    .endr
.Lmemory_delay_load:
    fld fa0, 56(a0)
    ret
    .option pop
.size illixr_vector_store_load_delayed, .-illixr_vector_store_load_delayed
.option pop
