#include "../../src/blas_reference.hpp"
#include <zephyr/kernel.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

extern "C" {
void __wrap_dtrmm_(const char*,const char*,const char*,const char*,const int*,const int*,const double*,const double*,const int*,double*,const int*);
void __wrap_dtrsm_(const char*,const char*,const char*,const char*,const int*,const int*,const double*,const double*,const int*,double*,const int*);
void illixr_vector_active(double*,unsigned long*,unsigned long,unsigned long);
void illixr_vector_load(const void*,unsigned long);
}
namespace ILLIXR::blas_backend {
#ifdef ILLIXR_DIAGNOSTIC_RVV_MEMORY
extern "C" double illixr_vector_store_load(double*,unsigned long,double);
extern "C" double illixr_vector_store_load_delayed(double*,unsigned long,unsigned long,double);
#endif
#ifdef ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ
extern "C" void illixr_native_vector_irq(void*);
#endif
#ifdef ILLIXR_DIAGNOSTIC_RVV_REJECT
extern "C" void illixr_rejected_vector(void*,unsigned long);
#endif
namespace {
double value(unsigned i) { return double(int((i*17+11)%41)-20)/23.; }
bool equal(double a,double b) { return std::isfinite(a) && std::abs(a-b)<=1e-12+1e-10*std::abs(b); }
uint64_t bits(double x) { uint64_t r; std::memcpy(&r,&x,8); return r; }
unsigned hart() { unsigned long x; asm volatile("csrr %0, mhartid":"=r"(x)); return x; }
bool tail(unsigned &checks) {
 bool passed=true; unsigned id=0;
 for(unsigned masked : {0u,1u}) for(unsigned round=0;round<3;++round)
 for(int n : {1,3,7,8,9,47,48,49,50}) for(unsigned variant=0;variant<3;++variant)
 for(unsigned input_path : {0u,1u}) {
  const int m=47,lda=49,ldb=48;
  const char side='L',u=variant==1?'L':'U',t=variant==1?'T':'N',d=variant==2?'U':'N';
  const double alpha=.7,inverse=1./alpha;
  std::vector<double>A(lda*m),B(ldb*n),R(ldb*n);
  for(unsigned i=0;i<A.size();++i) A[i]=.05*value(i);
  for(int i=0;i<m;++i) A[i+i*lda]=2.;
  for(unsigned i=0;i<B.size();++i) B[i]=R[i]=value(i+2);
  printf("ILLIXR_TRSM_START id=%u masked=%u round=%u n=%d flags=L%c%c%c input=%s hart=%u\n",++id,masked,round,n,u,t,d,input_path?"trmm":"reference",hart());
  fflush(stdout);
  // Keep oracle preparation out of the active interrupt experiment.
  unsigned key=irq_lock();
  blas_reference::triangular(side,u,t,d,m,n,alpha,A.data(),lda,R.data(),ldb);
  irq_unlock(key);
  unsigned input_errors=0;
  if(input_path) {
   if(masked) key=irq_lock();
   __wrap_dtrmm_(&side,&u,&t,&d,&m,&n,&alpha,A.data(),&lda,B.data(),&ldb);
   if(masked) irq_unlock(key);
   for(unsigned i=0;i<B.size();++i) { ++checks; input_errors+=!equal(B[i],R[i]); }
  } else B=R;
  if(masked) key=irq_lock();
  __wrap_dtrsm_(&side,&u,&t,&d,&m,&n,&inverse,A.data(),&lda,B.data(),&ldb);
  if(masked) irq_unlock(key);
  unsigned errors=0,first=0;
  for(unsigned i=0;i<B.size();++i) {
   ++checks;
   if(!equal(B[i],value(i+2))) { if(!errors) first=i; ++errors; }
  }
  printf("ILLIXR_TRSM_RESULT id=%u input_errors=%u errors=%u first_row=%u first_col=%u actual=%016llx expected=%016llx hart=%u\n",id,input_errors,errors,first%ldb,first/ldb,(unsigned long long)bits(B[first]),(unsigned long long)bits(value(first+2)),hart());
  fflush(stdout);
  passed=passed && !errors && !input_errors;
 }
 return passed;
}
K_THREAD_STACK_DEFINE(competitor_stack,8192);
k_thread competitor_thread;
atomic_t stop_competitor{}, competitor_rounds{}, ticks{}, active_window{}, active_ticks{};
void tick(k_timer*) { atomic_inc(&ticks); if(atomic_get(&active_window)) atomic_inc(&active_ticks); }
K_TIMER_DEFINE(timer,tick,nullptr);
void competitor(void*,void*,void*) {
 alignas(64) unsigned char pattern[1024];
 for(unsigned i=0;i<1024;++i) pattern[i]=(i*13+71)&255;
 while(!atomic_get(&stop_competitor)) {
  illixr_vector_load(pattern,7);
  asm volatile("li t0, 13; fcvt.d.w ft0, t0" ::: "t0","ft0");
  atomic_inc(&competitor_rounds);
  k_sleep(K_TICKS(1));
 }
}
bool interrupts(unsigned &checks) {
 bool passed=true;
 for(unsigned competing : {0u,1u}) {
  atomic_set(&stop_competitor,0); atomic_set(&competitor_rounds,0);
  if(competing) k_thread_create(&competitor_thread,competitor_stack,K_THREAD_STACK_SIZEOF(competitor_stack),competitor,nullptr,nullptr,nullptr,-1,0,K_NO_WAIT);
  for(unsigned masked : {0u,1u}) {
   unsigned errors=0; const auto before=atomic_get(&competitor_rounds);
   atomic_set(&ticks,0); atomic_set(&active_ticks,0); k_timer_start(&timer,K_TICKS(1),K_TICKS(1));
   for(unsigned round=0;round<32;++round) for(unsigned vl : {1u,3u,8u}) {
    alignas(64) double out[8]{}; unsigned long csr[5]{};
    unsigned key=0; if(masked) key=irq_lock();
    atomic_set(&active_window,1);
    illixr_vector_active(out,csr,20000,vl);
    atomic_set(&active_window,0);
    if(masked) irq_unlock(key);
    unsigned bad=0;
    for(unsigned i=0;i<vl;++i) { ++checks; bad+=out[i]!=20000.; }
    const unsigned long expected[]={vl,25,0,3,0};
    for(unsigned i=0;i<5;++i) { ++checks; bad+=csr[i]!=expected[i]; }
    if(bad && errors<16) printf("ILLIXR_ACTIVE_ERROR competing=%u masked=%u round=%u requested_vl=%u bad=%u value=%016llx vl=%lu vtype=%lu vstart=%lu vcsr=%lu fcsr=%lu hart=%u\n",competing,masked,round,vl,bad,(unsigned long long)bits(out[0]),csr[0],csr[1],csr[2],csr[3],csr[4],hart());
    errors+=bad;
   }
   k_timer_stop(&timer);
   const auto observed=atomic_get(&ticks), switches=atomic_get(&competitor_rounds)-before;
   const auto during=atomic_get(&active_ticks);
   const bool coverage=masked ? during==0 : (during>0 && (!competing || switches>0));
   passed=passed && errors==0 && coverage;
   printf("ILLIXR_ACTIVE_RESULT competing=%u masked=%u errors=%u timer_callbacks=%ld active_callbacks=%ld competitor_rounds=%ld coverage=%u hart=%u\n",competing,masked,errors,long(observed),long(during),long(switches),unsigned(coverage),hart());
   fflush(stdout);
  }
  if(competing) { atomic_set(&stop_competitor,1); k_thread_join(&competitor_thread,K_FOREVER); }
 }
 return passed;
}
}
bool focused_diagnostic(unsigned &checks) {
 printf("ILLIXR_FOCUSED_START version=1 tail_cases=324 active_rounds=384\n");
#ifdef ILLIXR_DIAGNOSTIC_RVV_MEMORY
 bool memory_good=true;
 alignas(64) double memory_buffer[16]{};
 for(unsigned fenced : {0u,1u}) {
  unsigned early_errors=0,late_errors=0;
  for(unsigned round=0;round<64;++round) for(unsigned offset=0;offset<8;++offset) {
   const double expected=double(round*8+offset+1);
   const auto key=irq_lock();
   double *buffer=memory_buffer+offset;
   const double early=illixr_vector_store_load(buffer,fenced,expected);
   asm volatile("fence rw,rw" ::: "memory");
   unsigned late=0;
   for(unsigned i=0;i<8;++i) { ++checks;late+=buffer[i]!=expected; }
   irq_unlock(key);++checks;
   if((early!=expected || late) && early_errors+late_errors<16)
    printf("ILLIXR_MEMORY_ERROR fenced=%u round=%u offset=%u early=%016llx expected=%016llx late_errors=%u hart=%u\n",fenced,round,offset,(unsigned long long)bits(early),(unsigned long long)bits(expected),late,hart());
   early_errors+=early!=expected;late_errors+=late;
  }
  printf("ILLIXR_MEMORY_RESULT fenced=%u early_errors=%u late_errors=%u cases=512 hart=%u\n",fenced,early_errors,late_errors,hart());
  memory_good=memory_good && !early_errors && !late_errors;
 }
 // The immediate-load fixture can miss a one-cycle overlap at store completion.
 // Keep its original cases, then sweep the issue time without adding a fence.
 for(unsigned fenced : {0u,1u}) {
  unsigned printed=0;
  for(unsigned delay=0;delay<=96;++delay) {
   unsigned early_errors=0,late_errors=0;
   for(unsigned round=0;round<16;++round) for(unsigned offset=0;offset<8;++offset) {
    const double expected=double((delay*16+round)*8+offset+1);
    const auto key=irq_lock();
    double *buffer=memory_buffer+offset;
    const double early=illixr_vector_store_load_delayed(buffer,fenced,delay,expected);
    asm volatile("fence rw,rw" ::: "memory");
    unsigned late=0;
    for(unsigned i=0;i<8;++i) { ++checks;late+=buffer[i]!=expected; }
    irq_unlock(key);++checks;
    if((early!=expected || late) && printed++<16)
     printf("ILLIXR_MEMORY_DELAY_ERROR fenced=%u delay=%u round=%u offset=%u early=%016llx expected=%016llx late_errors=%u hart=%u\n",fenced,delay,round,offset,(unsigned long long)bits(early),(unsigned long long)bits(expected),late,hart());
    early_errors+=early!=expected;late_errors+=late;
   }
   printf("ILLIXR_MEMORY_DELAY_RESULT fenced=%u delay=%u early_errors=%u late_errors=%u cases=128 hart=%u\n",fenced,delay,early_errors,late_errors,hart());
   memory_good=memory_good && !early_errors && !late_errors;
  }
 }
 printf("ILLIXR_MEMORY_END passed=%u\n",unsigned(memory_good));
#else
 const bool memory_good=true;
#endif
#ifdef ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ
 bool native_good=true;
 for(unsigned round=0;round<64;++round) {
  struct Result { uint64_t cause,pc,remaining,add_pc,dec_pc,branch_pc,vstart,hits; double during[8],after[8]; } result{};
  const auto key=irq_lock();
  illixr_native_vector_irq(&result);
  irq_unlock(key);
  bool good=result.cause==0x8000000000000007ULL && result.hits==1 && result.vstart==0;
  const bool pc_valid=result.pc==result.add_pc || result.pc==result.dec_pc || result.pc==result.branch_pc;
  good=good && pc_valid;
  const double expected_during=double(20000-result.remaining)+(result.pc==result.dec_pc?1.:0.);
  checks+=4;
  unsigned during_errors=0,after_errors=0;
  for(unsigned i=0;i<8;++i) { during_errors+=result.during[i]!=expected_during; after_errors+=result.after[i]!=20000.; checks+=2; }
  good=good && !during_errors && !after_errors; native_good=native_good && good;
  printf("ILLIXR_NATIVE_IRQ round=%u passed=%u cause=%llx pc=%llx add_pc=%llx dec_pc=%llx branch_pc=%llx remaining=%llu vstart=%llu hits=%llu during=%016llx expected_during=%016llx after=%016llx during_errors=%u after_errors=%u hart=%u\n",round,unsigned(good),(unsigned long long)result.cause,(unsigned long long)result.pc,(unsigned long long)result.add_pc,(unsigned long long)result.dec_pc,(unsigned long long)result.branch_pc,(unsigned long long)result.remaining,(unsigned long long)result.vstart,(unsigned long long)result.hits,(unsigned long long)bits(result.during[0]),(unsigned long long)bits(expected_during),(unsigned long long)bits(result.after[0]),during_errors,after_errors,hart());
 }
 printf("ILLIXR_NATIVE_IRQ_END passed=%u\n",unsigned(native_good));
#else
 const bool native_good=true;
#endif
#ifdef ILLIXR_DIAGNOSTIC_RVV_REJECT
 bool rejected_good=true;
 for(unsigned mode=0;mode<4;++mode) for(unsigned round=0;round<8;++round) {
  struct Result { uint64_t cause,pc,tval,expected_pc; double before[8],after[8]; } result{};
  const auto key=irq_lock();
  illixr_rejected_vector(&result,mode);
  irq_unlock(key);
  bool good=result.cause==2 && result.pc==result.expected_pc;
  checks+=2;
  for(unsigned i=0;i<8;++i) { good=good && result.before[i]==9. && result.after[i]==9.; checks+=2; }
  rejected_good=rejected_good && good;
  printf("ILLIXR_REJECTED_VECTOR mode=%u round=%u passed=%u cause=%llu pc=%llx expected_pc=%llx instruction=%llx before=%016llx after=%016llx hart=%u\n",mode,round,unsigned(good),(unsigned long long)result.cause,(unsigned long long)result.pc,(unsigned long long)result.expected_pc,(unsigned long long)result.tval,(unsigned long long)bits(result.before[0]),(unsigned long long)bits(result.after[0]),hart());
 }
 printf("ILLIXR_REJECTED_VECTOR_END passed=%u\n",unsigned(rejected_good));
#else
 const bool rejected_good=true;
#endif
 const bool a=tail(checks);
 const bool b=interrupts(checks);
 printf("ILLIXR_FOCUSED_END tail_pass=%u active_pass=%u checks=%u\n",unsigned(a),unsigned(b),checks);
 return a && b && rejected_good && native_good && memory_good;
}
}
