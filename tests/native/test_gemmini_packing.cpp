#include "../../src/gemmini_packing.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace ILLIXR::gemmini_backend;
int main() {
 // Padded column-major A/B and C: C = A*B, preserving C padding.
 const double a[]={1,2,99,3,4,99},b[]={5,6,99,7,8,99};
 double c[]={NAN,NAN,77,NAN,NAN,77};float ap[4],bp[4],cp[4];
 Request r{true,1,2,2,2,1,0,a,b,c,1,3,1,3,1,3,0};
 size_t bytes;assert(workspace_bytes(r,bytes)&&bytes==48);
 pack<double>(r,ap,bp,cp);
 assert(ap[0]==1&&ap[1]==3&&ap[2]==2&&ap[3]==4);
 for(auto v:cp)assert(v==0);
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<2;++j)
  for(unsigned k=0;k<2;++k)cp[2*i+j]+=ap[2*i+k]*bp[2*k+j];
 unpack<double>(r,cp);
 assert(c[0]==23&&c[1]==34&&c[3]==31&&c[4]==46&&c[2]==77&&c[5]==77);
 // Negative x/y strides and transposed A, independent of matrix padding.
 double x[]={2,9,1},y[]={NAN,77,NAN};
 r={true,3,2,1,2,1,0,a,x+2,y+2,3,1,-2,0,-2,0,0};
 pack<double>(r,ap,bp,cp);
 assert(ap[0]==1&&ap[1]==2&&ap[2]==3&&ap[3]==4&&bp[0]==1&&bp[1]==2);
 cp[0]=5;cp[1]=11;unpack<double>(r,cp);
 assert(y[0]==11&&y[1]==77&&y[2]==5);
 r.alpha=0;r.beta=0;scale_only<double>(r);assert(y[0]==0&&y[1]==77&&y[2]==0);
 r.m=SIZE_MAX;r.k=2;assert(!workspace_bytes(r,bytes));
 r.m=SIZE_MAX/4;r.k=1;r.n=1;assert(!workspace_bytes(r,bytes));
}
