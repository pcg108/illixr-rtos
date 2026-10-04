#include "gemmini_packing_rvv.h"
#include <riscv_vector.h>
#ifdef ILLIXR_PACKING_SATURN_COMPAT
// The accepted Saturn image truncates FPConvBlock.frm to two bits. Keep this
// opt-in compatibility branch visible in diagnostic/runtime evidence.
static unsigned long long rmm_calls,rmm_elements;
unsigned long long illixr_pack_rmm_calls(void) {return rmm_calls;}
unsigned long long illixr_pack_rmm_elements(void) {return rmm_elements;}
#endif

void illixr_pack_d2f(size_t n,const double *s,ptrdiff_t ss,float *d,ptrdiff_t ds) {
#ifdef ILLIXR_PACKING_SATURN_COMPAT
 if(!n)return;
 unsigned rm;__asm__ volatile("frrm %0":"=r"(rm)::"memory");
 if(rm==4){
  ++rmm_calls;rmm_elements+=n;
  while(n){float y;__asm__ volatile("fcvt.s.d %0,%1,dyn":"=f"(y):"f"(*s));*d=y;--n;if(n){s+=ss;d+=ds;}}
  return;
 }
#endif
 while(n) {
  size_t vl=__riscv_vsetvl_e32m1(n);
  vfloat64m2_t x=ss==1 ? __riscv_vle64_v_f64m2(s,vl) : __riscv_vlse64_v_f64m2(s,ss*(ptrdiff_t)sizeof(*s),vl);
  vfloat32m1_t y=__riscv_vfncvt_f_f_w_f32m1(x,vl);
  if(ds==1) __riscv_vse32_v_f32m1(d,y,vl); else __riscv_vsse32_v_f32m1(d,ds*(ptrdiff_t)sizeof(*d),y,vl);
  n-=vl;if(n){s+=(ptrdiff_t)vl*ss;d+=(ptrdiff_t)vl*ds;}
 }
#ifdef ILLIXR_PACKING_SATURN_COMPAT
 __asm__ volatile("fence rw,rw" ::: "memory");
#endif
}
void illixr_pack_f2d(size_t n,const float *s,ptrdiff_t ss,double *d,ptrdiff_t ds) {
 while(n) {
  size_t vl=__riscv_vsetvl_e32m1(n);
  vfloat32m1_t x=ss==1 ? __riscv_vle32_v_f32m1(s,vl) : __riscv_vlse32_v_f32m1(s,ss*(ptrdiff_t)sizeof(*s),vl);
  vfloat64m2_t y=__riscv_vfwcvt_f_f_v_f64m2(x,vl);
  if(ds==1) __riscv_vse64_v_f64m2(d,y,vl); else __riscv_vsse64_v_f64m2(d,ds*(ptrdiff_t)sizeof(*d),y,vl);
  n-=vl;if(n){s+=(ptrdiff_t)vl*ss;d+=(ptrdiff_t)vl*ds;}
 }
#ifdef ILLIXR_PACKING_SATURN_COMPAT
 __asm__ volatile("fence rw,rw" ::: "memory");
#endif
}
void illixr_pack_f2f(size_t n,const float *s,ptrdiff_t ss,float *d,ptrdiff_t ds) {
 while(n) {
  size_t vl=__riscv_vsetvl_e32m1(n);
  vfloat32m1_t x=ss==1 ? __riscv_vle32_v_f32m1(s,vl) : __riscv_vlse32_v_f32m1(s,ss*(ptrdiff_t)sizeof(*s),vl);
  if(ds==1) __riscv_vse32_v_f32m1(d,x,vl); else __riscv_vsse32_v_f32m1(d,ds*(ptrdiff_t)sizeof(*d),x,vl);
  n-=vl;if(n){s+=(ptrdiff_t)vl*ss;d+=(ptrdiff_t)vl*ds;}
 }
}
void illixr_pack_zero(size_t n,float *d) {
 while(n){size_t vl=__riscv_vsetvl_e32m1(n);__riscv_vse32_v_f32m1(d,__riscv_vfmv_v_f_f32m1(0.f,vl),vl);n-=vl;if(n)d+=vl;}
}
