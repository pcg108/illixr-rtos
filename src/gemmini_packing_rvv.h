#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef ILLIXR_PACKING_SATURN_COMPAT
// Read only after serialized packing has stopped.
unsigned long long illixr_pack_rmm_calls(void);
unsigned long long illixr_pack_rmm_elements(void);
#endif
// Element strides may be negative. Pointers address the first logical element.
// No rounding-mode changes: conversion uses the caller's dynamic frm.
void illixr_pack_d2f(size_t count,const double *src,ptrdiff_t src_stride,float *dst,ptrdiff_t dst_stride);
void illixr_pack_f2d(size_t count,const float *src,ptrdiff_t src_stride,double *dst,ptrdiff_t dst_stride);
void illixr_pack_f2f(size_t count,const float *src,ptrdiff_t src_stride,float *dst,ptrdiff_t dst_stride);
void illixr_pack_zero(size_t count,float *dst);
#ifdef __cplusplus
}
#endif
