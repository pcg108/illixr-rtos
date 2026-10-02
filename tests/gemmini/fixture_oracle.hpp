#pragma once
// Standalone fixtures only. Production firmware never includes this header.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "trace_output.hpp"

namespace fixture_oracle {
struct Product { uint64_t expected, scale; int k; bool exact; };
struct Triangle { uint64_t input_hash; unsigned offset, count; };
#if !defined(ILLIXR_ORACLE_CAPTURE)
extern const Product products[];
extern const Triangle triangles[];
extern const uint64_t triangle_values[];
extern const size_t product_count, triangle_count;
#endif
inline size_t product_cursor=0, triangle_cursor=0;
inline unsigned errors=0;
#if defined(ILLIXR_ORACLE_CAPTURE)
inline char capture_buffer[16384];
inline size_t capture_used=0;
inline void flush_capture() {
 if(capture_used && !ILLIXR::trace_output::transport_write(capture_buffer,capture_used)) ++errors;
 capture_used=0;
}
template<class... Args> inline void capture_print(const char *format,Args... args) {
 if(capture_used+256>sizeof(capture_buffer)) flush_capture();
 const int size=std::snprintf(capture_buffer+capture_used,sizeof(capture_buffer)-capture_used,format,args...);
 if(size<0 || size>=int(sizeof(capture_buffer)-capture_used)) {++errors;return;}
 capture_used+=size;
}
#endif
inline uint64_t bits(double value) { uint64_t b;std::memcpy(&b,&value,8);return b; }
inline double value(uint64_t bits) { double d;std::memcpy(&d,&bits,8);return d; }
inline bool product(double &expected,double &scale,int k,bool exact) {
#if defined(ILLIXR_ORACLE_CAPTURE)
 capture_print("ILLIXR_FIXTURE_G %016llx %016llx %d %u\n",
   (unsigned long long)bits(expected),(unsigned long long)bits(scale),k,unsigned(exact));
 ++product_cursor;return true;
#else
 if(product_cursor>=product_count) {++errors;return false;}
 const auto &r=products[product_cursor++];
 if(r.k!=k || r.exact!=exact) {++errors;return false;}
#if defined(ILLIXR_ORACLE_VERIFY)
 if(bits(expected)!=r.expected || bits(scale)!=r.scale) {
  if(errors++<8) std::printf("ILLIXR_FIXTURE_MISMATCH product=%zu\n",product_cursor-1);
  return false;
 }
#else
 expected=value(r.expected);scale=value(r.scale);
#endif
 return true;
#endif
}
inline uint64_t mix(uint64_t hash,uint64_t word) {
 for(unsigned i=0;i<8;++i) {hash^=(word>>(8*i))&255;hash*=1099511628211ull;}
 return hash;
}
inline uint64_t triangle_key(char side,char u,char t,char d,int m,int n,double alpha,
 const double *a,int lda,const double *b,int ldb) {
 uint64_t h=14695981039346656037ull;
 for(uint64_t v:{uint64_t(side),uint64_t(u),uint64_t(t),uint64_t(d),uint64_t(m),
                 uint64_t(n),bits(alpha),uint64_t(lda),uint64_t(ldb)}) h=mix(h,v);
 for(int i=0;i<lda*(side=='L'?m:n);++i) h=mix(h,bits(a[i]));
 for(int i=0;i<ldb*n;++i) h=mix(h,bits(b[i]));
 return h;
}
inline void triangle(uint64_t hash,double *b,unsigned count) {
#if defined(ILLIXR_ORACLE_CAPTURE)
 capture_print("ILLIXR_FIXTURE_T %016llx %u\n",(unsigned long long)hash,count);
 for(unsigned i=0;i<count;++i)
  capture_print("ILLIXR_FIXTURE_V %016llx\n",(unsigned long long)bits(b[i]));
 ++triangle_cursor;
#else
 if(triangle_cursor>=triangle_count) {++errors;std::abort();}
 const auto &r=triangles[triangle_cursor++];
 if(r.input_hash!=hash || r.count!=count) {++errors;std::abort();}
 for(unsigned i=0;i<count;++i) {
#if defined(ILLIXR_ORACLE_VERIFY)
  if(bits(b[i])!=triangle_values[r.offset+i]) {
   if(errors++<8) std::printf("ILLIXR_FIXTURE_MISMATCH triangle=%zu index=%u\n",triangle_cursor-1,i);
  }
#else
  b[i]=value(triangle_values[r.offset+i]);
#endif
 }
#endif
}
inline bool finish() {
#if defined(ILLIXR_ORACLE_CAPTURE)
 flush_capture();
 const char *mode="capture";
#elif defined(ILLIXR_ORACLE_VERIFY)
 const char *mode="verify";
#else
 const char *mode="table";
#endif
#if !defined(ILLIXR_ORACLE_CAPTURE)
 if(product_cursor!=product_count || triangle_cursor!=triangle_count) ++errors;
#endif
 std::printf("ILLIXR_FIXTURE_ORACLE {\"mode\":\"%s\",\"passed\":%s,\"products\":%zu,\"triangles\":%zu,\"errors\":%u}\n",
  mode,errors?"false":"true",product_cursor,triangle_cursor,errors);
 return errors==0;
}
}
