"""Adapt the pinned fork's real GEMM/GEMV interfaces to the RTOS service.

Dispatch after BLAS argument validation, before allocations, GEMM-to-GEMV
forwarding, or GEMV beta scaling. The service owns packing and completion.
"""
from pathlib import Path
import hashlib

def adapt(source):
    records = {}
    for op in ('gemm', 'gemv'):
        path = Path(source) / 'interface' / (op + '.c')
        original = path.read_text()
        text = original.replace('#include "gemmini/gemmini.h"', '''
#ifdef DOUBLE
#define ILLIXR_GEMMINI_DOUBLE 1
#else
#define ILLIXR_GEMMINI_DOUBLE 0
#endif
extern void illixr_gemmini_gemm(int, int, int, long, long, long,
    double, const void *, long, const void *, long, double, void *, long);
extern void illixr_gemmini_gemv(int, int, long, long, double,
    const void *, long, const void *, long, double, void *, long);
''')
        # Delete the original accelerator branch (including its malloc/free),
        # retaining the ordinary implementation under !GEMMINI_BACKEND.
        start = text.index('#if defined(GEMMINI_BACKEND)', text.index('void NAME'))
        lines = text[start:].splitlines(keepends=True)
        depth, split, end, offset = 0, None, None, 0
        for line in lines:
            directive = line.strip()
            if directive.startswith(('#if ', '#ifdef ', '#ifndef ')): depth += 1
            elif directive == '#else' and depth == 1: split = offset + len(line)
            elif directive.startswith('#endif'):
                depth -= 1
                if depth == 0:
                    end = offset
                    stop = offset + len(line)
                    break
            offset += len(line)
        if split is None or end is None: raise ValueError('Unknown Gemmini branch shape')
        text = text[:start] + text[start + split:start + end] + text[start + stop:]
        if op == 'gemm':
            marker = '  if ((args.m == 0) || (args.n == 0)) return;'
            dispatch = '''
#if defined(GEMMINI_BACKEND)
  illixr_gemmini_gemm(ILLIXR_GEMMINI_DOUBLE, transa, transb,
      args.m, args.n, args.k, *(FLOAT *)args.alpha, args.a, args.lda,
      args.b, args.ldb, *(FLOAT *)args.beta, args.c, args.ldc);
  return;
#endif
'''
        else:
            marker = '  if ((m==0) || (n==0)) return;'
            dispatch = '''
#if defined(GEMMINI_BACKEND)
  illixr_gemmini_gemv(ILLIXR_GEMMINI_DOUBLE, trans, m, n, alpha,
      a, lda, x, incx, beta, y, incy);
  return;
#endif
'''
        if text.count(marker) != 1: raise ValueError('Unknown BLAS validation/dispatch shape')
        text = text.replace(marker, marker + dispatch)
        path.write_text(text)
        records[op] = {'original_sha256': hashlib.sha256(original.encode()).hexdigest(),
                       'adapted_sha256': hashlib.sha256(text.encode()).hexdigest()}
    return records
