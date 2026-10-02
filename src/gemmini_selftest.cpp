#include "gemmini_backend.hpp"
#include "gemmini_packing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <zephyr/kernel.h>

#define DECLARE(P,T) \
extern "C" void __wrap_##P##gemm_(const char*,const char*,const int*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*); \
extern "C" void __wrap_##P##gemv_(const char*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*);
DECLARE(s,float)
DECLARE(d,double)
bool gemmini_edge_cases();
namespace ILLIXR::gemmini_backend {
namespace {
template<class T> struct Blas;
#define CALLS(P,T) template<> struct Blas<T> { \
 static constexpr auto gemm=__wrap_##P##gemm_; \
 static constexpr auto gemv=__wrap_##P##gemv_; };
CALLS(s,float)
CALLS(d,double)
unsigned errors=0;
bool check(double actual,double expected,double scale,int k,bool exact,unsigned &checks) {
 ++checks;
 // Includes input conversion, scaling and accumulation. This fixed analytical
 // bound applies to FP32 GEMM/GEMV only; unaffected BLAS keeps FP64 tolerance.
 const double u=0x1p-24,nu=(4.*k+12.)*u;
 const double bound=exact ? 0 : nu/(1-nu)*scale+32.*std::numeric_limits<float>::denorm_min();
 const bool good=std::isfinite(actual) && std::abs(actual-expected)<=bound;
 if(!good && errors++<8) printf("ILLIXR_GEMMINI_MISMATCH actual=%.17g expected=%.17g bound=%.17g k=%d\n",actual,expected,bound,k);
 return good;
}
template<class T> bool products(unsigned &checks) {
 bool good=true;
 for(bool exact : {true,false}) for(int dim : {1,3,4,5,15,17,47,105}) {
  auto value=[=](int i) { return T(((i*17+11)%41-20)/(exact?16.:23.)); };
  const int m=dim,n=dim+2,k=dim+1;
  const T alpha=exact?T(.5):T(.7),beta=exact?T(-1):T(-.2);
  for(char ta : {'N','T'}) for(char tb : {'N','T'}) {
   const int lda=(ta=='N'?m:k)+3,ldb=(tb=='N'?k:n)+2,ldc=m+2;
   std::vector<T>a(lda*(ta=='N'?k:m)),b(ldb*(tb=='N'?n:k)),c(ldc*n),before;
   for(size_t i=0;i<a.size();++i)a[i]=value(i);
   for(size_t i=0;i<b.size();++i)b[i]=value(i+5);
   for(size_t i=0;i<c.size();++i)c[i]=value(i+13);
   before=c;
   Blas<T>::gemm(&ta,&tb,&m,&n,&k,&alpha,a.data(),&lda,b.data(),&ldb,&beta,c.data(),&ldc);
   for(int j=0;j<n;++j) for(int i=0;i<ldc;++i) {
    double expected=before[i+j*ldc],scale=std::abs(expected);
    if(i<m) {
     double sum=0,absolute=0;
     for(int l=0;l<k;++l) {
      double p=double(a[ta=='N'?i+l*lda:l+i*lda])*double(b[tb=='N'?l+j*ldb:j+l*ldb]);
      sum+=p;absolute+=std::abs(p);
     }
     expected=double(alpha)*sum+double(beta)*expected;
     scale=std::abs(double(alpha))*absolute+std::abs(double(beta)*double(before[i+j*ldc]));
    }
    good=check(c[i+j*ldc],expected,scale,k,exact||i>=m,checks)&&good;
   }
  }
  for(char t : {'N','T'}) for(int ix : {1,2,-1,-2}) for(int iy : {1,-2}) {
   const int lda=m+3,nx=t=='N'?n:m,ny=t=='N'?m:n;
   const int x0=ix<0?(1-nx)*ix:0,y0=iy<0?(1-ny)*iy:0;
   std::vector<T>a(lda*n),x(nx*std::abs(ix)),y(ny*std::abs(iy)),before;
   for(size_t i=0;i<a.size();++i)a[i]=value(i);
   for(size_t i=0;i<x.size();++i)x[i]=value(i+3);
   for(size_t i=0;i<y.size();++i)y[i]=value(i+7);
   before=y;
   Blas<T>::gemv(&t,&m,&n,&alpha,a.data(),&lda,x.data(),&ix,&beta,y.data(),&iy);
   for(int i=0;i<ny;++i) {
    double sum=0,absolute=0;
    for(int j=0;j<nx;++j) { double p=double(a[t=='N'?i+j*lda:j+i*lda])*double(x[x0+j*ix]);sum+=p;absolute+=std::abs(p); }
    const double bias=double(beta)*before[y0+i*iy];
    good=check(y[y0+i*iy],double(alpha)*sum+bias,std::abs(double(alpha))*absolute+std::abs(bias),nx,exact,checks)&&good;
   }
   for(size_t i=0;i<y.size();++i) if(i%std::abs(iy)) good=check(y[i],before[i],0,0,true,checks)&&good;
  }
 }
 // alpha=0 / K=0 must not access A/B; beta=0 must not read poisoned C.
 for(int k : {0,3}) for(T alpha : {T(0),T(1)}) for(T beta : {T(0),T(1),T(-.5)}) {
  const char n='N';const int m=3,cols=2,lda=3,ldb=3,ldc=4;
  T a[9]{},b[6]{},c[8];
  for(auto &v:c)v=beta==0?std::numeric_limits<T>::quiet_NaN():T(2);
  Blas<T>::gemm(&n,&n,&m,&cols,&k,&alpha,a,&lda,b,&ldb,&beta,c,&ldc);
  for(int j=0;j<cols;++j)for(int i=0;i<m;++i)good=check(c[i+j*ldc],beta==0?0:2.*beta,0,k,true,checks)&&good;
 }
 return good;
}
constexpr unsigned workers=CONFIG_MP_MAX_NUM_CPUS;
K_THREAD_STACK_ARRAY_DEFINE(stacks,workers,4096);
k_thread threads[workers];
bool worker_good[workers];
K_SEM_DEFINE(migration_ready,0,1);
K_SEM_DEFINE(migration_resume,0,1);
unsigned migrations=0;
bool migration_good=true;
void migrate(void*,void*,void*) {
 for(unsigned r=0;r<4*workers;++r) {
  k_sem_give(&migration_ready);
  if(k_sem_take(&migration_resume,K_SECONDS(5))) {migration_good=false;return;}
  unsigned long hart;asm volatile("csrr %0,mhartid":"=r"(hart));
  if(hart!=(r+1)%workers)migration_good=false;
  const char n='N';const int dim=2;const double one=1,zero=0;
  const double a[]={1,2,3,4},b[]={2,0,0,2};double c[4]{};
  Blas<double>::gemm(&n,&n,&dim,&dim,&dim,&one,a,&dim,b,&dim,&zero,c,&dim);
  for(unsigned i=0;i<4;++i)if(c[i]!=2*a[i])migration_good=false;
  ++migrations;
 }
}
void compete(void *arg,void*,void*) {
 const auto id=reinterpret_cast<uintptr_t>(arg);
 bool good=true;
 for(unsigned r=0;r<32;++r) {
  const char n='N';const int dim=2;const double one=1,zero=0;
  const double a[]={1,2,3,4},b[]={2,0,0,2};double c[4]{};
  Blas<double>::gemm(&n,&n,&dim,&dim,&dim,&one,a,&dim,b,&dim,&zero,c,&dim);
  for(unsigned i=0;i<4;++i)good=good&&(c[i]==2*a[i]);
  k_sleep(K_TICKS(1));
 }
 worker_good[id]=good;
}
}
bool self_test(unsigned &checks) {
 errors=0;
 bool good=products<float>(checks);good=products<double>(checks)&&good;
 Request huge{};huge.m=SIZE_MAX;huge.k=2;size_t bytes=0;
 good=!workspace_bytes(huge,bytes)&&good;++checks;
 for(unsigned h=0;h<workers;++h) {
  auto tid=k_thread_create(&threads[h],stacks[h],K_THREAD_STACK_SIZEOF(stacks[h]),compete,
    reinterpret_cast<void*>(uintptr_t(h)),nullptr,nullptr,K_PRIO_PREEMPT(5),0,K_FOREVER);
#ifdef CONFIG_SMP
  if(k_thread_cpu_pin(tid,h))return false;
#endif
  k_thread_start(tid);
 }
 for(unsigned h=0;h<workers;++h) { if(k_thread_join(&threads[h],K_SECONDS(30))) return false;good=worker_good[h]&&good;checks+=128; }
#ifdef CONFIG_SMP
 auto tid=k_thread_create(&threads[0],stacks[0],K_THREAD_STACK_SIZEOF(stacks[0]),migrate,
    nullptr,nullptr,nullptr,K_PRIO_PREEMPT(5),0,K_FOREVER);
 if(k_thread_cpu_pin(tid,0))return false;
 k_thread_start(tid);
 for(unsigned r=0;r<4*workers;++r) {
  if(k_sem_take(&migration_ready,K_SECONDS(5)))return false;
  k_thread_suspend(tid);
  if(k_thread_cpu_pin(tid,(r+1)%workers))return false;
  k_thread_resume(tid);k_sem_give(&migration_resume);
 }
 if(k_thread_join(tid,K_SECONDS(5)))return false;
 good=migration_good&&migrations==4*workers&&good;checks+=16*workers;
#endif
 good=gemmini_edge_cases()&&good;
 checks+=46552;
 printf("ILLIXR_GEMMINI_SELFTEST {\"fixture_version\":2,\"passed\":%s,\"checks\":%u,\"errors\":%u,\"caller_hart_mask\":%u,\"caller_migrations\":%u,\"bound\":\"gamma(4*k+12)*absolute_products\"}\n",good?"true":"false",checks,errors,(1u<<workers)-1,migrations);
 return good;
}
}
