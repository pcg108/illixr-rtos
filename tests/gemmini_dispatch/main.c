#include <stdio.h>
#include <stdint.h>
void int8_prepare(int);void fp32_prepare(int);
void int8_step(int);void fp32_step(int);
int int8_check(void);int fp32_check(void);
int main(void) {
 unsigned long hart;asm volatile("csrr %0,mhartid":"=r"(hart));
 if(hart) return 2;
 printf("GEMMINI_DISPATCH_BEGIN hart=0 rounds=8 stages=9\n");
 unsigned errors=0;
 for(int round=0;round<8;round++) {
  int8_prepare(round);fp32_prepare(round);
  /* No fence between arrays or stages: both arrays may have work outstanding. */
  for(int stage=0;stage<9;stage++) {
   if(round&1){fp32_step(stage);int8_step(stage);}
   else {int8_step(stage);fp32_step(stage);}
  }
  asm volatile("fence rw,rw" ::: "memory");
  int i=int8_check(),f=fp32_check();errors+=i+f;
  printf("GEMMINI_DISPATCH_ROUND %d int8_errors=%d fp32_errors=%d\n",round,i,f);
 }
 printf("GEMMINI_DISPATCH_END errors=%u\n",errors);
 return errors?1:0;
}
