// Supplemental fixtures through the actual linked BLAS entry points. The
// complete platform, context, concurrency and numerical suite is separate.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#define DECLARE(P,T) \
extern "C" void __wrap_##P##gemm_(const char*,const char*,const int*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*); \
extern "C" void __wrap_##P##gemv_(const char*,const int*,const int*,const T*,const T*,const int*,const T*,const int*,const T*,T*,const int*);
DECLARE(s,float)
DECLARE(d,double)
namespace {
template<class T> struct Blas;
#define CALLS(P,T) template<> struct Blas<T> { \
 static constexpr auto gemm=__wrap_##P##gemm_; \
 static constexpr auto gemv=__wrap_##P##gemv_; };
CALLS(s,float)
CALLS(d,double)
unsigned checks{},errors{},cases{};
void equal(double actual,double expected) {
 ++checks;
 if(!std::isfinite(actual) || actual!=expected) {
  if(errors++<8) printf("ILLIXR_GEMMINI_EDGE_MISMATCH case=%u actual=%.17g expected=%.17g\n",cases,actual,expected);
 }
}
template<class T> void boundaries() {
 // All dimensions and leading dimensions are valid, including quick returns.
 // Null product inputs ensure alpha=0/K=0 cannot accidentally dereference them.
 for(char ta:{'N','T'}) for(char tb:{'N','T'})
  for(int m:{0,3}) for(int n:{0,4}) for(int k:{0,2})
   for(T beta:{T(0),T(1),T(-.5)}) {
    ++cases;
    const int lda=std::max(1,ta=='N'?m:k),ldb=std::max(1,tb=='N'?k:n),ldc=4;
    const T alpha=0;
    T c[16];std::fill_n(c,16,T(2));
    Blas<T>::gemm(&ta,&tb,&m,&n,&k,&alpha,nullptr,&lda,nullptr,&ldb,&beta,c,&ldc);
    for(int j=0;j<4;++j)for(int i=0;i<4;++i)
     equal(c[i+j*ldc],i<m && j<n?2*double(beta):2);
   }
 // beta=0 must not consume NaN output, even on a zero-product fast path.
 for(int k:{0,2}) {
  ++cases;
  const char n='N';const int m=3,cols=4,lda=3,ldb=2,ldc=4;
  const T alpha=k?T(0):T(1),zero=0;
  T c[16];std::fill_n(c,16,std::numeric_limits<T>::quiet_NaN());
  Blas<T>::gemm(&n,&n,&m,&cols,&k,&alpha,nullptr,&lda,nullptr,&ldb,&zero,c,&ldc);
  for(int j=0;j<cols;++j)for(int i=0;i<m;++i)equal(c[i+j*ldc],0);
 }
 for(char t:{'N','T'}) for(int m:{0,3}) for(int n:{0,4})
  for(int ix:{1,-2}) for(int iy:{1,2,-1,-2})
   for(T beta:{T(0),T(1),T(-.5)}) {
    ++cases;
    const int lda=std::max(1,m),ny=t=='N'?m:n;
    const int y0=iy<0?(1-ny)*iy:0;
    const T alpha=0;
    T y[16];std::fill_n(y,16,T(2));
    Blas<T>::gemv(&t,&m,&n,&alpha,nullptr,&lda,nullptr,&ix,&beta,y,&iy);
    T expected[16];std::fill_n(expected,16,T(2));
    // BLAS quick returns on either zero dimension, before applying beta.
    if(m && n)for(int i=0;i<ny;++i)expected[y0+i*iy]=T(2)*beta;
    for(int i=0;i<16;++i)equal(y[i],expected[i]);
   }
 // Nondegenerate GEMV must also ignore a poisoned old output with beta=0.
 for(char t:{'N','T'}) for(int iy:{1,-2}) {
  ++cases;
  const int m=3,n=4,lda=5,incx=-2,nx=t=='N'?n:m,ny=t=='N'?m:n;
  const int x0=(1-nx)*incx,y0=iy<0?(1-ny)*iy:0;
  const T alpha=T(.5),zero=0;
  T a[20]{},x[8]{},y[8];std::fill_n(y,8,std::numeric_limits<T>::quiet_NaN());
  for(int j=0;j<n;++j)for(int i=0;i<m;++i)a[i+j*lda]=T(i+2*j);
  for(int i=0;i<nx;++i)x[x0+i*incx]=1;
  Blas<T>::gemv(&t,&m,&n,&alpha,a,&lda,x,&incx,&zero,y,&iy);
  for(int i=0;i<ny;++i) {
   // Sum of arithmetic progressions, independent of the packing/index loops.
   const int sum=t=='N'?n*i+n*(n-1):m*(m-1)/2+2*m*i;
   equal(y[y0+i*iy],double(alpha)*sum);
  }
 }
}
template<class T> void observed_size(char transpose) {
 // 135 is the largest dimension observed in the quad OpenVINS Spike trace.
 // Dyadic inputs keep all intermediates exactly representable in FP32.
 ++cases;
 const int n=135,ld=n+3;
 const T alpha=T(.5),beta=T(-.25);
 std::vector<T>a(ld*n,T(77)),b(ld*n,T(77)),c(ld*n,T(77));
 double sum_p=0,sum_q=0,sum_pq=0;
 for(int k=0;k<n;++k) {
  const double p=(k%5-2)*.125,q=(k%7-3)*.125;
  sum_p+=p;sum_q+=q;sum_pq+=p*q;
 }
 for(int j=0;j<n;++j)for(int i=0;i<n;++i) {
  const int pos=transpose=='N'?i+j*ld:j+i*ld;
  a[pos]=T((i%3-1)*.25+(j%5-2)*.125);
  b[pos]=T((i%7-3)*.125+(j%4-1)*.5);
  c[i+j*ld]=T((i+j)%5-2);
 }
 Blas<T>::gemm(&transpose,&transpose,&n,&n,&n,&alpha,a.data(),&ld,b.data(),&ld,&beta,c.data(),&ld);
 for(int j=0;j<n;++j)for(int i=0;i<ld;++i) {
  double expected=77;
  if(i<n) {
   const double x=(i%3-1)*.25,y=(j%4-1)*.5;
   expected=.5*(x*sum_q+n*x*y+sum_pq+y*sum_p)-.25*((i+j)%5-2);
  }
  equal(c[i+j*ld],expected);
 }
}
}
bool gemmini_edge_cases() {
 checks=errors=cases=0;
 boundaries<float>();boundaries<double>();
 observed_size<float>('N');observed_size<double>('T');
 printf("ILLIXR_GEMMINI_EDGE {\"passed\":%s,\"cases\":%u,\"checks\":%u,\"errors\":%u,\"largest_dim\":135,\"reference\":\"exact_dyadic_and_closed_form\"}\n",
        errors?"false":"true",cases,checks,errors);
 return errors==0;
}
