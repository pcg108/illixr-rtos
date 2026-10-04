#pragma once
#include "gemmini_packing.hpp"
#include "gemmini_packing_rvv.h"
#include <type_traits>
namespace ILLIXR::gemmini_backend {
enum class Traversal { rows, source_contiguous };
template<class S,class D> void vector_copy(size_t rows,size_t cols,const S *src,ptrdiff_t sr,ptrdiff_t sc,D *dst,ptrdiff_t dr,ptrdiff_t dc,Traversal traversal) {
 if(!rows || !cols)return;
 if(traversal==Traversal::source_contiguous && sr==1 && sc!=1){std::swap(rows,cols);std::swap(sr,sc);std::swap(dr,dc);}
 for(size_t i=0;i<rows;++i){
  const S *s=src+ptrdiff_t(i)*sr;D *d=dst+ptrdiff_t(i)*dr;
  if constexpr(std::is_same_v<S,double>) illixr_pack_d2f(cols,s,sc,d,dc);
  else if constexpr(std::is_same_v<D,double>) illixr_pack_f2d(cols,s,sc,d,dc);
  else illixr_pack_f2f(cols,s,sc,d,dc);
 }
}
template<class T> void pack_vector(const Request&r,float*a,float*b,float*c,Traversal t=Traversal::rows){
 vector_copy(r.m,r.k,static_cast<const T*>(r.a),r.ar,r.ac,a,ptrdiff_t(r.k),1,t);
 vector_copy(r.k,r.n,static_cast<const T*>(r.b),r.br,r.bc,b,ptrdiff_t(r.n),1,t);
 if(r.beta==0)illixr_pack_zero(r.m*r.n,c);
 else vector_copy(r.m,r.n,static_cast<const T*>(r.c),r.cr,r.cc,c,ptrdiff_t(r.n),1,t);
}
template<class T> void unpack_vector(const Request&r,const float*c,Traversal t=Traversal::rows){
 if(t==Traversal::source_contiguous && r.cr==1 && r.cc!=1)
  vector_copy(r.n,r.m,c,1,ptrdiff_t(r.n),static_cast<T*>(r.c),r.cc,r.cr,Traversal::rows);
 else vector_copy(r.m,r.n,c,ptrdiff_t(r.n),1,static_cast<T*>(r.c),r.cr,r.cc,Traversal::rows);
}
}
