#include "blas_backend.hpp"
#ifdef ILLIXR_USE_GEMMINI_BLAS
#include "gemmini_backend.hpp"
#endif
#include "trace_output.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/__assert.h>

#ifndef ILLIXR_USE_OPENBLAS
namespace ILLIXR::blas_backend {
void initialize() { printf("ILLIXR_BLAS {\"backend\":\"eigen\",\"scratch_bytes\":0}\n"); }
void dump() {}
bool self_test() { return true; }
}
#else
#include "replay.hpp"
#include "blas_reference.hpp"
namespace {
constexpr size_t arena_size = 32u * 1024u * 1024u;
struct alignas(4096) Scratch { unsigned char bytes[arena_size]; uint64_t guard[8]; };
// Like desktop malloc, BLAS workspace has no initial-value contract. Kernels
// and Gemmini packing initialize the bytes they consume; initialize() below
// writes the overflow guards explicitly. Avoid clearing 32 MiB before boot.
__noinit Scratch scratch;
auto &arena = scratch.bytes;
constexpr uint64_t sentinel = 0xc73aba502e61459fULL;
K_MUTEX_DEFINE(blas_lock);
bool allocated = false;
uint64_t allocations = 0;
struct Stats { uint64_t calls=0, cycles=0, wait_cycles=0, cycles_samples=0, cycles_migrated=0, wait_cycles_migrated=0, elapsed_ns=0, wait_ns=0; unsigned max_m=0,max_n=0,max_k=0,mask=0; uint64_t harts[4]{}; } stats[12];
uint64_t cycles() { uint64_t value; asm volatile("rdcycle %0" : "=r"(value)); return value; }
[[noreturn]] void fatal(const char *reason) { printf("ILLIXR_BLAS_ERROR %s\n",reason); k_panic(); __builtin_unreachable(); }
struct Counter { uint64_t cycles, ticks; unsigned hart; };
Counter counter() {
 const auto key=irq_lock();
 const Counter result{cycles(), k_cycle_get_64(), ILLIXR::replay::hart_id()};
 irq_unlock(key);
 return result;
}
struct Call {
 Stats &s; Counter start;
 Call(unsigned id,int m,int n,int k):s(stats[id]) {
  const auto before=counter();
  if(k_mutex_lock(&blas_lock,K_FOREVER)) fatal("mutex acquisition failed");
  start=counter();
  s.wait_ns+=k_cyc_to_ns_floor64(start.ticks-before.ticks);
  if(start.hart==before.hart && start.cycles>=before.cycles) s.wait_cycles+=start.cycles-before.cycles;
  else ++s.wait_cycles_migrated;
  ++s.calls;
  if(m>0 && unsigned(m)>s.max_m) s.max_m=m;
  if(n>0 && unsigned(n)>s.max_n) s.max_n=n;
  if(k>0 && unsigned(k)>s.max_k) s.max_k=k;
  const unsigned hart=start.hart;
  if(hart>=4) fatal("unexpected hart");
  s.mask|=1u<<hart; ++s.harts[hart];
 }
 ~Call() {
  const auto end=counter();
  // Core counters are hart-local. Never subtract readings from different harts.
  if(end.hart==start.hart && end.cycles>=start.cycles) { s.cycles+=end.cycles-start.cycles; ++s.cycles_samples; }
  else ++s.cycles_migrated;
  s.elapsed_ns+=k_cyc_to_ns_floor64(end.ticks-start.ticks);
  s.mask|=1u<<end.hart;
  if(allocated) fatal("scratch allocation leaked");
  k_mutex_unlock(&blas_lock);
 }
};
}
extern "C" void *blas_memory_alloc(int) {
 if(blas_lock.owner!=k_current_get() || allocated) fatal("unserialized or nested scratch allocation");
 allocated=true; ++allocations; return arena;
}
extern "C" void blas_memory_free(void *p) {
 if(p!=arena || !allocated || blas_lock.owner!=k_current_get()) fatal("invalid scratch release");
 for(auto value:scratch.guard) if(value!=sentinel) fatal("scratch arena overflow");
 allocated=false;
}
extern "C" int xerbla_(const char *, int *, int) { fatal("invalid BLAS arguments"); }

#define WRAPPERS(P,T,BASE) \
extern "C" void __real_##P##gemm_(const char*,const char*,const int*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*); \
extern "C" void __wrap_##P##gemm_(const char*a,const char*b,const int*m,const int*n,const int*k,const T*alpha,const T*A,const int*lda,const T*B,const int*ldb,const T*beta,T*C,const int*ldc) { Call call(BASE,*m,*n,*k); __real_##P##gemm_(a,b,m,n,k,alpha,A,lda,B,ldb,beta,C,ldc); } \
extern "C" void __real_##P##gemv_(const char*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*); \
extern "C" void __wrap_##P##gemv_(const char*t,const int*m,const int*n,const T*alpha,const T*A,const int*lda,const T*x,const int*ix,const T*beta,T*y,const int*iy) { Call call(BASE+1,*m,*n,0); __real_##P##gemv_(t,m,n,alpha,A,lda,x,ix,beta,y,iy); } \
extern "C" void __real_##P##trmm_(const char*,const char*,const char*,const char*,const int*,const int*,const T*,const T*,const int*,T*,const int*); \
extern "C" void __wrap_##P##trmm_(const char*s,const char*u,const char*t,const char*d,const int*m,const int*n,const T*alpha,const T*A,const int*lda,T*B,const int*ldb) { Call call(BASE+2,*m,*n,0); __real_##P##trmm_(s,u,t,d,m,n,alpha,A,lda,B,ldb); } \
extern "C" void __real_##P##trmv_(const char*,const char*,const char*,const int*,const T*,const int*,T*,const int*); \
extern "C" void __wrap_##P##trmv_(const char*u,const char*t,const char*d,const int*n,const T*A,const int*lda,T*x,const int*ix) { Call call(BASE+3,*n,0,0); __real_##P##trmv_(u,t,d,n,A,lda,x,ix); }
WRAPPERS(d,double,0)
WRAPPERS(s,float,6)
#define EXTRA_WRAPPERS(P,T,BASE) \
extern "C" void __real_##P##trsm_(const char*,const char*,const char*,const char*,const int*,const int*,const T*,const T*,const int*,T*,const int*); \
extern "C" void __wrap_##P##trsm_(const char*s,const char*u,const char*t,const char*d,const int*m,const int*n,const T*alpha,const T*A,const int*lda,T*B,const int*ldb) { Call call(BASE+4,*m,*n,0); __real_##P##trsm_(s,u,t,d,m,n,alpha,A,lda,B,ldb); } \
extern "C" void __real_##P##axpy_(const int*,const T*,const T*,const int*,T*,const int*); \
extern "C" void __wrap_##P##axpy_(const int*n,const T*alpha,const T*x,const int*ix,T*y,const int*iy) { Call call(BASE+5,*n,0,0); __real_##P##axpy_(n,alpha,x,ix,y,iy); }
EXTRA_WRAPPERS(d,double,0)
EXTRA_WRAPPERS(s,float,6)
#ifdef ILLIXR_USE_RVV_BLAS
namespace {
struct KernelStats { uint64_t calls{},harts[4]{}; } kernel_stats[3];
void kernel_entry(unsigned id) {
 if(blas_lock.owner!=k_current_get()) fatal("RVV kernel bypassed BLAS serialization");
 ++kernel_stats[id].calls; ++kernel_stats[id].harts[ILLIXR::replay::hart_id()];
}
void dump_kernels(const char *phase) {
 const char *names[]={"dgemm_kernel","dgemv_n","dgemv_t"};
 for(unsigned i=0;i<3;++i) {
  const auto &s=kernel_stats[i];
  ILLIXR::trace_output::print("ILLIXR_RVV_KERNEL {\"phase\":\"%s\",\"name\":\"%s\",\"calls\":%llu,\"harts\":[%llu,%llu,%llu,%llu]}\n",phase,names[i],(unsigned long long)s.calls,(unsigned long long)s.harts[0],(unsigned long long)s.harts[1],(unsigned long long)s.harts[2],(unsigned long long)s.harts[3]);
 }
}
}
extern "C" int __real_dgemm_kernel(long,long,long,double,double*,double*,double*,long);
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB
namespace ILLIXR::blas_backend { bool diagnostic_kernel_fence(); }
#endif
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
namespace ILLIXR::blas_backend { unsigned diagnostic_kernel_action(); }
#endif
extern "C" int __wrap_dgemm_kernel(long m,long n,long k,double alpha,double *a,double *b,double *c,long ldc) {
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
 kernel_entry(0);
 const unsigned action=ILLIXR::blas_backend::diagnostic_kernel_action();
 if(!action) return __real_dgemm_kernel(m,n,k,alpha,a,b,c,ldc);
 const int result=__real_dgemm_kernel(m,n,k,alpha,a,b,c,ldc);
 // Delay controls have compiler barriers but emit no hardware memory fence.
 // Keep a true tail-call baseline; the common non-tail return also adds delay.
 switch(action) {
 case 1: asm volatile(".option push\n.option norvc\nnop\n.option pop" ::: "memory"); break;
 case 2: asm volatile(".option push\n.option norvc\n.rept 16\nnop\n.endr\n.option pop" ::: "memory"); break;
 case 3: asm volatile(".option push\n.option norvc\n.rept 64\nnop\n.endr\n.option pop" ::: "memory"); break;
 case 4: asm volatile("fence rw,rw" ::: "memory"); break;
 }
 return result;
#elif defined(ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB)
 kernel_entry(0);
 // Preserve the original tail call on the unfenced path: an extra return
 // sequence can itself hide a vector-store / scalar-load timing defect.
 if(!ILLIXR::blas_backend::diagnostic_kernel_fence())
  return __real_dgemm_kernel(m,n,k,alpha,a,b,c,ldc);
 const int result=__real_dgemm_kernel(m,n,k,alpha,a,b,c,ldc);
 asm volatile("fence rw,rw" ::: "memory");
 return result;
#else
 kernel_entry(0);return __real_dgemm_kernel(m,n,k,alpha,a,b,c,ldc);
#endif
}
#define GEMV_KERNEL(NAME,ID) \
extern "C" int __real_##NAME(long,long,long,double,double*,long,double*,long,double*,long,double*); \
extern "C" int __wrap_##NAME(long m,long n,long dummy,double alpha,double*a,long lda,double*x,long incx,double*y,long incy,double*buffer) { \
 kernel_entry(ID);return __real_##NAME(m,n,dummy,alpha,a,lda,x,incx,y,incy,buffer); }
GEMV_KERNEL(dgemv_n,1)
GEMV_KERNEL(dgemv_t,2)
#endif
namespace ILLIXR::blas_backend {
void initialize() {
 for(auto &value:scratch.guard) value=sentinel;
 printf("ILLIXR_BLAS {\"backend\":\"%s\",\"scratch_bytes\":%zu,\"interface_bits\":32,\"threads\":1}\n",ILLIXR_LINALG_BACKEND,arena_size);
#ifdef ILLIXR_USE_GEMMINI_BLAS
 gemmini_backend::initialize();
#endif
}
#ifdef ILLIXR_DIAGNOSTIC_BLAS_PROGRESS
namespace {
unsigned diagnostic_case = 0;
unsigned diagnostic_errors = 0;
unsigned diagnostic_mode = 0;
const char *diagnostic_operation = "none";
#ifdef ILLIXR_DIAGNOSTIC_TRSM_CAPTURE
struct SolveCapture {
 bool frozen=false;
 unsigned mode=0,id=0,input_errors=0;
 int m=0,n=0,lda=0,ldb=0;
 char side='-',u='-',t='-',d='-';
 double a[8192]{},before[8192]{},after[8192]{};
} solve_capture;
void dump_solve_capture() {
 const auto &s=solve_capture;
 printf("ILLIXR_SOLVE_CAPTURE present=%u mode=%u id=%u m=%d n=%d lda=%d ldb=%d flags=%c%c%c%c input_errors=%u\n",unsigned(s.frozen),s.mode,s.id,s.m,s.n,s.lda,s.ldb,s.side,s.u,s.t,s.d,s.input_errors);
 if(!s.frozen) return;
 const int sizes[]={s.lda*(s.side=='L'?s.m:s.n),s.ldb*s.n,s.ldb*s.n};
 const double *arrays[]={s.a,s.before,s.after};
 for(unsigned a=0;a<3;++a) for(int i=0;i<sizes[a];i+=4) {
  printf("ILLIXR_SOLVE_DATA array=%u offset=%d",a,i);
  for(int j=i;j<std::min(i+4,sizes[a]);++j) {
   uint64_t bits; std::memcpy(&bits,&arrays[a][j],8);
   printf(" %016llx",static_cast<unsigned long long>(bits));
  }
  printf("\n");
 }
 printf("ILLIXR_SOLVE_CAPTURE_END\n");
}
#endif
struct SelfTestProgress {
 unsigned id;
 const char *operation;
 unsigned irq_key{};
 bool masked=false, fenced=false;
 SelfTestProgress(const char *op, int m, int n, int k,
                  char a='-', char b='-', char c='-', char d='-', int inc=0)
     : id(++diagnostic_case), operation(op) {
  diagnostic_operation = op;
  printf("ILLIXR_BLAS_PROGRESS enter id=%u op=%s m=%d n=%d k=%d flags=%c%c%c%c inc=%d hart=%u cycle=%llu\n",
         id, operation, m, n, k, a, b, c, d, inc, ILLIXR::replay::hart_id(),
         static_cast<unsigned long long>(cycles()));
  fflush(stdout);
#ifdef ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK
  masked=true;
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_AB
  masked=(diagnostic_mode & 1u)!=0;
  fenced=(diagnostic_mode & 2u)!=0;
#endif
  if(masked) irq_key=irq_lock();
  if(fenced) asm volatile("fence rw,rw" ::: "memory");
 }
 ~SelfTestProgress() {
  if(fenced) asm volatile("fence rw,rw" ::: "memory");
  if(masked) irq_unlock(irq_key);
  printf("ILLIXR_BLAS_PROGRESS exit id=%u op=%s cycle=%llu\n", id, operation,
         static_cast<unsigned long long>(cycles()));
  fflush(stdout);
 }
};
}
#define SELFTEST_CALL(OP, M, N, K, A, B, C, D, INC, ...) \
 do { SelfTestProgress progress(OP,M,N,K,A,B,C,D,INC); __VA_ARGS__; } while (false)
#elif defined(ILLIXR_DIAGNOSTIC_BLAS_FAILURES)
namespace {
struct FailureContext {
 unsigned id=0; const char *operation="none";
 int m=0,n=0,k=0,inc=0; char flags[5]{};
};
struct FailureRecord { FailureContext context; unsigned check; uint64_t actual,expected; };
FailureContext failure_context;
FailureRecord failure_records[16];
unsigned failure_count=0;
}
#define SELFTEST_CALL(OP, M, N, K, A, B, C, D, INC, ...) \
 do { failure_context={failure_context.id+1,OP,M,N,K,INC,{A,B,C,D,0}}; __VA_ARGS__; } while (false)
#else
#define SELFTEST_CALL(OP, M, N, K, A, B, C, D, INC, ...) do { __VA_ARGS__; } while (false)
#endif
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB
bool diagnostic_kernel_fence() { return (diagnostic_mode & 4u)!=0; }
#endif
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
unsigned diagnostic_kernel_action() {
 if(diagnostic_mode & 4u) return 4;
 if(diagnostic_mode & 8u) return 1;
 if(diagnostic_mode & 16u) return 2;
 if(diagnostic_mode & 32u) return 3;
 return 0;
}
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_AB
static bool self_test_phase() {
#else
bool self_test() {
#endif
#ifdef ILLIXR_DIAGNOSTIC_RVV_FOCUSED
 extern bool focused_diagnostic(unsigned &);
 unsigned focused_checks=0;
 const bool focused_good=focused_diagnostic(focused_checks);
 printf("ILLIXR_BLAS_SELFTEST {\"passed\":%s,\"checks\":%u}\n",focused_good?"true":"false",focused_checks);
 return focused_good;
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_PROGRESS
 diagnostic_errors=0;
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK
 printf("ILLIXR_BLAS_DIAGNOSTIC irq_mask_during_calls=1\n");
#endif
 unsigned checks=0;
 auto value=[](int i) { return double((i*17+11)%41-20)/23.; };
 auto equal=[&checks](double a,double b) {
  ++checks;
  const bool matches=std::isfinite(a) && std::abs(a-b)<=1e-12+1e-10*std::abs(b);
#if defined(ILLIXR_DIAGNOSTIC_BLAS_FAILURES) && !defined(ILLIXR_DIAGNOSTIC_BLAS_PROGRESS)
  if(!matches && failure_count++<16) {
   auto &record=failure_records[failure_count-1];
   record.context=failure_context;record.check=checks;
   std::memcpy(&record.actual,&a,8);std::memcpy(&record.expected,&b,8);
  }
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_PROGRESS
  if (!matches && diagnostic_errors++ < 16) {
   uint64_t actual, expected;
   std::memcpy(&actual,&a,sizeof(actual));
   std::memcpy(&expected,&b,sizeof(expected));
   printf("ILLIXR_BLAS_MISMATCH check=%u case=%u op=%s actual=%016llx expected=%016llx\n",
          checks,diagnostic_case,diagnostic_operation,
          static_cast<unsigned long long>(actual),static_cast<unsigned long long>(expected));
   fflush(stdout);
  }
#endif
  return matches;
 };
 bool good=true;
#ifdef ILLIXR_USE_GEMMINI_BLAS
 good=gemmini_backend::self_test(checks);
#endif
 // Exercise odd sizes, leading-dimension padding, both transpose modes and strides.
 for(int dim : {1,3,15,47,81}) {
  const int m=dim,n=dim+2,k=dim+1; const double alpha=.7,beta=-.2;
#ifndef ILLIXR_USE_GEMMINI_BLAS
  for(char ta : {'N','T'}) for(char tb : {'N','T'}) {
   const int lda=(ta=='N'?m:k)+3,ldb=(tb=='N'?k:n)+2,ldc=m+1;
   std::vector<double>A(lda*(ta=='N'?k:m)),B(ldb*(tb=='N'?n:k)),C(ldc*n),R(ldc*n);
   for(unsigned i=0;i<A.size();++i) A[i]=value(i);
   for(unsigned i=0;i<B.size();++i) B[i]=value(i+5);
   for(unsigned i=0;i<C.size();++i) C[i]=R[i]=value(i+13);
   SELFTEST_CALL("reference_gemm",m,n,k,ta,tb,'-','-',0,
     blas_reference::gemm(ta,tb,m,n,k,alpha,A.data(),lda,B.data(),ldb,beta,R.data(),ldc));
   SELFTEST_CALL("dgemm",m,n,k,ta,tb,'-','-',0,
     __wrap_dgemm_(&ta,&tb,&m,&n,&k,&alpha,A.data(),&lda,B.data(),&ldb,&beta,C.data(),&ldc));
   for(unsigned i=0;i<C.size();++i) good=equal(C[i],R[i])&&good;
  }
  for(char t : {'N','T'}) for(int inc : {1,2,-1,-2}) {
   const int lda=m+3,nx=t=='N'?n:m,ny=t=='N'?m:n,stride=std::abs(inc);
   std::vector<double>A(lda*n),x(nx*stride),y(ny*stride),r(ny*stride);
   for(unsigned i=0;i<A.size();++i) A[i]=value(i);
   for(unsigned i=0;i<x.size();++i) x[i]=value(i+2);
   for(unsigned i=0;i<y.size();++i) y[i]=r[i]=value(i+7);
   SELFTEST_CALL("reference_gemv",m,n,0,t,'-','-','-',inc,
     blas_reference::gemv(t,m,n,alpha,A.data(),lda,x.data(),inc,beta,r.data()));
   SELFTEST_CALL("dgemv",m,n,0,t,'-','-','-',inc,
     __wrap_dgemv_(&t,&m,&n,&alpha,A.data(),&lda,x.data(),&inc,&beta,y.data(),&inc));
   for(unsigned i=0;i<y.size();++i) good=equal(y[i],r[i])&&good;
  }
#endif
  for(char side : {'L','R'}) for(char u : {'U','L'}) for(char t : {'N','T'}) for(char d : {'N','U'}) {
   const int order=side=='L'?m:n,lda=order+2,ldb=m+1;
   std::vector<double>A(lda*order),B(ldb*n),R(ldb*n);
   for(unsigned i=0;i<A.size();++i) A[i]=.05*value(i);
   for(int i=0;i<order;++i) A[i+i*lda]=2.;
   for(unsigned i=0;i<B.size();++i) B[i]=R[i]=value(i+2);
   auto a=[&](int i,int j) { if(t=='T') std::swap(i,j); if(i==j && d=='U') return 1.; if((u=='U' && i>j)||(u=='L' && i<j)) return 0.; return A[i+j*lda]; };
   SELFTEST_CALL("reference_triangular",m,n,0,side,u,t,d,0,
     blas_reference::triangular(side,u,t,d,m,n,alpha,A.data(),lda,R.data(),ldb));
   SELFTEST_CALL("dtrmm",m,n,0,side,u,t,d,0,
     __wrap_dtrmm_(&side,&u,&t,&d,&m,&n,&alpha,A.data(),&lda,B.data(),&ldb));
#ifdef ILLIXR_DIAGNOSTIC_TRSM_CAPTURE
   const auto prior_errors=diagnostic_errors;
#endif
   for(unsigned i=0;i<B.size();++i) good=equal(B[i],R[i])&&good;
   const double inverse_alpha=1./alpha;
#ifdef ILLIXR_DIAGNOSTIC_TRSM_CAPTURE
   const bool capture=!solve_capture.frozen && dim==47;
   const auto before_solve_errors=diagnostic_errors;
   if(capture) {
    auto &s=solve_capture;
    __ASSERT_NO_MSG(A.size()<=8192 && B.size()<=8192);
    s.mode=diagnostic_mode;s.id=diagnostic_case+1;s.input_errors=diagnostic_errors-prior_errors;
    s.m=m;s.n=n;s.lda=lda;s.ldb=ldb;s.side=side;s.u=u;s.t=t;s.d=d;
    std::memcpy(s.a,A.data(),A.size()*sizeof(double));
    std::memcpy(s.before,B.data(),B.size()*sizeof(double));
   }
#endif
   SELFTEST_CALL("dtrsm",m,n,0,side,u,t,d,0,
     __wrap_dtrsm_(&side,&u,&t,&d,&m,&n,&inverse_alpha,A.data(),&lda,B.data(),&ldb));
   for(unsigned i=0;i<B.size();++i) good=equal(B[i],value(i+2))&&good;
#ifdef ILLIXR_DIAGNOSTIC_TRSM_CAPTURE
   if(capture && diagnostic_errors>before_solve_errors) {
    std::memcpy(solve_capture.after,B.data(),B.size()*sizeof(double));
    solve_capture.frozen=true;
   }
#endif
   // The same triangular matrix also exercises matrix-vector dispatch.
   const int inc=2; std::vector<double>x(order*inc),y(order*inc);
   for(unsigned i=0;i<x.size();++i) x[i]=y[i]=value(i+3);
   for(int i=0;i<order;++i) { double sum=0; for(int j=0;j<order;++j) sum+=a(i,j)*x[j*inc]; y[i*inc]=sum; }
   SELFTEST_CALL("dtrmv",order,0,0,u,t,d,'-',inc,
     __wrap_dtrmv_(&u,&t,&d,&order,A.data(),&lda,x.data(),&inc));
   for(unsigned i=0;i<x.size();++i) good=equal(x[i],y[i])&&good;
   SELFTEST_CALL("daxpy",order,0,0,'-','-','-','-',inc,
     __wrap_daxpy_(&order,&alpha,x.data(),&inc,y.data(),&inc));
   for(int i=0;i<order;++i) good=equal(y[i*inc],(1.+alpha)*x[i*inc])&&good;
  }
 }
#if defined(ILLIXR_DIAGNOSTIC_BLAS_FAILURES) && !defined(ILLIXR_DIAGNOSTIC_BLAS_PROGRESS)
 printf("ILLIXR_BLAS_FAILURES errors=%u stored=%u\n",failure_count,std::min(failure_count,16u));
 for(unsigned i=0;i<std::min(failure_count,16u);++i) {
  const auto &r=failure_records[i];const auto &c=r.context;
  printf("ILLIXR_BLAS_MISMATCH check=%u case=%u op=%s m=%d n=%d k=%d flags=%s inc=%d actual=%016llx expected=%016llx\n",
   r.check,c.id,c.operation,c.m,c.n,c.k,c.flags,c.inc,
   static_cast<unsigned long long>(r.actual),static_cast<unsigned long long>(r.expected));
 }
#endif
#ifdef ILLIXR_DIAGNOSTIC_BLAS_AB
 printf("ILLIXR_BLAS_AB_PHASE mode=%u irq_mask=%u fence=%u passed=%u checks=%u errors=%u\n",
        diagnostic_mode,diagnostic_mode&1u,(diagnostic_mode>>1)&1u,unsigned(good),checks,diagnostic_errors);
#else
 printf("ILLIXR_BLAS_SELFTEST {\"passed\":%s,\"checks\":%u}\n",good?"true":"false",checks);
#endif
#ifdef ILLIXR_USE_RVV_BLAS
 dump_kernels("selftest");
 for(auto &s:kernel_stats) s={};
#endif
 for(auto &s:stats) s={};
 allocations=0;
#ifdef ILLIXR_USE_GEMMINI_BLAS
 gemmini_backend::dump("selftest",true);
#endif
 return good;
}
#ifdef ILLIXR_DIAGNOSTIC_BLAS_AB
bool self_test() {
 bool passed=true;
 // Repeat baseline last to expose ordering/warm-state effects within one ELF.
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
 for(unsigned mode : {0u,1u,9u,5u,17u,1u,33u,5u,9u,0u}) {
#elif defined(ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB)
 for(unsigned mode : {0u,1u,4u,5u,0u}) {
#else
 for(unsigned mode : {0u,1u,2u,3u,0u}) {
#endif
  diagnostic_mode=mode;
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
  const unsigned action=diagnostic_kernel_action();
  printf("ILLIXR_KERNEL_DELAY_MODE mode=%u action=%u nops=%u fence=%u\n",mode,action,action==1?1u:action==2?16u:action==3?64u:0u,unsigned(action==4));
#endif
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB
  printf("ILLIXR_KERNEL_FENCE_MODE mode=%u fence_at_gemm_return=%u\n",mode,unsigned((mode&4u)!=0));
#endif
  printf("ILLIXR_BLAS_AB_START mode=%u irq_mask=%u fence=%u\n",mode,mode&1u,(mode>>1)&1u);
  fflush(stdout);
  passed=self_test_phase() && passed;
 }
#ifdef ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB
 printf("ILLIXR_BLAS_SELFTEST {\"passed\":%s,\"checks\":3495360}\n",passed?"true":"false");
#else
 printf("ILLIXR_BLAS_SELFTEST {\"passed\":%s,\"checks\":1747680}\n",passed?"true":"false");
#endif
#ifdef ILLIXR_DIAGNOSTIC_TRSM_CAPTURE
 dump_solve_capture();
#endif
 return passed;
}
#endif
void dump() {
#ifdef ILLIXR_USE_GEMMINI_BLAS
 gemmini_backend::shutdown();
 gemmini_backend::dump("work");
#endif
#ifdef ILLIXR_USE_RVV_BLAS
 dump_kernels("work");
#endif
 const char *names[]={"dgemm","dgemv","dtrmm","dtrmv","dtrsm","daxpy","sgemm","sgemv","strmm","strmv","strsm","saxpy"};
 for(unsigned i=0;i<12;++i) { const auto&s=stats[i];
  trace_output::print("ILLIXR_BLAS_WORK {\"name\":\"%s\",\"calls\":%llu,\"cycles\":%llu,\"wait_cycles\":%llu,\"counter_version\":2,\"cycles_samples\":%llu,\"cycles_migrated\":%llu,\"wait_cycles_migrated\":%llu,\"elapsed_ns\":%llu,\"wait_ns\":%llu,\"max_m\":%u,\"max_n\":%u,\"max_k\":%u,\"hart_mask\":%u,\"harts\":[%llu,%llu,%llu,%llu]}\n",names[i],(unsigned long long)s.calls,(unsigned long long)s.cycles,(unsigned long long)s.wait_cycles,(unsigned long long)s.cycles_samples,(unsigned long long)s.cycles_migrated,(unsigned long long)s.wait_cycles_migrated,(unsigned long long)s.elapsed_ns,(unsigned long long)s.wait_ns,s.max_m,s.max_n,s.max_k,s.mask,(unsigned long long)s.harts[0],(unsigned long long)s.harts[1],(unsigned long long)s.harts[2],(unsigned long long)s.harts[3]);
 }
 trace_output::print("ILLIXR_BLAS_MEMORY {\"allocations\":%llu,\"reserved_bytes\":%zu,\"outstanding\":%u}\n",(unsigned long long)allocations,arena_size,unsigned(allocated));
}
}
#endif
