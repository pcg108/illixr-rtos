#include "gemmini_packing_vector.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <zephyr/kernel.h>
#include "replay.hpp"
using namespace ILLIXR::gemmini_backend;
namespace {
constexpr size_t cap=32768;
alignas(64) double ad[cap],bd[cap],cd[cap],outd[cap];
alignas(64) float af[cap],bf[cap],cf[cap],outf[cap],a0[cap],b0[cap],c0[cap],a1[cap],b1[cap],c1[cap];
unsigned failures=0;uint64_t checks=0;
unsigned csr(){unsigned x;asm volatile("frcsr %0":"=r"(x));return x;}
void csr(unsigned x){asm volatile("fscsr %0"::"r"(x):"memory");}
void check(bool ok,const char*what){++checks;if(!ok && failures++<8)printf("PACKING_MISMATCH %s check=%llu\n",what,(unsigned long long)checks);}
template<class T> void same(const T*a,const T*b,size_t n,const char*what){checks+=n;if(std::memcmp(a,b,n*sizeof(T)) && failures++<8)printf("PACKING_MISMATCH %s n=%zu\n",what,n);}
__attribute__((noinline)) float narrow(double x){return float(x);}
__attribute__((noinline)) double widen(float x){return double(x);}
#ifdef ILLIXR_PACKING_DIAGNOSTICS
struct FlagDiagnostic {unsigned rm;int stride;size_t n;unsigned want,got,after_read,after_fence,before_read_fence;bool bits_match;size_t mismatch_index;uint64_t input_bits;uint32_t expected_bits,actual_bits;};
FlagDiagnostic flag_diagnostics[180];size_t flag_count=0;
#endif
void conversion(){
 const uint64_t patterns[]={0,0x8000000000000000ull,0x3ff0000010000000ull,0x3ff0000030000000ull,0xbff0000010000000ull,0x47efffffe0000000ull,0x47f0000000000000ull,0x3690000000000000ull,0x36a0000000000000ull,1,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000123ull,0x7ff0000000000123ull};
 for(size_t i=0;i<cap;++i){uint64_t x=patterns[i%(sizeof(patterns)/8)];std::memcpy(ad+i,&x,8);}
 for(unsigned rm=0;rm<5;++rm)for(int stride:{-3,-1,1,3})for(size_t n:{size_t(0),size_t(1),size_t(7),size_t(8),size_t(9),size_t(15),size_t(17),size_t(111),size_t(257)}){
  size_t start=stride<0?3*n:0;std::memset(a0,0xA5,sizeof a0);std::memset(a1,0xA5,sizeof a1);
  csr(rm<<5);for(size_t i=0;i<n;++i)a0[i]=narrow(ad[ptrdiff_t(start)+ptrdiff_t(i)*stride]);unsigned want=csr();
  csr(rm<<5);illixr_pack_d2f(n,ad+start,stride,a1,1);unsigned got=csr();
#ifdef ILLIXR_PACKING_DIAGNOSTICS
  asm volatile("fence rw,rw" ::: "memory");unsigned before_read_fence=csr();
#endif
  same(a0,a1,cap,"narrow bits/guards");
#ifdef ILLIXR_PACKING_DIAGNOSTICS
  unsigned after_read=csr();asm volatile("fence rw,rw" ::: "memory");unsigned after_fence=csr();
  auto &diagnostic=flag_diagnostics[flag_count++];diagnostic={rm,stride,n,want,got,after_read,after_fence,before_read_fence,std::memcmp(a0,a1,sizeof a0)==0,n,0,0,0};
  for(size_t j=0;j<n;++j)if(std::memcmp(a0+j,a1+j,4)){diagnostic.mismatch_index=j;std::memcpy(&diagnostic.input_bits,ad+ptrdiff_t(start)+ptrdiff_t(j)*stride,8);std::memcpy(&diagnostic.expected_bits,a0+j,4);std::memcpy(&diagnostic.actual_bits,a1+j,4);break;}
#endif
  check(got==want,"narrow fcsr");
  std::memset(cd,0xA5,sizeof cd);std::memset(outd,0xA5,sizeof outd);csr(rm<<5);for(size_t i=0;i<n;++i)cd[ptrdiff_t(start)+ptrdiff_t(i)*stride]=widen(a0[i]);want=csr();csr(rm<<5);illixr_pack_f2d(n,a1,1,outd+start,stride);got=csr();same(cd,outd,cap,"widen bits/guards");check(got==want,"widen fcsr");
 }
 csr(0);
 // Copies preserve payload bits, including signaling NaNs, without FP arithmetic.
 for(size_t i=0;i<cap;++i){uint32_t x=0x7f800001u+unsigned(i);std::memcpy(af+i,&x,4);}csr(0);illixr_pack_f2f(cap,af,1,bf,1);same(af,bf,cap,"float payload copy");check(csr()==0,"copy fcsr");
 for(unsigned rm=0;rm<5;++rm){csr(rm<<5);for(size_t i=0;i<cap;++i)cd[i]=widen(af[i]);unsigned want=csr();csr(rm<<5);illixr_pack_f2d(cap,af,1,outd,1);unsigned got=csr();same(cd,outd,cap,"signaling NaN widen");check(got==want,"signaling NaN flags");}csr(0);
 Request zero{true,1,1,1,1,1,0,ad,ad,nullptr,1,1,1,1,1,1,0};
 pack<double>(zero,a0,b0,c0);pack_vector<double>(zero,a1,b1,c1);same(c0,c1,1,"beta zero never reads C");
}
template<class T>void matrices(T*a,T*b,T*c,T*out){
 for(size_t i=0;i<cap;++i){a[i]=T(int(i%79)-39)/T(11);b[i]=T(int(i%47)-23)/T(7);}
 for(size_t m:{size_t(1),size_t(7),size_t(8),size_t(9),size_t(31),size_t(111),size_t(135)})for(int layout=0;layout<4;++layout)for(double beta:{0.,.7})for(auto t:{Traversal::rows,Traversal::source_contiguous}){
  size_t n=m>=111?17:m+2,k=m>=111?m:m+3;
  for(size_t i=0;i<cap;++i)c[i]=out[i]=T(int(i%37)-18)/T(13);
  Request r{sizeof(T)==8,0,m,n,k,1,beta,a,b,c,layout&1?ptrdiff_t(k+3):1,layout&1?1:ptrdiff_t(m+3),layout&2?ptrdiff_t(n+3):1,layout&2?1:ptrdiff_t(k+3),1,ptrdiff_t(m+3),0};
  pack<T>(r,a0,b0,c0);pack_vector<T>(r,a1,b1,c1,t);same(a0,a1,m*k,"matrix A");same(b0,b1,k*n,"matrix B");same(c0,c1,m*n,"matrix C");
  for(size_t i=0;i<m*n;++i)c0[i]=c1[i]=float(int(i%29)-14)/17.f;
  unpack<T>(r,c0);r.c=out;unpack_vector<T>(r,c1,t);same(c,out,cap,"matrix output and padding");
 }
 for(int inc:{-3,-1,1,3}){
  size_t n=17,k=9;Request r{sizeof(T)==8,2,n,1,k,1,0,a,inc<0?b+3*k:b,inc<0?c+3*n:c,1,ptrdiff_t(n+2),inc,0,inc,0,0};
  pack<T>(r,a0,b0,c0);pack_vector<T>(r,a1,b1,c1);same(a0,a1,n*k,"gemv A");same(b0,b1,k,"gemv x");same(c0,c1,n,"gemv beta-zero");
 }
}
}
bool gemmini_packing_cases(){
 unsigned saved=csr();conversion();matrices(ad,bd,cd,outd);matrices(af,bf,cf,outf);csr(saved);
 #ifdef ILLIXR_PACKING_DIAGNOSTICS
 for(size_t i=0;i<flag_count;++i){auto &d=flag_diagnostics[i];printf("ILLIXR_PACKING_FLAGS {\"rm\":%u,\"stride\":%d,\"n\":%zu,\"expected\":%u,\"immediate\":%u,\"after_read\":%u,\"after_fence\":%u,\"bits_match\":%s}\n",d.rm,d.stride,d.n,d.want,d.got,d.after_read,d.after_fence,d.bits_match?"true":"false");printf("ILLIXR_PACKING_FLAG_DETAIL {\"rm\":%u,\"stride\":%d,\"n\":%zu,\"before_read_fence\":%u,\"mismatch_index\":%zu,\"input_hex\":\"%016llx\",\"expected_hex\":\"%08x\",\"actual_hex\":\"%08x\"}\n",d.rm,d.stride,d.n,d.before_read_fence,d.mismatch_index,(unsigned long long)d.input_bits,d.expected_bits,d.actual_bits);}
#endif
#ifdef ILLIXR_PACKING_SATURN_COMPAT
 printf("ILLIXR_PACKING_COMPAT {\"scalar_rmm_calls\":%llu,\"scalar_rmm_elements\":%llu,\"conversion_return_fence\":true}\n",illixr_pack_rmm_calls(),illixr_pack_rmm_elements());
 check(illixr_pack_rmm_calls()==32 && illixr_pack_rmm_elements()==1700,"explicit RMM dispatch accounting");
#endif
 printf("ILLIXR_PACKING_TEST {\"checks\":%llu,\"failures\":%u,\"passed\":%s}\n",(unsigned long long)checks,failures,failures?"false":"true");return failures==0;
}
#ifdef ILLIXR_PACKING_BENCHMARK
namespace {
uint64_t cycles(){uint64_t x;asm volatile("fence rw,rw; rdcycle %0":"=r"(x)::"memory");return x;}
}
namespace {
K_THREAD_STACK_DEFINE(benchmark_stack,16384);
k_thread benchmark_thread;
bool benchmark_ok=false;
void benchmark_worker(void*,void*,void*) {
 if(ILLIXR::replay::hart_id()!=0){printf("ILLIXR_PACKING_BENCH_ERROR hart\n");return;}
 for(size_t i=0;i<cap;++i){ad[i]=double(int(i%79)-39)/11.;bd[i]=double(int(i%47)-23)/7.;cd[i]=double(int(i%37)-18)/13.;}
 struct Shape{size_t m,n,k;};
 for(auto shape:{Shape{3,3,3},Shape{15,15,15},Shape{21,21,21},Shape{63,63,63},Shape{111,111,111},Shape{135,135,135},Shape{111,15,15},Shape{111,1,111}})
 for(int layout=0;layout<4;++layout)for(double beta:{0.,1.})for(int trial=0;trial<9;++trial){
  Request r{true,0,shape.m,shape.n,shape.k,1,beta,ad,bd,cd,layout&1?ptrdiff_t(shape.k+2):1,layout&1?1:ptrdiff_t(shape.m+2),layout&2?ptrdiff_t(shape.n+2):1,layout&2?1:ptrdiff_t(shape.k+2),1,ptrdiff_t(shape.m+2),0};
  uint64_t packtime[3],unpacktime[3];
  for(int z=0;z<3;++z){int method=(z+trial)%3;auto begin=cycles();
   if(method==0)pack<double>(r,a0,b0,c0);else pack_vector<double>(r,a0,b0,c0,method==1?Traversal::rows:Traversal::source_contiguous);
   auto middle=cycles();if(method==0)unpack<double>(r,c0);else unpack_vector<double>(r,c0,method==1?Traversal::rows:Traversal::source_contiguous);auto end=cycles();packtime[method]=middle-begin;unpacktime[method]=end-middle;
  }
  if(trial)printf("ILLIXR_PACKING_BENCH {\"m\":%zu,\"n\":%zu,\"k\":%zu,\"layout\":%d,\"beta\":%d,\"trial\":%d,\"pack_cycles\":[%llu,%llu,%llu],\"unpack_cycles\":[%llu,%llu,%llu]}\n",shape.m,shape.n,shape.k,layout,int(beta),trial,(unsigned long long)packtime[0],(unsigned long long)packtime[1],(unsigned long long)packtime[2],(unsigned long long)unpacktime[0],(unsigned long long)unpacktime[1],(unsigned long long)unpacktime[2]);
 }
 benchmark_ok=true;
}
}
bool gemmini_packing_benchmark(){
 auto tid=k_thread_create(&benchmark_thread,benchmark_stack,K_THREAD_STACK_SIZEOF(benchmark_stack),benchmark_worker,nullptr,nullptr,nullptr,5,0,K_FOREVER);
#ifdef CONFIG_SCHED_CPU_MASK
 if(k_thread_cpu_pin(tid,0)!=0){printf("ILLIXR_PACKING_BENCH_ERROR affinity\n");k_thread_abort(tid);return false;}
#endif
 k_thread_start(tid);
 return k_thread_join(tid,K_FOREVER)==0 && benchmark_ok;
}
#endif
