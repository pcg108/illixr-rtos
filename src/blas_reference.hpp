#pragma once

// Test oracle only; plain-pointer ABI prevents sharing Eigen definitions with
// application translation units compiled with EIGEN_USE_BLAS.
namespace ILLIXR::blas_reference {
void gemm(char ta, char tb, int m, int n, int k, double alpha,
          const double *a, int lda, const double *b, int ldb,
          double beta, double *c, int ldc);
void gemv(char transpose, int m, int n, double alpha, const double *a,
          int lda, const double *x, int inc, double beta, double *y);
void triangular(char side, char uplo, char transpose, char diagonal,
                int m, int n, double alpha, const double *a, int lda,
                double *b, int ldb);
}
