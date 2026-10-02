#ifndef GEMMINIFP_PARAMS_H
#define GEMMINIFP_PARAMS_H

#include <stdint.h>
#include <limits.h>

#define FIM 4
#define FP_BANK_ROWS 512
#define FP_ACC_ROWS 512
//#define MAX_BYTES 64
#define FP_MAX_BLOCK_LEN (MAX_BYTES/(FIM*4))
#define FP_MAX_BLOCK_LEN_ACC (MAX_BYTES/(FIM*4))
/*
typedef float elem_t;
static const elem_t elem_t_max = 3.4028235E38;
static const elem_t elem_t_min = -3.4028235E38;
typedef float acc_t;
typedef double full_t;
*/
#define ELEM_T_IS_FLOAT
#define ELEM_T_EXP_BITS 8
#define ELEM_T_SIG_BITS 24
#define ACC_T_EXP_BITS 8
#define ACC_T_SIG_BITS 24
typedef uint32_t elem_t_bits;
typedef uint32_t acc_t_bits;

#define HAS_MVIN_SCALE
//typedef float scale_t;
//typedef uint32_t scale_t_bits;

//typedef int32_t scale_acc_t;
//typedef uint32_t scale_acc_t_bits;

//typedef float acc_scale_t;
//typedef uint32_t acc_scale_t_bits;

#define fp_row_align(blocks) __attribute__((aligned(blocks*FIM*sizeof(elem_t))))
#define fp_row_align_acc(blocks) __attribute__((aligned(blocks*FIM*sizeof(acc_t))))

//#define ACC_READ_SMALL_WIDTH

#endif // GEMMINI_PARAMS_H
