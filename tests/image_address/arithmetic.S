/* Replay the faulting scalar FP instruction sequence; no estimator changes. */
.text
.align 2
.global illixr_image_arithmetic
.type illixr_image_arithmetic, @function
illixr_image_arithmetic:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s0, 8(sp)
    fsd fs0, 16(sp)
    fsd fs1, 24(sp)
    fsd fs2, 32(sp)
    mv s0, a2
    fld fs0, 0(a0)
    fld fs1, 8(a0)
    fld fs2, 16(a0)
    fcvt.d.w fa0, a1
    fnmadd.d fa0, fs0, fa0, fs2
    fsd fa0, 0(s0)
    fdiv.d fa0, fa0, fs1
    fsd fa0, 8(s0)
    call round
    fsd fa0, 16(s0)
    fcvt.w.d a0, fa0, rtz
    sw a0, 24(s0)
    fld fs2, 32(sp)
    fld fs1, 24(sp)
    fld fs0, 16(sp)
    ld s0, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 48
    ret
.size illixr_image_arithmetic, .-illixr_image_arithmetic

.align 2
.global illixr_image_arithmetic_tight
.type illixr_image_arithmetic_tight, @function
illixr_image_arithmetic_tight:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s0, 8(sp)
    fsd fs0, 16(sp)
    fsd fs1, 24(sp)
    fsd fs2, 32(sp)
    mv s0, a2
    fld fs0, 0(a0)
    fld fs1, 8(a0)
    fld fs2, 16(a0)
    fcvt.d.w fa0, a1
    fnmadd.d fa0, fs0, fa0, fs2
    fdiv.d fa0, fa0, fs1
    call round
    fsd fa0, 16(s0)
    fcvt.w.d a0, fa0, rtz
    sw a0, 24(s0)
    fld fs2, 32(sp)
    fld fs1, 24(sp)
    fld fs0, 16(sp)
    ld s0, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 48
    ret
.size illixr_image_arithmetic_tight, .-illixr_image_arithmetic_tight
