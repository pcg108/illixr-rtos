// Check the supplemental fixture's closed-form expectations against a simple
// independent BLAS implementation on the host before using them on hardware.
#include <algorithm>
#include <cassert>
#include <cstddef>
template<class T> void gemm(const char *ta,const char *tb,const int *m,const int *n,
 const int *k,const T *alpha,const T *a,const int *lda,const T *b,const int *ldb,
 const T *beta,T *c,const int *ldc) {
 for(int j=0;j<*n;++j)for(int i=0;i<*m;++i) {
  T product=0;
  if(*alpha!=0)for(int l=0;l<*k;++l)
   product+=a[*ta=='N'?i+l**lda:l+i**lda]*b[*tb=='N'?l+j**ldb:j+l**ldb];
  c[i+j**ldc]=*alpha*product+(*beta==0?T(0):*beta*c[i+j**ldc]);
 }
}
template<class T> void gemv(const char *t,const int *m,const int *n,const T *alpha,
 const T *a,const int *lda,const T *x,const int *ix,const T *beta,T *y,const int *iy) {
 if(!*m || !*n)return;
 const int nx=*t=='N'?*n:*m,ny=*t=='N'?*m:*n;
 const int x0=*ix<0?(1-nx)**ix:0,y0=*iy<0?(1-ny)**iy:0;
 for(int i=0;i<ny;++i) {
  T product=0;
  if(*alpha!=0)for(int j=0;j<nx;++j)
   product+=a[*t=='N'?i+j**lda:j+i**lda]*x[x0+j**ix];
  y[y0+i**iy]=*alpha*product+(*beta==0?T(0):*beta*y[y0+i**iy]);
 }
}
#define WRAP(P,T) \
extern "C" void __wrap_##P##gemm_(const char*a,const char*b,const int*m,const int*n,const int*k,const T*alpha,const T*A,const int*lda,const T*B,const int*ldb,const T*beta,T*C,const int*ldc) {gemm(a,b,m,n,k,alpha,A,lda,B,ldb,beta,C,ldc);} \
extern "C" void __wrap_##P##gemv_(const char*t,const int*m,const int*n,const T*alpha,const T*A,const int*lda,const T*x,const int*ix,const T*beta,T*y,const int*iy) {gemv(t,m,n,alpha,A,lda,x,ix,beta,y,iy);}
WRAP(s,float)
WRAP(d,double)
bool gemmini_edge_cases();
int main() {assert(gemmini_edge_cases());}
