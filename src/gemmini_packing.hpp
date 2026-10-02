#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <initializer_list>

namespace ILLIXR::gemmini_backend {
// Strides are in elements. Pointers already address the first logical element.
struct Request {
 bool is_double;
 unsigned operation;
 size_t m,n,k;
 double alpha,beta;
 const void *a,*b;
 void *c;
 ptrdiff_t ar,ac,br,bc,cr,cc;
 unsigned caller_hart;
};
inline bool workspace_bytes(const Request &r, size_t &bytes) {
 size_t elements=0;
 for (auto pair : {std::pair<size_t,size_t>{r.m,r.k},{r.k,r.n},{r.m,r.n}}) {
  if (pair.first && pair.second > (std::numeric_limits<size_t>::max()-elements)/pair.first) return false;
  elements += pair.first*pair.second;
 }
 if (elements > std::numeric_limits<size_t>::max()/sizeof(float)) return false;
 bytes=elements*sizeof(float); return true;
}
template<class T> void pack(const Request &r,float *a,float *b,float *c) {
 const auto *src_a=static_cast<const T*>(r.a),*src_b=static_cast<const T*>(r.b);
 const auto *src_c=static_cast<const T*>(r.c);
 for(size_t i=0;i<r.m;++i) for(size_t k=0;k<r.k;++k)
  a[i*r.k+k]=float(src_a[ptrdiff_t(i)*r.ar+ptrdiff_t(k)*r.ac]);
 for(size_t k=0;k<r.k;++k) for(size_t j=0;j<r.n;++j)
  b[k*r.n+j]=float(src_b[ptrdiff_t(k)*r.br+ptrdiff_t(j)*r.bc]);
 for(size_t i=0;i<r.m;++i) for(size_t j=0;j<r.n;++j)
  c[i*r.n+j]=r.beta==0 ? 0.f : float(src_c[ptrdiff_t(i)*r.cr+ptrdiff_t(j)*r.cc]);
}
template<class T> void unpack(const Request &r,const float *c) {
 auto *dst=static_cast<T*>(r.c);
 for(size_t i=0;i<r.m;++i) for(size_t j=0;j<r.n;++j)
  dst[ptrdiff_t(i)*r.cr+ptrdiff_t(j)*r.cc]=T(c[i*r.n+j]);
}
template<class T> void scale_only(const Request &r) {
 if(r.beta==1) return;
 auto *dst=static_cast<T*>(r.c);
 for(size_t i=0;i<r.m;++i) for(size_t j=0;j<r.n;++j) {
  auto &v=dst[ptrdiff_t(i)*r.cr+ptrdiff_t(j)*r.cc];
  v=r.beta==0 ? T(0) : T(r.beta)*v;
 }
}
}
