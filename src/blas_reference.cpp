// A separate namespace isolates this Eigen-only numerical oracle from the
// application's BLAS-enabled Eigen instantiations (including inline symbols).
#undef EIGEN_USE_BLAS
#define Eigen IllixrBlasReferenceEigen
#include <Eigen/Core>
#undef Eigen
#include "blas_reference.hpp"
#include <cstdlib>

namespace ILLIXR::blas_reference {
using Matrix = IllixrBlasReferenceEigen::MatrixXd;
using Vector = IllixrBlasReferenceEigen::VectorXd;
static Matrix matrix(const double *a, int rows, int cols, int stride) {
  Matrix result(rows, cols);
  for (int j=0; j<cols; ++j)
    for (int i=0; i<rows; ++i) result(i,j)=a[i+j*stride];
  return result;
}
void gemm(char ta, char tb, int m, int n, int k, double alpha,
          const double *a, int lda, const double *b, int ldb,
          double beta, double *c, int ldc) {
  Matrix left=matrix(a,ta=='N'?m:k,ta=='N'?k:m,lda);
  Matrix right=matrix(b,tb=='N'?k:n,tb=='N'?n:k,ldb);
  if(ta=='T') left.transposeInPlace();
  if(tb=='T') right.transposeInPlace();
  Matrix result=alpha*(left*right)+beta*matrix(c,m,n,ldc);
  for(int j=0;j<n;++j) for(int i=0;i<m;++i) c[i+j*ldc]=result(i,j);
}
void gemv(char transpose, int m, int n, double alpha, const double *a,
          int lda, const double *x, int inc, double beta, double *y) {
  Matrix mat=matrix(a,m,n,lda);
  if(transpose=='T') mat.transposeInPlace();
  const int nx=mat.cols(),ny=mat.rows();
  const int x0=inc<0?(nx-1)*std::abs(inc):0;
  const int y0=inc<0?(ny-1)*std::abs(inc):0;
  Vector input(nx),output(ny);
  for(int i=0;i<nx;++i) input(i)=x[x0+i*inc];
  for(int i=0;i<ny;++i) output(i)=y[y0+i*inc];
  output=alpha*(mat*input)+beta*output;
  for(int i=0;i<ny;++i) y[y0+i*inc]=output(i);
}
void triangular(char side, char uplo, char transpose, char diagonal,
                int m, int n, double alpha, const double *a, int lda,
                double *b, int ldb) {
  const int order=side=='L'?m:n;
  Matrix mat=matrix(a,order,order,lda),input=matrix(b,m,n,ldb);
  for(int j=0;j<order;++j) for(int i=0;i<order;++i) {
    if((uplo=='U' && i>j)||(uplo=='L' && i<j)) mat(i,j)=0.;
    if(diagonal=='U' && i==j) mat(i,j)=1.;
  }
  if(transpose=='T') mat.transposeInPlace();
  Matrix result;
  if(side=='L') result=alpha*(mat*input);
  else result=alpha*(input*mat);
  for(int j=0;j<n;++j) for(int i=0;i<m;++i) b[i+j*ldb]=result(i,j);
}
}
