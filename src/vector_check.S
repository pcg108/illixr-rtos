/* Explicit assembly avoids compiler use of the test's live vector state. */
.text
.option push
.option arch, +v
.global illixr_vector_load
.type illixr_vector_load,@function
illixr_vector_load:
    csrw vstart, zero
    vl8re8.v v0, (a0)
    addi a0, a0, 256
    vl8re8.v v8, (a0)
    addi a0, a0, 256
    vl8re8.v v16, (a0)
    addi a0, a0, 256
    vl8re8.v v24, (a0)
    li t0, 3
    vsetvli zero, t0, e64, m1, tu, mu
    csrw vcsr, a1
    li t0, 1
    csrw vstart, t0
    ret
.size illixr_vector_load, .-illixr_vector_load

.global illixr_vector_store
.type illixr_vector_store,@function
illixr_vector_store:
    csrr t0, vlenb
    sd t0, 0(a1)
    csrr t0, vl
    sd t0, 8(a1)
    csrr t0, vtype
    sd t0, 16(a1)
    csrr t0, vcsr
    sd t0, 24(a1)
    csrr t0, vstart
    sd t0, 32(a1)
    csrw vstart, zero
    vs8r.v v0, (a0)
    addi a0, a0, 256
    vs8r.v v8, (a0)
    addi a0, a0, 256
    vs8r.v v16, (a0)
    addi a0, a0, 256
    vs8r.v v24, (a0)
    ret
.size illixr_vector_store, .-illixr_vector_store

.global illixr_vector_fp64
.type illixr_vector_fp64,@function
illixr_vector_fp64:
    csrw vstart, zero
    li t0, 4
    vsetvli zero, t0, e64, m1, tu, mu
    vle64.v v0, (a0)
    vfadd.vv v1, v0, v0
    vfmul.vv v2, v1, v0
    vse64.v v2, (a1)
    ret
.size illixr_vector_fp64, .-illixr_vector_fp64
.option pop
