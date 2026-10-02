#include "ritnet.h"
#include "include/gemmini.h"
#include "include/gemmini_nn.h"
#include "ritnet_params.h"
#include "ritnet_weights.h"
#include "ritnet_helpers.h"
#include "images.h"
#ifdef RITNET_HOST_REFERENCE
#define RITNET_EXECUTION_TYPE CPU
#else
#define RITNET_EXECUTION_TYPE WS
_Static_assert(XCUSTOM_ACC == 2 && DIM == 16, "INT8 opcode/array mismatch");
_Static_assert(sizeof(elem_t)==1 && sizeof(acc_t)==4, "INT8 type mismatch");
#endif

#ifdef RITNET_DIAGNOSTICS
#include "diagnostics.h"
void rd_drain(void) { gemmini_fence(); }
int rd_guards(void) { return ritnet_workspace_check(); }
uint64_t rd_immutable(void) {
 uint64_t hash=UINT64_C(14695981039346656037);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv1_b,1,sizeof(down_block1_conv1_b),sizeof(down_block1_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv1_w,1,sizeof(down_block1_conv1_w),sizeof(down_block1_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv21_b,1,sizeof(down_block1_conv21_b),sizeof(down_block1_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv21_w,1,sizeof(down_block1_conv21_w),sizeof(down_block1_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv22_b,1,sizeof(down_block1_conv22_b),sizeof(down_block1_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv22_w,1,sizeof(down_block1_conv22_w),sizeof(down_block1_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv31_b,1,sizeof(down_block1_conv31_b),sizeof(down_block1_conv31_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv31_w,1,sizeof(down_block1_conv31_w),sizeof(down_block1_conv31_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv32_b,1,sizeof(down_block1_conv32_b),sizeof(down_block1_conv32_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block1_conv32_w,1,sizeof(down_block1_conv32_w),sizeof(down_block1_conv32_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv1_b,1,sizeof(down_block2_conv1_b),sizeof(down_block2_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv1_w,1,sizeof(down_block2_conv1_w),sizeof(down_block2_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv21_b,1,sizeof(down_block2_conv21_b),sizeof(down_block2_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv21_w,1,sizeof(down_block2_conv21_w),sizeof(down_block2_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv22_b,1,sizeof(down_block2_conv22_b),sizeof(down_block2_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv22_w,1,sizeof(down_block2_conv22_w),sizeof(down_block2_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv31_b,1,sizeof(down_block2_conv31_b),sizeof(down_block2_conv31_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv31_w,1,sizeof(down_block2_conv31_w),sizeof(down_block2_conv31_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv32_b,1,sizeof(down_block2_conv32_b),sizeof(down_block2_conv32_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block2_conv32_w,1,sizeof(down_block2_conv32_w),sizeof(down_block2_conv32_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv1_b,1,sizeof(down_block3_conv1_b),sizeof(down_block3_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv1_w,1,sizeof(down_block3_conv1_w),sizeof(down_block3_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv21_b,1,sizeof(down_block3_conv21_b),sizeof(down_block3_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv21_w,1,sizeof(down_block3_conv21_w),sizeof(down_block3_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv22_b,1,sizeof(down_block3_conv22_b),sizeof(down_block3_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv22_w,1,sizeof(down_block3_conv22_w),sizeof(down_block3_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv31_b,1,sizeof(down_block3_conv31_b),sizeof(down_block3_conv31_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv31_w,1,sizeof(down_block3_conv31_w),sizeof(down_block3_conv31_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv32_b,1,sizeof(down_block3_conv32_b),sizeof(down_block3_conv32_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block3_conv32_w,1,sizeof(down_block3_conv32_w),sizeof(down_block3_conv32_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv1_b,1,sizeof(down_block4_conv1_b),sizeof(down_block4_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv1_w,1,sizeof(down_block4_conv1_w),sizeof(down_block4_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv21_b,1,sizeof(down_block4_conv21_b),sizeof(down_block4_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv21_w,1,sizeof(down_block4_conv21_w),sizeof(down_block4_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv22_b,1,sizeof(down_block4_conv22_b),sizeof(down_block4_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv22_w,1,sizeof(down_block4_conv22_w),sizeof(down_block4_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv31_b,1,sizeof(down_block4_conv31_b),sizeof(down_block4_conv31_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv31_w,1,sizeof(down_block4_conv31_w),sizeof(down_block4_conv31_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv32_b,1,sizeof(down_block4_conv32_b),sizeof(down_block4_conv32_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block4_conv32_w,1,sizeof(down_block4_conv32_w),sizeof(down_block4_conv32_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv1_b,1,sizeof(down_block5_conv1_b),sizeof(down_block5_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv1_w,1,sizeof(down_block5_conv1_w),sizeof(down_block5_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv21_b,1,sizeof(down_block5_conv21_b),sizeof(down_block5_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv21_w,1,sizeof(down_block5_conv21_w),sizeof(down_block5_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv22_b,1,sizeof(down_block5_conv22_b),sizeof(down_block5_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv22_w,1,sizeof(down_block5_conv22_w),sizeof(down_block5_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv31_b,1,sizeof(down_block5_conv31_b),sizeof(down_block5_conv31_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv31_w,1,sizeof(down_block5_conv31_w),sizeof(down_block5_conv31_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv32_b,1,sizeof(down_block5_conv32_b),sizeof(down_block5_conv32_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){down_block5_conv32_w,1,sizeof(down_block5_conv32_w),sizeof(down_block5_conv32_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){identity_32,1,sizeof(identity_32),sizeof(identity_32),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){identity_kernel,1,sizeof(identity_kernel),sizeof(identity_kernel),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){out_conv1_b,1,sizeof(out_conv1_b),sizeof(out_conv1_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){out_conv1_w,1,sizeof(out_conv1_w),sizeof(out_conv1_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv11_b,1,sizeof(up_block1_conv11_b),sizeof(up_block1_conv11_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv11_w,1,sizeof(up_block1_conv11_w),sizeof(up_block1_conv11_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv12_b,1,sizeof(up_block1_conv12_b),sizeof(up_block1_conv12_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv12_w,1,sizeof(up_block1_conv12_w),sizeof(up_block1_conv12_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv21_b,1,sizeof(up_block1_conv21_b),sizeof(up_block1_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv21_w,1,sizeof(up_block1_conv21_w),sizeof(up_block1_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv22_b,1,sizeof(up_block1_conv22_b),sizeof(up_block1_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block1_conv22_w,1,sizeof(up_block1_conv22_w),sizeof(up_block1_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv11_b,1,sizeof(up_block2_conv11_b),sizeof(up_block2_conv11_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv11_w,1,sizeof(up_block2_conv11_w),sizeof(up_block2_conv11_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv12_b,1,sizeof(up_block2_conv12_b),sizeof(up_block2_conv12_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv12_w,1,sizeof(up_block2_conv12_w),sizeof(up_block2_conv12_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv21_b,1,sizeof(up_block2_conv21_b),sizeof(up_block2_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv21_w,1,sizeof(up_block2_conv21_w),sizeof(up_block2_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv22_b,1,sizeof(up_block2_conv22_b),sizeof(up_block2_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block2_conv22_w,1,sizeof(up_block2_conv22_w),sizeof(up_block2_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv11_b,1,sizeof(up_block3_conv11_b),sizeof(up_block3_conv11_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv11_w,1,sizeof(up_block3_conv11_w),sizeof(up_block3_conv11_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv12_b,1,sizeof(up_block3_conv12_b),sizeof(up_block3_conv12_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv12_w,1,sizeof(up_block3_conv12_w),sizeof(up_block3_conv12_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv21_b,1,sizeof(up_block3_conv21_b),sizeof(up_block3_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv21_w,1,sizeof(up_block3_conv21_w),sizeof(up_block3_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv22_b,1,sizeof(up_block3_conv22_b),sizeof(up_block3_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block3_conv22_w,1,sizeof(up_block3_conv22_w),sizeof(up_block3_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv11_b,1,sizeof(up_block4_conv11_b),sizeof(up_block4_conv11_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv11_w,1,sizeof(up_block4_conv11_w),sizeof(up_block4_conv11_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv12_b,1,sizeof(up_block4_conv12_b),sizeof(up_block4_conv12_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv12_w,1,sizeof(up_block4_conv12_w),sizeof(up_block4_conv12_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv21_b,1,sizeof(up_block4_conv21_b),sizeof(up_block4_conv21_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv21_w,1,sizeof(up_block4_conv21_w),sizeof(up_block4_conv21_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv22_b,1,sizeof(up_block4_conv22_b),sizeof(up_block4_conv22_b),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){up_block4_conv22_w,1,sizeof(up_block4_conv22_w),sizeof(up_block4_conv22_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){upsample_dw_w,1,sizeof(upsample_dw_w),sizeof(upsample_dw_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){upsize_w,1,sizeof(upsize_w),sizeof(upsize_w),1}))*UINT64_C(1099511628211);
 hash=(hash ^ rd_hash((struct rd_view){z_bias,1,sizeof(z_bias),sizeof(z_bias),1}))*UINT64_C(1099511628211);
 return hash;
}
#endif
__attribute__((weak)) void ritnet_stage(unsigned stage) { (void)stage; }
const int8_t *ritnet_sample(void) { return &images[0][0][0][0]; }
const int8_t *ritnet_output(void) { return &out[0][0][0][0]; }
int ritnet_infer(const int8_t *input, struct ritnet_result *result) {
    if (!input || !result) return -1;
    const elem_t *images = input;
    unsigned stage = 0;
#ifdef RITNET_DIAGNOSTICS
    if(rd_inference_begin(input,160*240)) return -3;
#endif
    ritnet_workspace_reset();
#ifndef RITNET_HOST_REFERENCE
    gemmini_flush(0);
#endif
    enum tiled_matmul_type_t tiled_matmul_type = RITNET_EXECUTION_TYPE;

    uint64_t start, end;
    uint64_t im2col_cycles = 0, matmul_cycles = 0, conv_cycles = 0, pool_cycles = 0, conv_dw_cycles = 0, res_add_cycles = 0, other_cycles = 0;

// down block 1

    // copying image into column 0 of concat1, applying the same quantization factor as conv1 so that they can be concatenated
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {1,"down_block1_concat1_temp","tiled_matmul_auto",
      {{(const void*)(images),38400,1,1,1},{(const void*)(identity_kernel),1,sizeof(identity_kernel)/1,sizeof(identity_kernel)/1,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block1_concat1_temp),38400,1,65,1},
      {(double)(float)(38400),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0),(double)(float)(65),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(db1_conv1_x_scale/db1_conv1_y_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(38400, 1, 1, 
        images, identity_kernel, NULL, down_block1_concat1_temp,
        1, 1, 0, 65,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, db1_conv1_x_scale/db1_conv1_y_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();


    // conv 1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {2,"down_block1_concat1_temp+1","tiled_conv_stride_auto",
      {{(const void*)(images),(down_block1_conv1_params.batch_size)*(down_block1_conv1_params.in_row_dim)*(down_block1_conv1_params.in_col_dim),down_block1_conv1_params.in_channels,1,1},{(const void*)(down_block1_conv1_w),1,sizeof(down_block1_conv1_w)/1,sizeof(down_block1_conv1_w)/1,1},{(const void*)(down_block1_conv1_b),1,sizeof(down_block1_conv1_b)/4,sizeof(down_block1_conv1_b)/4,4}},{(const void*)(down_block1_concat1_temp+1),(down_block1_conv1_params.batch_size)*(((down_block1_conv1_params.pool_stride)==0?(down_block1_conv1_params.out_row_dim):((down_block1_conv1_params.out_row_dim)+2*(down_block1_conv1_params.pool_padding)-(down_block1_conv1_params.pool_size))/(down_block1_conv1_params.pool_stride)+1))*(((down_block1_conv1_params.pool_stride)==0?(down_block1_conv1_params.out_col_dim):((down_block1_conv1_params.out_col_dim)+2*(down_block1_conv1_params.pool_padding)-(down_block1_conv1_params.pool_size))/(down_block1_conv1_params.pool_stride)+1)),down_block1_conv1_params.out_channels,65,1},
      {(double)(float)(down_block1_conv1_params.batch_size),(double)(float)(down_block1_conv1_params.in_row_dim),(double)(float)(down_block1_conv1_params.in_col_dim),(double)(float)(down_block1_conv1_params.in_channels),(double)(float)(down_block1_conv1_params.out_channels),(double)(float)(down_block1_conv1_params.out_row_dim),(double)(float)(down_block1_conv1_params.out_col_dim),(double)(float)(down_block1_conv1_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block1_conv1_params.padding),(double)(float)(down_block1_conv1_params.kernel_size),(double)(float)(1),(double)(float)(down_block1_conv1_params.out_channels),(double)(float)(65),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block1_conv1_params.output_scale),(double)(float)(down_block1_conv1_params.pool_size),(double)(float)(down_block1_conv1_params.pool_stride),(double)(float)(down_block1_conv1_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block1_conv1_params.batch_size, 
        /* in_row_dim */        down_block1_conv1_params.in_row_dim, 
        /* in_col_dim */        down_block1_conv1_params.in_col_dim, 
        /* in_channels */       down_block1_conv1_params.in_channels,
        /* out_channels */      down_block1_conv1_params.out_channels, 
        /* out_row_dim */       down_block1_conv1_params.out_row_dim, 
        /* out_col_dim */       down_block1_conv1_params.out_col_dim, 
        /* stride */            down_block1_conv1_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block1_conv1_params.padding, 
        /* kernel_dim */        down_block1_conv1_params.kernel_size,
        /* in_stride */         1, 
        /* weight_stride */     down_block1_conv1_params.out_channels, 
        /* out_stride */        65,
        false, false, false, false, false,
        
        /* input */             images, 
        /* weights */           down_block1_conv1_w, 
        /* bias */              down_block1_conv1_b,
        /* output */            down_block1_concat1_temp+1,

        /* activation */        RELU, 
        /* scale */             down_block1_conv1_params.output_scale, 
        /* pool_size */         down_block1_conv1_params.pool_size, 
        /* pool_stride */       down_block1_conv1_params.pool_stride, 
        /* pool_padding */      down_block1_conv1_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    // printf("db_1_conv1 cycles: %llu \n", end - start);


    // conv 21 uses concat1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {3,"down_block1_conv21_out","tiled_matmul_auto",
      {{(const void*)(down_block1_concat1_temp),down_block1_conv21_params.I,down_block1_conv21_params.K,65,1},{(const void*)(down_block1_conv21_w),1,sizeof(down_block1_conv21_w)/1,sizeof(down_block1_conv21_w)/1,1},{(const void*)(down_block1_conv21_b),1,sizeof(down_block1_conv21_b)/4,sizeof(down_block1_conv21_b)/4,4}},{(const void*)(down_block1_conv21_out),down_block1_conv21_params.I,down_block1_conv21_params.J,down_block1_conv21_params.J,1},
      {(double)(float)(down_block1_conv21_params.I),(double)(float)(down_block1_conv21_params.J),(double)(float)(down_block1_conv21_params.K),(double)(float)(65),(double)(float)(down_block1_conv21_params.J),(double)(float)(down_block1_conv21_params.J),(double)(float)(down_block1_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block1_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block1_conv21_params.I, down_block1_conv21_params.J, down_block1_conv21_params.K,
        down_block1_concat1_temp, down_block1_conv21_w, down_block1_conv21_b, down_block1_conv21_out,
        65, down_block1_conv21_params.J, down_block1_conv21_params.J, down_block1_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block1_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_1_conv_21 (matmul) cycles: %llu \n", end - start); 

    // conv 22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {4,"down_block1_concat2_temp+33","tiled_conv_stride_auto",
      {{(const void*)(down_block1_conv21_out),(down_block1_conv22_params.batch_size)*(down_block1_conv22_params.in_row_dim)*(down_block1_conv22_params.in_col_dim),down_block1_conv22_params.in_channels,down_block1_conv22_params.in_channels,1},{(const void*)(down_block1_conv22_w),1,sizeof(down_block1_conv22_w)/1,sizeof(down_block1_conv22_w)/1,1},{(const void*)(down_block1_conv22_b),1,sizeof(down_block1_conv22_b)/4,sizeof(down_block1_conv22_b)/4,4}},{(const void*)(down_block1_concat2_temp+33),(down_block1_conv22_params.batch_size)*(((down_block1_conv22_params.pool_stride)==0?(down_block1_conv22_params.out_row_dim):((down_block1_conv22_params.out_row_dim)+2*(down_block1_conv22_params.pool_padding)-(down_block1_conv22_params.pool_size))/(down_block1_conv22_params.pool_stride)+1))*(((down_block1_conv22_params.pool_stride)==0?(down_block1_conv22_params.out_col_dim):((down_block1_conv22_params.out_col_dim)+2*(down_block1_conv22_params.pool_padding)-(down_block1_conv22_params.pool_size))/(down_block1_conv22_params.pool_stride)+1)),down_block1_conv22_params.out_channels,65,1},
      {(double)(float)(down_block1_conv22_params.batch_size),(double)(float)(down_block1_conv22_params.in_row_dim),(double)(float)(down_block1_conv22_params.in_col_dim),(double)(float)(down_block1_conv22_params.in_channels),(double)(float)(down_block1_conv22_params.out_channels),(double)(float)(down_block1_conv22_params.out_row_dim),(double)(float)(down_block1_conv22_params.out_col_dim),(double)(float)(down_block1_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block1_conv22_params.padding),(double)(float)(down_block1_conv22_params.kernel_size),(double)(float)(down_block1_conv22_params.in_channels),(double)(float)(down_block1_conv22_params.out_channels),(double)(float)(65),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block1_conv22_params.output_scale),(double)(float)(down_block1_conv22_params.pool_size),(double)(float)(down_block1_conv22_params.pool_stride),(double)(float)(down_block1_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block1_conv22_params.batch_size, 
        /* in_row_dim */        down_block1_conv22_params.in_row_dim, 
        /* in_col_dim */        down_block1_conv22_params.in_col_dim, 
        /* in_channels */       down_block1_conv22_params.in_channels,
        /* out_channels */      down_block1_conv22_params.out_channels, 
        /* out_row_dim */       down_block1_conv22_params.out_row_dim, 
        /* out_col_dim */       down_block1_conv22_params.out_col_dim,
        /* stride */            down_block1_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block1_conv22_params.padding, 
        /* kernel_dim */        down_block1_conv22_params.kernel_size,
        /* in_stride */         down_block1_conv22_params.in_channels, 
        /* weight_stride */     down_block1_conv22_params.out_channels, 
        /* out_stride */        65,
        false, false, false, false, false,
        
        /* input */             down_block1_conv21_out, 
        /* weights */           down_block1_conv22_w, 
        /* bias */              down_block1_conv22_b, 
        /* output */            down_block1_concat2_temp+33,

        /* activation */        RELU, 
        /* scale */             down_block1_conv22_params.output_scale, 
        /* pool_size */         down_block1_conv22_params.pool_size, 
        /* pool_stride */       down_block1_conv22_params.pool_stride, 
        /* pool_padding */      down_block1_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_1_conv_22 cycles: %llu \n", end - start);


    // concat 2
    ritnet_stage(++stage); start = read_cycles();

    // add concat 1 (uses columns 0->32) to concat 2 (uses 33->65)
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {5,"down_block1_concat2_temp","tiled_resadd_auto",
      {{(const void*)(down_block1_concat1_temp),38400,65,65,1},{(const void*)(down_block1_concat2_temp),38400,65,65,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block1_concat2_temp),38400,65,65,1},
      {(double)(float)(38400),(double)(float)(65),(double)(float)(db1_conv1_y_scale/db1_conv22_y_scale),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(false)},6};
      rd_begin(&rd_op);
#endif
tiled_resadd_auto(38400, 65,
        /* A_scale*/ db1_conv1_y_scale/db1_conv22_y_scale, // dequantize from conv1 and requantize to conv22
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        down_block1_concat1_temp, 
        down_block1_concat2_temp,
        down_block1_concat2_temp,
        false,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // write the input quantized with conv22 scale into concat2
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {6,"down_block1_concat2_temp","tiled_matmul_auto",
      {{(const void*)(images),38400,1,1,1},{(const void*)(identity_kernel),1,sizeof(identity_kernel)/1,sizeof(identity_kernel)/1,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block1_concat2_temp),38400,1,65,1},
      {(double)(float)(38400),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(65),(double)(float)(1.0/db1_conv22_y_scale),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(38400, 1, 1, 
        images, identity_kernel, NULL, down_block1_concat2_temp,
        1, 1, 1, 65,
        /* A_scale */ 1.0/db1_conv22_y_scale, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true, // quantize to conv22
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_1_concat2 cycles: %llu \n", end - start);

    elem_t (*down_block1_concat2_out)[160][240][65] = (elem_t (*)[160][240][65]) down_block1_concat2_temp;


    // down block 1, conv_31
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {7,"down_block1_conv31_out","tiled_matmul_auto",
      {{(const void*)(down_block1_concat2_out),down_block1_conv31_params.I,down_block1_conv31_params.K,down_block1_conv31_params.K,1},{(const void*)(down_block1_conv31_w),1,sizeof(down_block1_conv31_w)/1,sizeof(down_block1_conv31_w)/1,1},{(const void*)(down_block1_conv31_b),1,sizeof(down_block1_conv31_b)/4,sizeof(down_block1_conv31_b)/4,4}},{(const void*)(down_block1_conv31_out),down_block1_conv31_params.I,down_block1_conv31_params.J,down_block1_conv31_params.J,1},
      {(double)(float)(down_block1_conv31_params.I),(double)(float)(down_block1_conv31_params.J),(double)(float)(down_block1_conv31_params.K),(double)(float)(down_block1_conv31_params.K),(double)(float)(down_block1_conv31_params.J),(double)(float)(down_block1_conv31_params.J),(double)(float)(down_block1_conv31_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block1_conv31_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block1_conv31_params.I, down_block1_conv31_params.J, down_block1_conv31_params.K,
        down_block1_concat2_out, down_block1_conv31_w, down_block1_conv31_b, down_block1_conv31_out,
        down_block1_conv31_params.K, down_block1_conv31_params.J, down_block1_conv31_params.J, down_block1_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block1_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("db_1_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 1, conv_32 relu 
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {8,"down_block1_conv32_out_relu","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block1_conv31_out),(down_block1_conv32_params.batch_size)*(down_block1_conv32_params.in_row_dim)*(down_block1_conv32_params.in_col_dim),down_block1_conv32_params.in_channels,down_block1_conv32_params.in_channels,1},{(const void*)(down_block1_conv32_w),1,sizeof(down_block1_conv32_w)/1,sizeof(down_block1_conv32_w)/1,1},{(const void*)(down_block1_conv32_b),1,sizeof(down_block1_conv32_b)/4,sizeof(down_block1_conv32_b)/4,4}},{(const void*)((elem_t*) down_block1_conv32_out_relu),(down_block1_conv32_params.batch_size)*(((1)==0?(160):((160)+2*(down_block1_conv32_params.pool_padding)-(1))/(1)+1))*(((1)==0?(240):((240)+2*(down_block1_conv32_params.pool_padding)-(1))/(1)+1)),down_block1_conv32_params.out_channels,down_block1_conv32_params.out_channels,1},
      {(double)(float)(down_block1_conv32_params.batch_size),(double)(float)(down_block1_conv32_params.in_row_dim),(double)(float)(down_block1_conv32_params.in_col_dim),(double)(float)(down_block1_conv32_params.in_channels),(double)(float)(down_block1_conv32_params.out_channels),(double)(float)(160),(double)(float)(240),(double)(float)(down_block1_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block1_conv32_params.padding),(double)(float)(down_block1_conv32_params.kernel_size),(double)(float)(down_block1_conv32_params.in_channels),(double)(float)(down_block1_conv32_params.out_channels),(double)(float)(down_block1_conv32_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block1_conv32_params.output_scale),(double)(float)(1),(double)(float)(1),(double)(float)(down_block1_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block1_conv32_params.batch_size, 
        /* in_row_dim */    down_block1_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block1_conv32_params.in_col_dim,
        /* in_channels */   down_block1_conv32_params.in_channels,
        /* out_channels */  down_block1_conv32_params.out_channels, 
        /* out_row_dim */   160, 
        /* out_col_dim */   240,
        /* stride */        down_block1_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block1_conv32_params.padding, 
        /* kernel_dim */    down_block1_conv32_params.kernel_size,
        /* in_stride */     down_block1_conv32_params.in_channels, 
        /* weight_stride */ down_block1_conv32_params.out_channels, 
        /* out_stride */    down_block1_conv32_params.out_channels,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block1_conv31_out, 
        /* weights */       (elem_t*)   down_block1_conv32_w, 
        /* bias */          (acc_t*)    down_block1_conv32_b, 
        /* output */        (elem_t*)   down_block1_conv32_out_relu,

        /* activation */    RELU, 
        /* scale */         down_block1_conv32_params.output_scale,  
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block1_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_1_conv_32_relu cycles: %llu \n", end - start);


    // average pooling
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {9,"down_block1_conv32_avg_pool","tiled_conv_dw_auto",
      {{(const void*)(down_block1_conv32_out_relu),(down_block1_conv32_params.batch_size)*(down_block1_conv32_params.in_row_dim)*(down_block1_conv32_params.in_col_dim),down_block1_conv32_params.in_channels,down_block1_conv32_params.in_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block1_conv32_avg_pool),(down_block1_conv32_params.batch_size)*(((1)==0?(down_block1_conv32_params.out_row_dim):((down_block1_conv32_params.out_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(down_block1_conv32_params.out_col_dim):((down_block1_conv32_params.out_col_dim)+2*(0)-(1))/(1)+1)),down_block1_conv32_params.in_channels,down_block1_conv32_params.in_channels,1},
      {(double)(float)(down_block1_conv32_params.batch_size),(double)(float)(down_block1_conv32_params.in_row_dim),(double)(float)(down_block1_conv32_params.in_col_dim),(double)(float)(down_block1_conv32_params.in_channels),(double)(float)(down_block1_conv32_params.out_row_dim),(double)(float)(down_block1_conv32_params.out_col_dim),(double)(float)(2),(double)(float)(0),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(db1_conv32_y_scale*0.25/db2_conv1_x_scale),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        down_block1_conv32_params.batch_size, 
        /* in_row_dim */        down_block1_conv32_params.in_row_dim, 
        /* in_col_dim */        down_block1_conv32_params.in_col_dim, 
        /* channels */          down_block1_conv32_params.in_channels, 
        /* out_row_dim */       down_block1_conv32_params.out_row_dim, 
        /* out_col_dim */       down_block1_conv32_params.out_col_dim, 
        /* stride */            2, 
        /* padding */           0, 
        /* kernel_dim */        2, 
        /* input */             down_block1_conv32_out_relu, 
        /* weights */           upsample_dw_w, 
        /* bias */              NULL, 
        /* output */            down_block1_conv32_avg_pool, 
        /* activation */ NO_ACTIVATION, /* scale */ db1_conv32_y_scale*0.25/db2_conv1_x_scale, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

 
    // average pooling again to write into the concat 1 of the next block (for some reason I can't just dequantize the first one)
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {10,"down_block1_conv32_avg_pool_2","tiled_conv_dw_auto",
      {{(const void*)(down_block1_conv32_out_relu),(down_block1_conv32_params.batch_size)*(down_block1_conv32_params.in_row_dim)*(down_block1_conv32_params.in_col_dim),down_block1_conv32_params.in_channels,down_block1_conv32_params.in_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block1_conv32_avg_pool_2),(down_block1_conv32_params.batch_size)*(((1)==0?(down_block1_conv32_params.out_row_dim):((down_block1_conv32_params.out_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(down_block1_conv32_params.out_col_dim):((down_block1_conv32_params.out_col_dim)+2*(0)-(1))/(1)+1)),down_block1_conv32_params.in_channels,down_block1_conv32_params.in_channels,1},
      {(double)(float)(down_block1_conv32_params.batch_size),(double)(float)(down_block1_conv32_params.in_row_dim),(double)(float)(down_block1_conv32_params.in_col_dim),(double)(float)(down_block1_conv32_params.in_channels),(double)(float)(down_block1_conv32_params.out_row_dim),(double)(float)(down_block1_conv32_params.out_col_dim),(double)(float)(2),(double)(float)(0),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(db1_conv32_y_scale*0.25/db2_conv21_x_scale),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        down_block1_conv32_params.batch_size, 
        /* in_row_dim */        down_block1_conv32_params.in_row_dim, 
        /* in_col_dim */        down_block1_conv32_params.in_col_dim, 
        /* channels */          down_block1_conv32_params.in_channels, 
        /* out_row_dim */       down_block1_conv32_params.out_row_dim, 
        /* out_col_dim */       down_block1_conv32_params.out_col_dim, 
        /* stride */            2, 
        /* padding */           0, 
        /* kernel_dim */        2, 
        /* input */             down_block1_conv32_out_relu, 
        /* weights */           upsample_dw_w, 
        /* bias */              NULL, 
        /* output */            down_block1_conv32_avg_pool_2, 
        /* activation */ NO_ACTIVATION, /* scale */ db1_conv32_y_scale*0.25/db2_conv21_x_scale, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    
    
    end = read_cycles();
    pool_cycles += end - start;
    // printf("db_1_avg_pool cycles: %llu \n", end - start);



// down block 2


    // copying the output of conv32 into the first 32 channels of concat1
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {11,"down_block2_concat1_temp","tiled_matmul_auto",
      {{(const void*)(down_block1_conv32_avg_pool_2),down_block2_conv1_params.I,down_block2_conv1_params.K,down_block2_conv1_params.K,1},{(const void*)(identity_32),1,sizeof(identity_32)/1,sizeof(identity_32)/1,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block2_concat1_temp),down_block2_conv1_params.I,down_block2_conv1_params.J,96,1},
      {(double)(float)(down_block2_conv1_params.I),(double)(float)(down_block2_conv1_params.J),(double)(float)(down_block2_conv1_params.K),(double)(float)(down_block2_conv1_params.K),(double)(float)(down_block2_conv1_params.J),(double)(float)(down_block2_conv1_params.J),(double)(float)(96),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(1.0),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block2_conv1_params.I, down_block2_conv1_params.J, down_block2_conv1_params.K,
        down_block1_conv32_avg_pool_2, identity_32, NULL, down_block2_concat1_temp,
        down_block2_conv1_params.K, down_block2_conv1_params.J, down_block2_conv1_params.J, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
        NO_ACTIVATION, 1.0, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */



    // down block 2, conv_1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {12,"down_block2_concat1_temp+32","tiled_conv_stride_auto",
      {{(const void*)(down_block1_conv32_avg_pool),(down_block2_conv1_params.batch_size)*(down_block2_conv1_params.in_row_dim)*(down_block2_conv1_params.in_col_dim),down_block2_conv1_params.in_channels,down_block2_conv1_params.in_channels,1},{(const void*)(down_block2_conv1_w),1,sizeof(down_block2_conv1_w)/1,sizeof(down_block2_conv1_w)/1,1},{(const void*)(down_block2_conv1_b),1,sizeof(down_block2_conv1_b)/4,sizeof(down_block2_conv1_b)/4,4}},{(const void*)(down_block2_concat1_temp+32),(down_block2_conv1_params.batch_size)*(((down_block2_conv1_params.pool_stride)==0?(down_block2_conv1_params.out_row_dim):((down_block2_conv1_params.out_row_dim)+2*(down_block2_conv1_params.pool_padding)-(down_block2_conv1_params.pool_size))/(down_block2_conv1_params.pool_stride)+1))*(((down_block2_conv1_params.pool_stride)==0?(down_block2_conv1_params.out_col_dim):((down_block2_conv1_params.out_col_dim)+2*(down_block2_conv1_params.pool_padding)-(down_block2_conv1_params.pool_size))/(down_block2_conv1_params.pool_stride)+1)),down_block2_conv1_params.out_channels,96,1},
      {(double)(float)(down_block2_conv1_params.batch_size),(double)(float)(down_block2_conv1_params.in_row_dim),(double)(float)(down_block2_conv1_params.in_col_dim),(double)(float)(down_block2_conv1_params.in_channels),(double)(float)(down_block2_conv1_params.out_channels),(double)(float)(down_block2_conv1_params.out_row_dim),(double)(float)(down_block2_conv1_params.out_col_dim),(double)(float)(down_block2_conv1_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block2_conv1_params.padding),(double)(float)(down_block2_conv1_params.kernel_size),(double)(float)(down_block2_conv1_params.in_channels),(double)(float)(down_block2_conv1_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block2_conv1_params.output_scale),(double)(float)(down_block2_conv1_params.pool_size),(double)(float)(down_block2_conv1_params.pool_stride),(double)(float)(down_block2_conv1_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block2_conv1_params.batch_size, 
        /* in_row_dim */        down_block2_conv1_params.in_row_dim, 
        /* in_col_dim */        down_block2_conv1_params.in_col_dim, 
        /* in_channels */       down_block2_conv1_params.in_channels,
        /* out_channels */      down_block2_conv1_params.out_channels, 
        /* out_row_dim */       down_block2_conv1_params.out_row_dim, 
        /* out_col_dim */       down_block2_conv1_params.out_col_dim,
        /* stride */            down_block2_conv1_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block2_conv1_params.padding, 
        /* kernel_dim */        down_block2_conv1_params.kernel_size,
        /* in_stride */         down_block2_conv1_params.in_channels, 
        /* weight_stride */     down_block2_conv1_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block1_conv32_avg_pool, 
        /* weights */           down_block2_conv1_w, 
        /* bias */              down_block2_conv1_b, 
        /* output */            down_block2_concat1_temp+32,

        /* activation */        RELU, 
        /* scale */             down_block2_conv1_params.output_scale, 
        /* pool_size */         down_block2_conv1_params.pool_size, 
        /* pool_stride */       down_block2_conv1_params.pool_stride, 
        /* pool_padding */      down_block2_conv1_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_2_conv1 cycles: %llu \n", end - start);

    // conv 21
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {13,"down_block2_conv21_out","tiled_matmul_auto",
      {{(const void*)(down_block2_concat1_temp),down_block2_conv21_params.I,down_block2_conv21_params.K,96,1},{(const void*)(down_block2_conv21_w),1,sizeof(down_block2_conv21_w)/1,sizeof(down_block2_conv21_w)/1,1},{(const void*)(down_block2_conv21_b),1,sizeof(down_block2_conv21_b)/4,sizeof(down_block2_conv21_b)/4,4}},{(const void*)(down_block2_conv21_out),down_block2_conv21_params.I,down_block2_conv21_params.J,down_block2_conv21_params.J,1},
      {(double)(float)(down_block2_conv21_params.I),(double)(float)(down_block2_conv21_params.J),(double)(float)(down_block2_conv21_params.K),(double)(float)(96),(double)(float)(down_block2_conv21_params.J),(double)(float)(down_block2_conv21_params.J),(double)(float)(down_block2_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block2_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block2_conv21_params.I, down_block2_conv21_params.J, down_block2_conv21_params.K,
        down_block2_concat1_temp, down_block2_conv21_w, down_block2_conv21_b, down_block2_conv21_out,
        96, down_block2_conv21_params.J, down_block2_conv21_params.J, down_block2_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block2_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_2_conv_21 (matmul) cycles: %llu \n", end - start);

    // conv 22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {14,"down_block2_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(down_block2_conv21_out),(down_block2_conv22_params.batch_size)*(down_block2_conv22_params.in_row_dim)*(down_block2_conv22_params.in_col_dim),down_block2_conv22_params.in_channels,down_block2_conv22_params.in_channels,1},{(const void*)(down_block2_conv22_w),1,sizeof(down_block2_conv22_w)/1,sizeof(down_block2_conv22_w)/1,1},{(const void*)(down_block2_conv22_b),1,sizeof(down_block2_conv22_b)/4,sizeof(down_block2_conv22_b)/4,4}},{(const void*)(down_block2_concat2_temp+64),(down_block2_conv22_params.batch_size)*(((down_block2_conv22_params.pool_stride)==0?(down_block2_conv22_params.out_row_dim):((down_block2_conv22_params.out_row_dim)+2*(down_block2_conv22_params.pool_padding)-(down_block2_conv22_params.pool_size))/(down_block2_conv22_params.pool_stride)+1))*(((down_block2_conv22_params.pool_stride)==0?(down_block2_conv22_params.out_col_dim):((down_block2_conv22_params.out_col_dim)+2*(down_block2_conv22_params.pool_padding)-(down_block2_conv22_params.pool_size))/(down_block2_conv22_params.pool_stride)+1)),down_block2_conv22_params.out_channels,96,1},
      {(double)(float)(down_block2_conv22_params.batch_size),(double)(float)(down_block2_conv22_params.in_row_dim),(double)(float)(down_block2_conv22_params.in_col_dim),(double)(float)(down_block2_conv22_params.in_channels),(double)(float)(down_block2_conv22_params.out_channels),(double)(float)(down_block2_conv22_params.out_row_dim),(double)(float)(down_block2_conv22_params.out_col_dim),(double)(float)(down_block2_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block2_conv22_params.padding),(double)(float)(down_block2_conv22_params.kernel_size),(double)(float)(down_block2_conv22_params.in_channels),(double)(float)(down_block2_conv22_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block2_conv22_params.output_scale),(double)(float)(down_block2_conv22_params.pool_size),(double)(float)(down_block2_conv22_params.pool_stride),(double)(float)(down_block2_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block2_conv22_params.batch_size, 
        /* in_row_dim */        down_block2_conv22_params.in_row_dim, 
        /* in_col_dim */        down_block2_conv22_params.in_col_dim, 
        /* in_channels */       down_block2_conv22_params.in_channels,
        /* out_channels */      down_block2_conv22_params.out_channels, 
        /* out_row_dim */       down_block2_conv22_params.out_row_dim, 
        /* out_col_dim */       down_block2_conv22_params.out_col_dim,
        /* stride */            down_block2_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block2_conv22_params.padding, 
        /* kernel_dim */        down_block2_conv22_params.kernel_size,
        /* in_stride */         down_block2_conv22_params.in_channels, 
        /* weight_stride */     down_block2_conv22_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block2_conv21_out, 
        /* weights */           down_block2_conv22_w, 
        /* bias */              down_block2_conv22_b, 
        /* output */            down_block2_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             down_block2_conv22_params.output_scale, 
        /* pool_size */         down_block2_conv22_params.pool_size, 
        /* pool_stride */       down_block2_conv22_params.pool_stride, 
        /* pool_padding */      down_block2_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_2_conv_22 cycles: %llu \n", end - start);


    // concat 2
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {15,"down_block2_concat2_temp","tiled_resadd_auto",
      {{(const void*)(down_block2_concat1_temp),9600,96,96,1},{(const void*)(down_block2_concat2_temp),9600,96,96,1},{(const void*)(NULL),0,0,0,1}},{(const void*)(down_block2_concat2_temp),9600,96,96,1},
      {(double)(float)(9600),(double)(float)(96),(double)(float)(db2_conv1_y_scale/db2_conv22_y_scale),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(false)},6};
      rd_begin(&rd_op);
#endif
tiled_resadd_auto(9600, 96,
        /* A_scale*/ db2_conv1_y_scale/db2_conv22_y_scale, // dequantize from conv1 and requantize to conv22
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        down_block2_concat1_temp, 
        down_block2_concat2_temp,
        down_block2_concat2_temp,
        false,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();


    // // copying the output of conv32 into the first 32 channels of concat2
    // tiled_matmul_auto(9600, 32, 32,
    //     down_block1_conv32_avg_pool_3, identity_32, NULL, down_block2_concat2_temp, 
    //     32, 32, 32, 96,
    //     MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
    //     NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
    //     false, false,
    //     false, false,
    //     0,
    //     RITNET_EXECUTION_TYPE);
    // end = read_cycles();
    // matmul_cycles += end - start;
    // printf("db_2_concat2 cycles: %llu \n", end - start);

    // elem_t (*layer_test)[80][120][96] = (elem_t (*)[80][120][96]) down_block2_concat2_temp;    
    // for (int i = 0; i < 80; i++) {
    //     for (int j = 0; j < 120; j++) { 
    //         for (int k = 0; k < 96; k++) { 
    //             if (layer_test[0][i][j][k] != test[0][i][j][k]) {
    //                 printf("mismatch at: i=%d, j=%d, k=%d: %" PRId8 " vs %" PRId8 "\n", i, j, k, layer_test[0][i][j][k], test[0][i][j][k]); 
    //             }
    //         }  
    //     }  
    // } 
    // printf("matched layer\n"); 
    // return 0; 


    elem_t (*down_block2_concat2_out)[80][120][96] = (elem_t (*)[80][120][96]) down_block2_concat2_temp;


    // down block 2, conv_31
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {16,"down_block2_conv31_out","tiled_matmul_auto",
      {{(const void*)(down_block2_concat2_out),down_block2_conv31_params.I,down_block2_conv31_params.K,down_block2_conv31_params.K,1},{(const void*)(down_block2_conv31_w),1,sizeof(down_block2_conv31_w)/1,sizeof(down_block2_conv31_w)/1,1},{(const void*)(down_block2_conv31_b),1,sizeof(down_block2_conv31_b)/4,sizeof(down_block2_conv31_b)/4,4}},{(const void*)(down_block2_conv31_out),down_block2_conv31_params.I,down_block2_conv31_params.J,down_block2_conv31_params.J,1},
      {(double)(float)(down_block2_conv31_params.I),(double)(float)(down_block2_conv31_params.J),(double)(float)(down_block2_conv31_params.K),(double)(float)(down_block2_conv31_params.K),(double)(float)(down_block2_conv31_params.J),(double)(float)(down_block2_conv31_params.J),(double)(float)(down_block2_conv31_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block2_conv31_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block2_conv31_params.I, down_block2_conv31_params.J, down_block2_conv31_params.K,
        down_block2_concat2_out, down_block2_conv31_w, down_block2_conv31_b, down_block2_conv31_out,
        down_block2_conv31_params.K, down_block2_conv31_params.J, down_block2_conv31_params.J, down_block2_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block2_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("db_2_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 2, conv_32 relu (for concat later)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {17,"up_block3_conv12_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block2_conv31_out),(down_block2_conv32_params.batch_size)*(down_block2_conv32_params.in_row_dim)*(down_block2_conv32_params.in_col_dim),down_block2_conv32_params.in_channels,down_block2_conv32_params.in_channels,1},{(const void*)(down_block2_conv32_w),1,sizeof(down_block2_conv32_w)/1,sizeof(down_block2_conv32_w)/1,1},{(const void*)(down_block2_conv32_b),1,sizeof(down_block2_conv32_b)/4,sizeof(down_block2_conv32_b)/4,4}},{(const void*)((elem_t*) up_block3_conv12_concat2_temp),(down_block2_conv32_params.batch_size)*(((1)==0?(80):((80)+2*(down_block2_conv32_params.pool_padding)-(1))/(1)+1))*(((1)==0?(120):((120)+2*(down_block2_conv32_params.pool_padding)-(1))/(1)+1)),down_block2_conv32_params.out_channels,96,1},
      {(double)(float)(down_block2_conv32_params.batch_size),(double)(float)(down_block2_conv32_params.in_row_dim),(double)(float)(down_block2_conv32_params.in_col_dim),(double)(float)(down_block2_conv32_params.in_channels),(double)(float)(down_block2_conv32_params.out_channels),(double)(float)(80),(double)(float)(120),(double)(float)(down_block2_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block2_conv32_params.padding),(double)(float)(down_block2_conv32_params.kernel_size),(double)(float)(down_block2_conv32_params.in_channels),(double)(float)(down_block2_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block2_conv32_params.output_scale),(double)(float)(1),(double)(float)(1),(double)(float)(down_block2_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block2_conv32_params.batch_size, 
        /* in_row_dim */    down_block2_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block2_conv32_params.in_col_dim,
        /* in_channels */   down_block2_conv32_params.in_channels,
        /* out_channels */  down_block2_conv32_params.out_channels, 
        /* out_row_dim */   80, 
        /* out_col_dim */   120,
        /* stride */        down_block2_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block2_conv32_params.padding, 
        /* kernel_dim */    down_block2_conv32_params.kernel_size,
        /* in_stride */     down_block2_conv32_params.in_channels, 
        /* weight_stride */ down_block2_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block2_conv31_out, 
        /* weights */       (elem_t*)   down_block2_conv32_w, 
        /* bias */          (acc_t*)    down_block2_conv32_b, 
        /* output */        (elem_t*)   up_block3_conv12_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block2_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block2_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_2_conv_32_relu cycles: %llu \n", end - start);


    // down block 2, conv_32 (for relu and pool)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {18,"down_block3_conv22_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block2_conv31_out),(down_block2_conv32_params.batch_size)*(down_block2_conv32_params.in_row_dim)*(down_block2_conv32_params.in_col_dim),down_block2_conv32_params.in_channels,down_block2_conv32_params.in_channels,1},{(const void*)(down_block2_conv32_w),1,sizeof(down_block2_conv32_w)/1,sizeof(down_block2_conv32_w)/1,1},{(const void*)(down_block2_conv32_b),1,sizeof(down_block2_conv32_b)/4,sizeof(down_block2_conv32_b)/4,4}},{(const void*)((elem_t*) down_block3_conv22_concat2_temp),(down_block2_conv32_params.batch_size)*(((down_block2_conv32_params.pool_stride)==0?(down_block2_conv32_params.out_row_dim):((down_block2_conv32_params.out_row_dim)+2*(down_block2_conv32_params.pool_padding)-(down_block2_conv32_params.pool_size))/(down_block2_conv32_params.pool_stride)+1))*(((down_block2_conv32_params.pool_stride)==0?(down_block2_conv32_params.out_col_dim):((down_block2_conv32_params.out_col_dim)+2*(down_block2_conv32_params.pool_padding)-(down_block2_conv32_params.pool_size))/(down_block2_conv32_params.pool_stride)+1)),down_block2_conv32_params.out_channels,96,1},
      {(double)(float)(down_block2_conv32_params.batch_size),(double)(float)(down_block2_conv32_params.in_row_dim),(double)(float)(down_block2_conv32_params.in_col_dim),(double)(float)(down_block2_conv32_params.in_channels),(double)(float)(down_block2_conv32_params.out_channels),(double)(float)(down_block2_conv32_params.out_row_dim),(double)(float)(down_block2_conv32_params.out_col_dim),(double)(float)(down_block2_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block2_conv32_params.padding),(double)(float)(down_block2_conv32_params.kernel_size),(double)(float)(down_block2_conv32_params.in_channels),(double)(float)(down_block2_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block2_conv32_params.output_scale),(double)(float)(down_block2_conv32_params.pool_size),(double)(float)(down_block2_conv32_params.pool_stride),(double)(float)(down_block2_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block2_conv32_params.batch_size, 
        /* in_row_dim */    down_block2_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block2_conv32_params.in_col_dim,
        /* in_channels */   down_block2_conv32_params.in_channels,
        /* out_channels */  down_block2_conv32_params.out_channels, 
        /* out_row_dim */   down_block2_conv32_params.out_row_dim, 
        /* out_col_dim */   down_block2_conv32_params.out_col_dim,
        /* stride */        down_block2_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block2_conv32_params.padding, 
        /* kernel_dim */    down_block2_conv32_params.kernel_size,
        /* in_stride */     down_block2_conv32_params.in_channels, 
        /* weight_stride */ down_block2_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block2_conv31_out, 
        /* weights */       (elem_t*)   down_block2_conv32_w, 
        /* bias */          (acc_t*)    down_block2_conv32_b, 
        /* output */        (elem_t*)   down_block3_conv22_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block2_conv32_params.output_scale,
        /* pool_size */     down_block2_conv32_params.pool_size, 
        /* pool_stride */   down_block2_conv32_params.pool_stride, 
        /* pool_padding */  down_block2_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_2_conv_32_relu_pool cycles: %llu \n", end - start);
    
// down block 3

    // down block 3, conv_1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {19,"down_block3_conv22_concat2_temp+32","tiled_conv_stride_auto",
      {{(const void*)(down_block3_conv22_concat2_temp),(down_block3_conv1_params.batch_size)*(down_block3_conv1_params.in_row_dim)*(down_block3_conv1_params.in_col_dim),down_block3_conv1_params.in_channels,96,1},{(const void*)(down_block3_conv1_w),1,sizeof(down_block3_conv1_w)/1,sizeof(down_block3_conv1_w)/1,1},{(const void*)(down_block3_conv1_b),1,sizeof(down_block3_conv1_b)/4,sizeof(down_block3_conv1_b)/4,4}},{(const void*)(down_block3_conv22_concat2_temp+32),(down_block3_conv1_params.batch_size)*(((down_block3_conv1_params.pool_stride)==0?(down_block3_conv1_params.out_row_dim):((down_block3_conv1_params.out_row_dim)+2*(down_block3_conv1_params.pool_padding)-(down_block3_conv1_params.pool_size))/(down_block3_conv1_params.pool_stride)+1))*(((down_block3_conv1_params.pool_stride)==0?(down_block3_conv1_params.out_col_dim):((down_block3_conv1_params.out_col_dim)+2*(down_block3_conv1_params.pool_padding)-(down_block3_conv1_params.pool_size))/(down_block3_conv1_params.pool_stride)+1)),down_block3_conv1_params.out_channels,96,1},
      {(double)(float)(down_block3_conv1_params.batch_size),(double)(float)(down_block3_conv1_params.in_row_dim),(double)(float)(down_block3_conv1_params.in_col_dim),(double)(float)(down_block3_conv1_params.in_channels),(double)(float)(down_block3_conv1_params.out_channels),(double)(float)(down_block3_conv1_params.out_row_dim),(double)(float)(down_block3_conv1_params.out_col_dim),(double)(float)(down_block3_conv1_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block3_conv1_params.padding),(double)(float)(down_block3_conv1_params.kernel_size),(double)(float)(96),(double)(float)(down_block3_conv1_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block3_conv1_params.output_scale),(double)(float)(down_block3_conv1_params.pool_size),(double)(float)(down_block3_conv1_params.pool_stride),(double)(float)(down_block3_conv1_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block3_conv1_params.batch_size, 
        /* in_row_dim */        down_block3_conv1_params.in_row_dim, 
        /* in_col_dim */        down_block3_conv1_params.in_col_dim, 
        /* in_channels */       down_block3_conv1_params.in_channels,
        /* out_channels */      down_block3_conv1_params.out_channels, 
        /* out_row_dim */       down_block3_conv1_params.out_row_dim, 
        /* out_col_dim */       down_block3_conv1_params.out_col_dim,
        /* stride */            down_block3_conv1_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block3_conv1_params.padding, 
        /* kernel_dim */        down_block3_conv1_params.kernel_size,
        /* in_stride */         96, 
        /* weight_stride */     down_block3_conv1_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block3_conv22_concat2_temp, 
        /* weights */           down_block3_conv1_w, 
        /* bias */              down_block3_conv1_b, 
        /* output */            down_block3_conv22_concat2_temp+32,

        /* activation */        RELU, 
        /* scale */             down_block3_conv1_params.output_scale, 
        /* pool_size */         down_block3_conv1_params.pool_size, 
        /* pool_stride */       down_block3_conv1_params.pool_stride, 
        /* pool_padding */      down_block3_conv1_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_3_conv1_concat1 cycles: %llu \n", end - start);


    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {20,"down_block3_conv21_out","tiled_matmul_auto",
      {{(const void*)(down_block3_conv22_concat2_temp),down_block3_conv21_params.I,down_block3_conv21_params.K,96,1},{(const void*)(down_block3_conv21_w),1,sizeof(down_block3_conv21_w)/1,sizeof(down_block3_conv21_w)/1,1},{(const void*)(down_block3_conv21_b),1,sizeof(down_block3_conv21_b)/4,sizeof(down_block3_conv21_b)/4,4}},{(const void*)(down_block3_conv21_out),down_block3_conv21_params.I,down_block3_conv21_params.J,down_block3_conv21_params.J,1},
      {(double)(float)(down_block3_conv21_params.I),(double)(float)(down_block3_conv21_params.J),(double)(float)(down_block3_conv21_params.K),(double)(float)(96),(double)(float)(down_block3_conv21_params.J),(double)(float)(down_block3_conv21_params.J),(double)(float)(down_block3_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block3_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block3_conv21_params.I, down_block3_conv21_params.J, down_block3_conv21_params.K,
        down_block3_conv22_concat2_temp, down_block3_conv21_w, down_block3_conv21_b, down_block3_conv21_out,
        96, down_block3_conv21_params.J, down_block3_conv21_params.J, down_block3_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block3_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_3_conv_21 (matmul) cycles: %llu \n", end - start);

    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {21,"down_block3_conv22_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(down_block3_conv21_out),(down_block3_conv22_params.batch_size)*(down_block3_conv22_params.in_row_dim)*(down_block3_conv22_params.in_col_dim),down_block3_conv22_params.in_channels,down_block3_conv22_params.in_channels,1},{(const void*)(down_block3_conv22_w),1,sizeof(down_block3_conv22_w)/1,sizeof(down_block3_conv22_w)/1,1},{(const void*)(down_block3_conv22_b),1,sizeof(down_block3_conv22_b)/4,sizeof(down_block3_conv22_b)/4,4}},{(const void*)(down_block3_conv22_concat2_temp+64),(down_block3_conv22_params.batch_size)*(((down_block2_conv22_params.pool_stride)==0?(down_block3_conv22_params.out_row_dim):((down_block3_conv22_params.out_row_dim)+2*(down_block2_conv22_params.pool_padding)-(down_block2_conv22_params.pool_size))/(down_block2_conv22_params.pool_stride)+1))*(((down_block2_conv22_params.pool_stride)==0?(down_block3_conv22_params.out_col_dim):((down_block3_conv22_params.out_col_dim)+2*(down_block2_conv22_params.pool_padding)-(down_block2_conv22_params.pool_size))/(down_block2_conv22_params.pool_stride)+1)),down_block3_conv22_params.out_channels,96,1},
      {(double)(float)(down_block3_conv22_params.batch_size),(double)(float)(down_block3_conv22_params.in_row_dim),(double)(float)(down_block3_conv22_params.in_col_dim),(double)(float)(down_block3_conv22_params.in_channels),(double)(float)(down_block3_conv22_params.out_channels),(double)(float)(down_block3_conv22_params.out_row_dim),(double)(float)(down_block3_conv22_params.out_col_dim),(double)(float)(down_block3_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block3_conv22_params.padding),(double)(float)(down_block3_conv22_params.kernel_size),(double)(float)(down_block3_conv22_params.in_channels),(double)(float)(down_block3_conv22_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block2_conv22_params.output_scale),(double)(float)(down_block2_conv22_params.pool_size),(double)(float)(down_block2_conv22_params.pool_stride),(double)(float)(down_block2_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block3_conv22_params.batch_size, 
        /* in_row_dim */        down_block3_conv22_params.in_row_dim, 
        /* in_col_dim */        down_block3_conv22_params.in_col_dim, 
        /* in_channels */       down_block3_conv22_params.in_channels,
        /* out_channels */      down_block3_conv22_params.out_channels, 
        /* out_row_dim */       down_block3_conv22_params.out_row_dim, 
        /* out_col_dim */       down_block3_conv22_params.out_col_dim,
        /* stride */            down_block3_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block3_conv22_params.padding, 
        /* kernel_dim */        down_block3_conv22_params.kernel_size,
        /* in_stride */         down_block3_conv22_params.in_channels, 
        /* weight_stride */     down_block3_conv22_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block3_conv21_out, 
        /* weights */           down_block3_conv22_w, 
        /* bias */              down_block3_conv22_b, 
        /* output */            down_block3_conv22_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             down_block2_conv22_params.output_scale, 
        /* pool_size */         down_block2_conv22_params.pool_size, 
        /* pool_stride */       down_block2_conv22_params.pool_stride, 
        /* pool_padding */      down_block2_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_3_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block3_conv22_concat2_out)[40][60][96] = (elem_t (*)[40][60][96]) down_block3_conv22_concat2_temp;


    // down block 3, conv_31
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {22,"down_block3_conv31_out","tiled_matmul_auto",
      {{(const void*)(down_block3_conv22_concat2_out),down_block3_conv31_params.I,down_block3_conv31_params.K,down_block3_conv31_params.K,1},{(const void*)(down_block3_conv31_w),1,sizeof(down_block3_conv31_w)/1,sizeof(down_block3_conv31_w)/1,1},{(const void*)(down_block3_conv31_b),1,sizeof(down_block3_conv31_b)/4,sizeof(down_block3_conv31_b)/4,4}},{(const void*)(down_block3_conv31_out),down_block3_conv31_params.I,down_block3_conv31_params.J,down_block3_conv31_params.J,1},
      {(double)(float)(down_block3_conv31_params.I),(double)(float)(down_block3_conv31_params.J),(double)(float)(down_block3_conv31_params.K),(double)(float)(down_block3_conv31_params.K),(double)(float)(down_block3_conv31_params.J),(double)(float)(down_block3_conv31_params.J),(double)(float)(down_block3_conv31_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block3_conv31_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block3_conv31_params.I, down_block3_conv31_params.J, down_block3_conv31_params.K,
        down_block3_conv22_concat2_out, down_block3_conv31_w, down_block3_conv31_b, down_block3_conv31_out,
        down_block3_conv31_params.K, down_block3_conv31_params.J, down_block3_conv31_params.J, down_block3_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block3_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("db_3_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 3, conv_32 relu (for concat later)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {23,"up_block2_conv12_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block3_conv31_out),(down_block3_conv32_params.batch_size)*(down_block3_conv32_params.in_row_dim)*(down_block3_conv32_params.in_col_dim),down_block3_conv32_params.in_channels,down_block3_conv32_params.in_channels,1},{(const void*)(down_block3_conv32_w),1,sizeof(down_block3_conv32_w)/1,sizeof(down_block3_conv32_w)/1,1},{(const void*)(down_block3_conv32_b),1,sizeof(down_block3_conv32_b)/4,sizeof(down_block3_conv32_b)/4,4}},{(const void*)((elem_t*) up_block2_conv12_concat2_temp),(down_block3_conv32_params.batch_size)*(((1)==0?(40):((40)+2*(down_block3_conv32_params.pool_padding)-(1))/(1)+1))*(((1)==0?(60):((60)+2*(down_block3_conv32_params.pool_padding)-(1))/(1)+1)),down_block3_conv32_params.out_channels,96,1},
      {(double)(float)(down_block3_conv32_params.batch_size),(double)(float)(down_block3_conv32_params.in_row_dim),(double)(float)(down_block3_conv32_params.in_col_dim),(double)(float)(down_block3_conv32_params.in_channels),(double)(float)(down_block3_conv32_params.out_channels),(double)(float)(40),(double)(float)(60),(double)(float)(down_block3_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block3_conv32_params.padding),(double)(float)(down_block3_conv32_params.kernel_size),(double)(float)(down_block3_conv32_params.in_channels),(double)(float)(down_block3_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block3_conv32_params.output_scale),(double)(float)(1),(double)(float)(1),(double)(float)(down_block3_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block3_conv32_params.batch_size, 
        /* in_row_dim */    down_block3_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block3_conv32_params.in_col_dim,
        /* in_channels */   down_block3_conv32_params.in_channels,
        /* out_channels */  down_block3_conv32_params.out_channels, 
        /* out_row_dim */   40, 
        /* out_col_dim */   60,
        /* stride */        down_block3_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block3_conv32_params.padding, 
        /* kernel_dim */    down_block3_conv32_params.kernel_size,
        /* in_stride */     down_block3_conv32_params.in_channels, 
        /* weight_stride */ down_block3_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block3_conv31_out, 
        /* weights */       (elem_t*)   down_block3_conv32_w, 
        /* bias */          (acc_t*)    down_block3_conv32_b, 
        /* output */        (elem_t*)   up_block2_conv12_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block3_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block3_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_3_conv_32_relu cycles: %llu \n", end - start);


    // down block 3, conv_32 (for relu and pool)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {24,"down_block4_conv22_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block3_conv31_out),(down_block3_conv32_params.batch_size)*(down_block3_conv32_params.in_row_dim)*(down_block3_conv32_params.in_col_dim),down_block3_conv32_params.in_channels,down_block3_conv32_params.in_channels,1},{(const void*)(down_block3_conv32_w),1,sizeof(down_block3_conv32_w)/1,sizeof(down_block3_conv32_w)/1,1},{(const void*)(down_block3_conv32_b),1,sizeof(down_block3_conv32_b)/4,sizeof(down_block3_conv32_b)/4,4}},{(const void*)((elem_t*) down_block4_conv22_concat2_temp),(down_block3_conv32_params.batch_size)*(((down_block3_conv32_params.pool_stride)==0?(down_block3_conv32_params.out_row_dim):((down_block3_conv32_params.out_row_dim)+2*(down_block3_conv32_params.pool_padding)-(down_block3_conv32_params.pool_size))/(down_block3_conv32_params.pool_stride)+1))*(((down_block3_conv32_params.pool_stride)==0?(down_block3_conv32_params.out_col_dim):((down_block3_conv32_params.out_col_dim)+2*(down_block3_conv32_params.pool_padding)-(down_block3_conv32_params.pool_size))/(down_block3_conv32_params.pool_stride)+1)),down_block3_conv32_params.out_channels,96,1},
      {(double)(float)(down_block3_conv32_params.batch_size),(double)(float)(down_block3_conv32_params.in_row_dim),(double)(float)(down_block3_conv32_params.in_col_dim),(double)(float)(down_block3_conv32_params.in_channels),(double)(float)(down_block3_conv32_params.out_channels),(double)(float)(down_block3_conv32_params.out_row_dim),(double)(float)(down_block3_conv32_params.out_col_dim),(double)(float)(down_block3_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block3_conv32_params.padding),(double)(float)(down_block3_conv32_params.kernel_size),(double)(float)(down_block3_conv32_params.in_channels),(double)(float)(down_block3_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block3_conv32_params.output_scale),(double)(float)(down_block3_conv32_params.pool_size),(double)(float)(down_block3_conv32_params.pool_stride),(double)(float)(down_block3_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block3_conv32_params.batch_size, 
        /* in_row_dim */    down_block3_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block3_conv32_params.in_col_dim,
        /* in_channels */   down_block3_conv32_params.in_channels,
        /* out_channels */  down_block3_conv32_params.out_channels, 
        /* out_row_dim */   down_block3_conv32_params.out_row_dim, 
        /* out_col_dim */   down_block3_conv32_params.out_col_dim,
        /* stride */        down_block3_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block3_conv32_params.padding, 
        /* kernel_dim */    down_block3_conv32_params.kernel_size,
        /* in_stride */     down_block3_conv32_params.in_channels, 
        /* weight_stride */ down_block3_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block3_conv31_out, 
        /* weights */       (elem_t*)   down_block3_conv32_w, 
        /* bias */          (acc_t*)    down_block3_conv32_b, 
        /* output */        (elem_t*)   down_block4_conv22_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block3_conv32_params.output_scale,
        /* pool_size */     down_block3_conv32_params.pool_size, 
        /* pool_stride */   down_block3_conv32_params.pool_stride, 
        /* pool_padding */  down_block3_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_3_conv_32_relu_pool cycles: %llu \n", end - start);
    

// down block 4

    // down block 4, conv_1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {25,"down_block4_conv22_concat2_temp+32","tiled_conv_stride_auto",
      {{(const void*)(down_block4_conv22_concat2_temp),(down_block4_conv1_params.batch_size)*(down_block4_conv1_params.in_row_dim)*(down_block4_conv1_params.in_col_dim),down_block4_conv1_params.in_channels,96,1},{(const void*)(down_block4_conv1_w),1,sizeof(down_block4_conv1_w)/1,sizeof(down_block4_conv1_w)/1,1},{(const void*)(down_block4_conv1_b),1,sizeof(down_block4_conv1_b)/4,sizeof(down_block4_conv1_b)/4,4}},{(const void*)(down_block4_conv22_concat2_temp+32),(down_block4_conv1_params.batch_size)*(((down_block4_conv1_params.pool_stride)==0?(down_block4_conv1_params.out_row_dim):((down_block4_conv1_params.out_row_dim)+2*(down_block4_conv1_params.pool_padding)-(down_block4_conv1_params.pool_size))/(down_block4_conv1_params.pool_stride)+1))*(((down_block4_conv1_params.pool_stride)==0?(down_block4_conv1_params.out_col_dim):((down_block4_conv1_params.out_col_dim)+2*(down_block4_conv1_params.pool_padding)-(down_block4_conv1_params.pool_size))/(down_block4_conv1_params.pool_stride)+1)),down_block4_conv1_params.out_channels,96,1},
      {(double)(float)(down_block4_conv1_params.batch_size),(double)(float)(down_block4_conv1_params.in_row_dim),(double)(float)(down_block4_conv1_params.in_col_dim),(double)(float)(down_block4_conv1_params.in_channels),(double)(float)(down_block4_conv1_params.out_channels),(double)(float)(down_block4_conv1_params.out_row_dim),(double)(float)(down_block4_conv1_params.out_col_dim),(double)(float)(down_block4_conv1_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block4_conv1_params.padding),(double)(float)(down_block4_conv1_params.kernel_size),(double)(float)(96),(double)(float)(down_block4_conv1_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block4_conv1_params.output_scale),(double)(float)(down_block4_conv1_params.pool_size),(double)(float)(down_block4_conv1_params.pool_stride),(double)(float)(down_block4_conv1_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block4_conv1_params.batch_size, 
        /* in_row_dim */        down_block4_conv1_params.in_row_dim, 
        /* in_col_dim */        down_block4_conv1_params.in_col_dim, 
        /* in_channels */       down_block4_conv1_params.in_channels,
        /* out_channels */      down_block4_conv1_params.out_channels, 
        /* out_row_dim */       down_block4_conv1_params.out_row_dim, 
        /* out_col_dim */       down_block4_conv1_params.out_col_dim,
        /* stride */            down_block4_conv1_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block4_conv1_params.padding, 
        /* kernel_dim */        down_block4_conv1_params.kernel_size,
        /* in_stride */         96, 
        /* weight_stride */     down_block4_conv1_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block4_conv22_concat2_temp, 
        /* weights */           down_block4_conv1_w, 
        /* bias */              down_block4_conv1_b, 
        /* output */            down_block4_conv22_concat2_temp+32,

        /* activation */        RELU, 
        /* scale */             down_block4_conv1_params.output_scale, 
        /* pool_size */         down_block4_conv1_params.pool_size, 
        /* pool_stride */       down_block4_conv1_params.pool_stride, 
        /* pool_padding */      down_block4_conv1_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_4_conv1_concat1 cycles: %llu \n", end - start);


    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {26,"down_block4_conv21_out","tiled_matmul_auto",
      {{(const void*)(down_block4_conv22_concat2_temp),down_block4_conv21_params.I,down_block4_conv21_params.K,96,1},{(const void*)(down_block4_conv21_w),1,sizeof(down_block4_conv21_w)/1,sizeof(down_block4_conv21_w)/1,1},{(const void*)(down_block4_conv21_b),1,sizeof(down_block4_conv21_b)/4,sizeof(down_block4_conv21_b)/4,4}},{(const void*)(down_block4_conv21_out),down_block4_conv21_params.I,down_block4_conv21_params.J,down_block4_conv21_params.J,1},
      {(double)(float)(down_block4_conv21_params.I),(double)(float)(down_block4_conv21_params.J),(double)(float)(down_block4_conv21_params.K),(double)(float)(96),(double)(float)(down_block4_conv21_params.J),(double)(float)(down_block4_conv21_params.J),(double)(float)(down_block4_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block4_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block4_conv21_params.I, down_block4_conv21_params.J, down_block4_conv21_params.K,
        down_block4_conv22_concat2_temp, down_block4_conv21_w, down_block4_conv21_b, down_block4_conv21_out,
        96, down_block4_conv21_params.J, down_block4_conv21_params.J, down_block4_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block4_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_4_conv_21 (matmul) cycles: %llu \n", end - start);

    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {27,"down_block4_conv22_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(down_block4_conv21_out),(down_block4_conv22_params.batch_size)*(down_block4_conv22_params.in_row_dim)*(down_block4_conv22_params.in_col_dim),down_block4_conv22_params.in_channels,down_block4_conv22_params.in_channels,1},{(const void*)(down_block4_conv22_w),1,sizeof(down_block4_conv22_w)/1,sizeof(down_block4_conv22_w)/1,1},{(const void*)(down_block4_conv22_b),1,sizeof(down_block4_conv22_b)/4,sizeof(down_block4_conv22_b)/4,4}},{(const void*)(down_block4_conv22_concat2_temp+64),(down_block4_conv22_params.batch_size)*(((down_block4_conv22_params.pool_stride)==0?(down_block4_conv22_params.out_row_dim):((down_block4_conv22_params.out_row_dim)+2*(down_block4_conv22_params.pool_padding)-(down_block4_conv22_params.pool_size))/(down_block4_conv22_params.pool_stride)+1))*(((down_block4_conv22_params.pool_stride)==0?(down_block4_conv22_params.out_col_dim):((down_block4_conv22_params.out_col_dim)+2*(down_block4_conv22_params.pool_padding)-(down_block4_conv22_params.pool_size))/(down_block4_conv22_params.pool_stride)+1)),down_block4_conv22_params.out_channels,96,1},
      {(double)(float)(down_block4_conv22_params.batch_size),(double)(float)(down_block4_conv22_params.in_row_dim),(double)(float)(down_block4_conv22_params.in_col_dim),(double)(float)(down_block4_conv22_params.in_channels),(double)(float)(down_block4_conv22_params.out_channels),(double)(float)(down_block4_conv22_params.out_row_dim),(double)(float)(down_block4_conv22_params.out_col_dim),(double)(float)(down_block4_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block4_conv22_params.padding),(double)(float)(down_block4_conv22_params.kernel_size),(double)(float)(down_block4_conv22_params.in_channels),(double)(float)(down_block4_conv22_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block4_conv22_params.output_scale),(double)(float)(down_block4_conv22_params.pool_size),(double)(float)(down_block4_conv22_params.pool_stride),(double)(float)(down_block4_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block4_conv22_params.batch_size, 
        /* in_row_dim */        down_block4_conv22_params.in_row_dim, 
        /* in_col_dim */        down_block4_conv22_params.in_col_dim, 
        /* in_channels */       down_block4_conv22_params.in_channels,
        /* out_channels */      down_block4_conv22_params.out_channels, 
        /* out_row_dim */       down_block4_conv22_params.out_row_dim, 
        /* out_col_dim */       down_block4_conv22_params.out_col_dim,
        /* stride */            down_block4_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block4_conv22_params.padding, 
        /* kernel_dim */        down_block4_conv22_params.kernel_size,
        /* in_stride */         down_block4_conv22_params.in_channels, 
        /* weight_stride */     down_block4_conv22_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block4_conv21_out, 
        /* weights */           down_block4_conv22_w, 
        /* bias */              down_block4_conv22_b, 
        /* output */            down_block4_conv22_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             down_block4_conv22_params.output_scale, 
        /* pool_size */         down_block4_conv22_params.pool_size, 
        /* pool_stride */       down_block4_conv22_params.pool_stride, 
        /* pool_padding */      down_block4_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_4_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block4_conv22_concat2_out)[20][30][96] = (elem_t (*)[20][30][96]) down_block4_conv22_concat2_temp;


    // down block 4, conv_31
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {28,"down_block4_conv31_out","tiled_matmul_auto",
      {{(const void*)(down_block4_conv22_concat2_out),down_block4_conv31_params.I,down_block4_conv31_params.K,down_block4_conv31_params.K,1},{(const void*)(down_block4_conv31_w),1,sizeof(down_block4_conv31_w)/1,sizeof(down_block4_conv31_w)/1,1},{(const void*)(down_block4_conv31_b),1,sizeof(down_block4_conv31_b)/4,sizeof(down_block4_conv31_b)/4,4}},{(const void*)(down_block4_conv31_out),down_block4_conv31_params.I,down_block4_conv31_params.J,down_block4_conv31_params.J,1},
      {(double)(float)(down_block4_conv31_params.I),(double)(float)(down_block4_conv31_params.J),(double)(float)(down_block4_conv31_params.K),(double)(float)(down_block4_conv31_params.K),(double)(float)(down_block4_conv31_params.J),(double)(float)(down_block4_conv31_params.J),(double)(float)(down_block4_conv31_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block4_conv31_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block4_conv31_params.I, down_block4_conv31_params.J, down_block4_conv31_params.K,
        down_block4_conv22_concat2_out, down_block4_conv31_w, down_block4_conv31_b, down_block4_conv31_out,
        down_block4_conv31_params.K, down_block4_conv31_params.J, down_block4_conv31_params.J, down_block4_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block4_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("db_4_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 4, conv_32 relu (for concat later)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {29,"up_block1_conv12_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block4_conv31_out),(down_block4_conv32_params.batch_size)*(down_block4_conv32_params.in_row_dim)*(down_block4_conv32_params.in_col_dim),down_block4_conv32_params.in_channels,down_block4_conv32_params.in_channels,1},{(const void*)(down_block4_conv32_w),1,sizeof(down_block4_conv32_w)/1,sizeof(down_block4_conv32_w)/1,1},{(const void*)(down_block4_conv32_b),1,sizeof(down_block4_conv32_b)/4,sizeof(down_block4_conv32_b)/4,4}},{(const void*)((elem_t*) up_block1_conv12_concat2_temp),(down_block4_conv32_params.batch_size)*(((1)==0?(20):((20)+2*(down_block4_conv32_params.pool_padding)-(1))/(1)+1))*(((1)==0?(30):((30)+2*(down_block4_conv32_params.pool_padding)-(1))/(1)+1)),down_block4_conv32_params.out_channels,96,1},
      {(double)(float)(down_block4_conv32_params.batch_size),(double)(float)(down_block4_conv32_params.in_row_dim),(double)(float)(down_block4_conv32_params.in_col_dim),(double)(float)(down_block4_conv32_params.in_channels),(double)(float)(down_block4_conv32_params.out_channels),(double)(float)(20),(double)(float)(30),(double)(float)(down_block4_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block4_conv32_params.padding),(double)(float)(down_block4_conv32_params.kernel_size),(double)(float)(down_block4_conv32_params.in_channels),(double)(float)(down_block4_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block4_conv32_params.output_scale),(double)(float)(1),(double)(float)(1),(double)(float)(down_block4_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block4_conv32_params.batch_size, 
        /* in_row_dim */    down_block4_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block4_conv32_params.in_col_dim,
        /* in_channels */   down_block4_conv32_params.in_channels,
        /* out_channels */  down_block4_conv32_params.out_channels, 
        /* out_row_dim */   20, 
        /* out_col_dim */   30,
        /* stride */        down_block4_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block4_conv32_params.padding, 
        /* kernel_dim */    down_block4_conv32_params.kernel_size,
        /* in_stride */     down_block4_conv32_params.in_channels, 
        /* weight_stride */ down_block4_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block4_conv31_out, 
        /* weights */       (elem_t*)   down_block4_conv32_w, 
        /* bias */          (acc_t*)    down_block4_conv32_b, 
        /* output */        (elem_t*)   up_block1_conv12_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block4_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block4_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_4_conv_32_relu cycles: %llu \n", end - start);


    // down block 4, conv_32 (for relu and pool)
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {30,"down_block5_conv22_concat2_temp","tiled_conv_stride_auto",
      {{(const void*)((elem_t*) down_block4_conv31_out),(down_block4_conv32_params.batch_size)*(down_block4_conv32_params.in_row_dim)*(down_block4_conv32_params.in_col_dim),down_block4_conv32_params.in_channels,down_block4_conv32_params.in_channels,1},{(const void*)(down_block4_conv32_w),1,sizeof(down_block4_conv32_w)/1,sizeof(down_block4_conv32_w)/1,1},{(const void*)(down_block4_conv32_b),1,sizeof(down_block4_conv32_b)/4,sizeof(down_block4_conv32_b)/4,4}},{(const void*)((elem_t*) down_block5_conv22_concat2_temp),(down_block4_conv32_params.batch_size)*(((down_block4_conv32_params.pool_stride)==0?(down_block4_conv32_params.out_row_dim):((down_block4_conv32_params.out_row_dim)+2*(down_block4_conv32_params.pool_padding)-(down_block4_conv32_params.pool_size))/(down_block4_conv32_params.pool_stride)+1))*(((down_block4_conv32_params.pool_stride)==0?(down_block4_conv32_params.out_col_dim):((down_block4_conv32_params.out_col_dim)+2*(down_block4_conv32_params.pool_padding)-(down_block4_conv32_params.pool_size))/(down_block4_conv32_params.pool_stride)+1)),down_block4_conv32_params.out_channels,96,1},
      {(double)(float)(down_block4_conv32_params.batch_size),(double)(float)(down_block4_conv32_params.in_row_dim),(double)(float)(down_block4_conv32_params.in_col_dim),(double)(float)(down_block4_conv32_params.in_channels),(double)(float)(down_block4_conv32_params.out_channels),(double)(float)(down_block4_conv32_params.out_row_dim),(double)(float)(down_block4_conv32_params.out_col_dim),(double)(float)(down_block4_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block4_conv32_params.padding),(double)(float)(down_block4_conv32_params.kernel_size),(double)(float)(down_block4_conv32_params.in_channels),(double)(float)(down_block4_conv32_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block4_conv32_params.output_scale),(double)(float)(down_block4_conv32_params.pool_size),(double)(float)(down_block4_conv32_params.pool_stride),(double)(float)(down_block4_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */    down_block4_conv32_params.batch_size, 
        /* in_row_dim */    down_block4_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block4_conv32_params.in_col_dim,
        /* in_channels */   down_block4_conv32_params.in_channels,
        /* out_channels */  down_block4_conv32_params.out_channels, 
        /* out_row_dim */   down_block4_conv32_params.out_row_dim, 
        /* out_col_dim */   down_block4_conv32_params.out_col_dim,
        /* stride */        down_block4_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block4_conv32_params.padding, 
        /* kernel_dim */    down_block4_conv32_params.kernel_size,
        /* in_stride */     down_block4_conv32_params.in_channels, 
        /* weight_stride */ down_block4_conv32_params.out_channels, 
        /* out_stride */    96,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block4_conv31_out, 
        /* weights */       (elem_t*)   down_block4_conv32_w, 
        /* bias */          (acc_t*)    down_block4_conv32_b, 
        /* output */        (elem_t*)   down_block5_conv22_concat2_temp,

        /* activation */    RELU, 
        /* scale */         down_block4_conv32_params.output_scale,
        /* pool_size */     down_block4_conv32_params.pool_size, 
        /* pool_stride */   down_block4_conv32_params.pool_stride, 
        /* pool_padding */  down_block4_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_4_conv_32_relu_pool cycles: %llu \n", end - start);
    

// down block 5

    // down block 5, conv_1
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {31,"down_block5_conv22_concat2_temp+32","tiled_conv_stride_auto",
      {{(const void*)(down_block5_conv22_concat2_temp),(down_block5_conv1_params.batch_size)*(down_block5_conv1_params.in_row_dim)*(down_block5_conv1_params.in_col_dim),down_block5_conv1_params.in_channels,96,1},{(const void*)(down_block5_conv1_w),1,sizeof(down_block5_conv1_w)/1,sizeof(down_block5_conv1_w)/1,1},{(const void*)(down_block5_conv1_b),1,sizeof(down_block5_conv1_b)/4,sizeof(down_block5_conv1_b)/4,4}},{(const void*)(down_block5_conv22_concat2_temp+32),(down_block5_conv1_params.batch_size)*(((down_block5_conv1_params.pool_stride)==0?(down_block5_conv1_params.out_row_dim):((down_block5_conv1_params.out_row_dim)+2*(down_block5_conv1_params.pool_padding)-(down_block5_conv1_params.pool_size))/(down_block5_conv1_params.pool_stride)+1))*(((down_block5_conv1_params.pool_stride)==0?(down_block5_conv1_params.out_col_dim):((down_block5_conv1_params.out_col_dim)+2*(down_block5_conv1_params.pool_padding)-(down_block5_conv1_params.pool_size))/(down_block5_conv1_params.pool_stride)+1)),down_block5_conv1_params.out_channels,96,1},
      {(double)(float)(down_block5_conv1_params.batch_size),(double)(float)(down_block5_conv1_params.in_row_dim),(double)(float)(down_block5_conv1_params.in_col_dim),(double)(float)(down_block5_conv1_params.in_channels),(double)(float)(down_block5_conv1_params.out_channels),(double)(float)(down_block5_conv1_params.out_row_dim),(double)(float)(down_block5_conv1_params.out_col_dim),(double)(float)(down_block5_conv1_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block5_conv1_params.padding),(double)(float)(down_block5_conv1_params.kernel_size),(double)(float)(96),(double)(float)(down_block5_conv1_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block5_conv1_params.output_scale),(double)(float)(down_block5_conv1_params.pool_size),(double)(float)(down_block5_conv1_params.pool_stride),(double)(float)(down_block5_conv1_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block5_conv1_params.batch_size, 
        /* in_row_dim */        down_block5_conv1_params.in_row_dim, 
        /* in_col_dim */        down_block5_conv1_params.in_col_dim, 
        /* in_channels */       down_block5_conv1_params.in_channels,
        /* out_channels */      down_block5_conv1_params.out_channels, 
        /* out_row_dim */       down_block5_conv1_params.out_row_dim, 
        /* out_col_dim */       down_block5_conv1_params.out_col_dim,
        /* stride */            down_block5_conv1_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block5_conv1_params.padding, 
        /* kernel_dim */        down_block5_conv1_params.kernel_size,
        /* in_stride */         96, 
        /* weight_stride */     down_block5_conv1_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block5_conv22_concat2_temp, 
        /* weights */           down_block5_conv1_w, 
        /* bias */              down_block5_conv1_b, 
        /* output */            down_block5_conv22_concat2_temp+32,

        /* activation */        RELU, 
        /* scale */             down_block5_conv1_params.output_scale, 
        /* pool_size */         down_block5_conv1_params.pool_size, 
        /* pool_stride */       down_block5_conv1_params.pool_stride, 
        /* pool_padding */      down_block5_conv1_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_5_conv1_concat1 cycles: %llu \n", end - start);


    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {32,"down_block5_conv21_out","tiled_matmul_auto",
      {{(const void*)(down_block5_conv22_concat2_temp),down_block5_conv21_params.I,down_block5_conv21_params.K,96,1},{(const void*)(down_block5_conv21_w),1,sizeof(down_block5_conv21_w)/1,sizeof(down_block5_conv21_w)/1,1},{(const void*)(down_block5_conv21_b),1,sizeof(down_block5_conv21_b)/4,sizeof(down_block5_conv21_b)/4,4}},{(const void*)(down_block5_conv21_out),down_block5_conv21_params.I,down_block5_conv21_params.J,down_block5_conv21_params.J,1},
      {(double)(float)(down_block5_conv21_params.I),(double)(float)(down_block5_conv21_params.J),(double)(float)(down_block5_conv21_params.K),(double)(float)(96),(double)(float)(down_block5_conv21_params.J),(double)(float)(down_block5_conv21_params.J),(double)(float)(down_block5_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block5_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block5_conv21_params.I, down_block5_conv21_params.J, down_block5_conv21_params.K,
        down_block5_conv22_concat2_temp, down_block5_conv21_w, down_block5_conv21_b, down_block5_conv21_out,
        96, down_block5_conv21_params.J, down_block5_conv21_params.J, down_block5_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block5_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    matmul_cycles += end - start;
    // printf("db_5_conv_21 (matmul) cycles: %llu \n", end - start);

    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {33,"down_block5_conv22_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(down_block5_conv21_out),(down_block5_conv22_params.batch_size)*(down_block5_conv22_params.in_row_dim)*(down_block5_conv22_params.in_col_dim),down_block5_conv22_params.in_channels,down_block5_conv22_params.in_channels,1},{(const void*)(down_block5_conv22_w),1,sizeof(down_block5_conv22_w)/1,sizeof(down_block5_conv22_w)/1,1},{(const void*)(down_block5_conv22_b),1,sizeof(down_block5_conv22_b)/4,sizeof(down_block5_conv22_b)/4,4}},{(const void*)(down_block5_conv22_concat2_temp+64),(down_block5_conv22_params.batch_size)*(((down_block5_conv22_params.pool_stride)==0?(down_block5_conv22_params.out_row_dim):((down_block5_conv22_params.out_row_dim)+2*(down_block5_conv22_params.pool_padding)-(down_block5_conv22_params.pool_size))/(down_block5_conv22_params.pool_stride)+1))*(((down_block5_conv22_params.pool_stride)==0?(down_block5_conv22_params.out_col_dim):((down_block5_conv22_params.out_col_dim)+2*(down_block5_conv22_params.pool_padding)-(down_block5_conv22_params.pool_size))/(down_block5_conv22_params.pool_stride)+1)),down_block5_conv22_params.out_channels,96,1},
      {(double)(float)(down_block5_conv22_params.batch_size),(double)(float)(down_block5_conv22_params.in_row_dim),(double)(float)(down_block5_conv22_params.in_col_dim),(double)(float)(down_block5_conv22_params.in_channels),(double)(float)(down_block5_conv22_params.out_channels),(double)(float)(down_block5_conv22_params.out_row_dim),(double)(float)(down_block5_conv22_params.out_col_dim),(double)(float)(down_block5_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block5_conv22_params.padding),(double)(float)(down_block5_conv22_params.kernel_size),(double)(float)(down_block5_conv22_params.in_channels),(double)(float)(down_block5_conv22_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block5_conv22_params.output_scale),(double)(float)(down_block5_conv22_params.pool_size),(double)(float)(down_block5_conv22_params.pool_stride),(double)(float)(down_block5_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        down_block5_conv22_params.batch_size, 
        /* in_row_dim */        down_block5_conv22_params.in_row_dim, 
        /* in_col_dim */        down_block5_conv22_params.in_col_dim, 
        /* in_channels */       down_block5_conv22_params.in_channels,
        /* out_channels */      down_block5_conv22_params.out_channels, 
        /* out_row_dim */       down_block5_conv22_params.out_row_dim, 
        /* out_col_dim */       down_block5_conv22_params.out_col_dim,
        /* stride */            down_block5_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           down_block5_conv22_params.padding, 
        /* kernel_dim */        down_block5_conv22_params.kernel_size,
        /* in_stride */         down_block5_conv22_params.in_channels, 
        /* weight_stride */     down_block5_conv22_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             down_block5_conv21_out, 
        /* weights */           down_block5_conv22_w, 
        /* bias */              down_block5_conv22_b, 
        /* output */            down_block5_conv22_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             down_block5_conv22_params.output_scale, 
        /* pool_size */         down_block5_conv22_params.pool_size, 
        /* pool_stride */       down_block5_conv22_params.pool_stride, 
        /* pool_padding */      down_block5_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_5_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block5_conv22_concat2_out)[10][15][96] = (elem_t (*)[10][15][96]) down_block5_conv22_concat2_temp;


    // down block 5, conv_31
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {34,"down_block5_conv31_out","tiled_matmul_auto",
      {{(const void*)(down_block5_conv22_concat2_out),down_block5_conv31_params.I,down_block5_conv31_params.K,down_block5_conv31_params.K,1},{(const void*)(down_block5_conv31_w),1,sizeof(down_block5_conv31_w)/1,sizeof(down_block5_conv31_w)/1,1},{(const void*)(down_block5_conv31_b),1,sizeof(down_block5_conv31_b)/4,sizeof(down_block5_conv31_b)/4,4}},{(const void*)(down_block5_conv31_out),down_block5_conv31_params.I,down_block5_conv31_params.J,down_block5_conv31_params.J,1},
      {(double)(float)(down_block5_conv31_params.I),(double)(float)(down_block5_conv31_params.J),(double)(float)(down_block5_conv31_params.K),(double)(float)(down_block5_conv31_params.K),(double)(float)(down_block5_conv31_params.J),(double)(float)(down_block5_conv31_params.J),(double)(float)(down_block5_conv31_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(down_block5_conv31_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(down_block5_conv31_params.I, down_block5_conv31_params.J, down_block5_conv31_params.K,
        down_block5_conv22_concat2_out, down_block5_conv31_w, down_block5_conv31_b, down_block5_conv31_out,
        down_block5_conv31_params.K, down_block5_conv31_params.J, down_block5_conv31_params.J, down_block5_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block5_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("db_5_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 5, conv_32 relu 
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {35,"down_block5_conv32_out_relu","tiled_conv_auto",
      {{(const void*)((elem_t*) down_block5_conv31_out),(down_block5_conv32_params.batch_size)*(down_block5_conv32_params.in_row_dim)*(down_block5_conv32_params.in_col_dim),down_block5_conv32_params.in_channels,down_block5_conv32_params.in_channels,1},{(const void*)(down_block5_conv32_w),1,sizeof(down_block5_conv32_w)/1,sizeof(down_block5_conv32_w)/1,1},{(const void*)(down_block5_conv32_b),1,sizeof(down_block5_conv32_b)/4,sizeof(down_block5_conv32_b)/4,4}},{(const void*)((elem_t*) down_block5_conv32_out_relu),(down_block5_conv32_params.batch_size)*(((1)==0?(10):((10)+2*(down_block5_conv32_params.pool_padding)-(1))/(1)+1))*(((1)==0?(15):((15)+2*(down_block5_conv32_params.pool_padding)-(1))/(1)+1)),down_block5_conv32_params.out_channels,down_block5_conv32_params.out_channels,1},
      {(double)(float)(down_block5_conv32_params.batch_size),(double)(float)(down_block5_conv32_params.in_row_dim),(double)(float)(down_block5_conv32_params.in_col_dim),(double)(float)(down_block5_conv32_params.in_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(10),(double)(float)(15),(double)(float)(down_block5_conv32_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(down_block5_conv32_params.padding),(double)(float)(down_block5_conv32_params.kernel_size),(double)(float)(down_block5_conv32_params.in_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(down_block5_conv32_params.output_scale),(double)(float)(1),(double)(float)(1),(double)(float)(down_block5_conv32_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */    down_block5_conv32_params.batch_size, 
        /* in_row_dim */    down_block5_conv32_params.in_row_dim, 
        /* in_col_dim */    down_block5_conv32_params.in_col_dim,
        /* in_channels */   down_block5_conv32_params.in_channels,
        /* out_channels */  down_block5_conv32_params.out_channels, 
        /* out_row_dim */   10, 
        /* out_col_dim */   15,
        /* stride */        down_block5_conv32_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */       down_block5_conv32_params.padding, 
        /* kernel_dim */    down_block5_conv32_params.kernel_size,
        false, false, false, false, false,

        /* input */         (elem_t*)   down_block5_conv31_out, 
        /* weights */       (elem_t*)   down_block5_conv32_w, 
        /* bias */          (acc_t*)    down_block5_conv32_b, 
        /* output */        (elem_t*)   down_block5_conv32_out_relu,

        /* activation */    RELU, 
        /* scale */         down_block5_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block5_conv32_params.pool_padding,

        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_5_conv_32_relu cycles: %llu \n", end - start);

    
// up block 1

    // resize down block 5, conv_32 relu from 10x15 up to 20x30 using nearest neighbor interpolation

    // input dilation with 0s
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {36,"up_block1_upsize","tiled_conv_auto",
      {{(const void*)(down_block5_conv32_out_relu),(down_block5_conv32_params.batch_size)*(down_block5_conv32_params.out_row_dim)*(down_block5_conv32_params.out_col_dim),down_block5_conv32_params.in_channels,down_block5_conv32_params.in_channels,1},{(const void*)(upsize_w),1,sizeof(upsize_w)/1,sizeof(upsize_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block1_upsize),(down_block5_conv32_params.batch_size)*(((1)==0?(up_block1_conv11_params.in_row_dim):((up_block1_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block1_conv11_params.in_col_dim):((up_block1_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),down_block5_conv32_params.out_channels,down_block5_conv32_params.out_channels,1},
      {(double)(float)(down_block5_conv32_params.batch_size),(double)(float)(down_block5_conv32_params.out_row_dim),(double)(float)(down_block5_conv32_params.out_col_dim),(double)(float)(down_block5_conv32_params.in_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(up_block1_conv11_params.in_row_dim),(double)(float)(up_block1_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(2),(double)(float)(1),(double)(float)(0),(double)(float)(1),(double)(float)(down_block5_conv32_params.in_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        down_block5_conv32_params.batch_size, 
        /* in_row_dim */        down_block5_conv32_params.out_row_dim, 
        /* in_col_dim */        down_block5_conv32_params.out_col_dim, 
        /* in_channels */       down_block5_conv32_params.in_channels, 
        /* out_channels */      down_block5_conv32_params.out_channels, 
        /* out_row_dim */       up_block1_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block1_conv11_params.in_col_dim, 
        /* stride */            1,  
        /* input_dilation */    2,  
        /* kernel_dilation */   1,  
        /* padding */           0, 
        /* kernel_dim */        1, 
        false, false, false, false, false, 
        /* input */             down_block5_conv32_out_relu, 
        /* weights */           upsize_w, 
        /* bias */              z_bias, 
        /* output */            up_block1_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // nearest neighbor upsample
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {37,"up_block1_upsample","tiled_conv_dw_auto",
      {{(const void*)(up_block1_upsize),(down_block5_conv32_params.batch_size)*(up_block1_conv11_params.in_row_dim)*(up_block1_conv11_params.in_col_dim),down_block5_conv32_params.out_channels,down_block5_conv32_params.out_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block1_upsample),(down_block5_conv32_params.batch_size)*(((1)==0?(up_block1_conv11_params.in_row_dim):((up_block1_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block1_conv11_params.in_col_dim):((up_block1_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),down_block5_conv32_params.out_channels,down_block5_conv32_params.out_channels,1},
      {(double)(float)(down_block5_conv32_params.batch_size),(double)(float)(up_block1_conv11_params.in_row_dim),(double)(float)(up_block1_conv11_params.in_col_dim),(double)(float)(down_block5_conv32_params.out_channels),(double)(float)(up_block1_conv11_params.in_row_dim),(double)(float)(up_block1_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(1),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        down_block5_conv32_params.batch_size, 
        /* in_row_dim */        up_block1_conv11_params.in_row_dim, 
        /* in_col_dim */        up_block1_conv11_params.in_col_dim, 
        /* channels */          down_block5_conv32_params.out_channels, 
        /* out_row_dim */       up_block1_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block1_conv11_params.in_col_dim, 
        /* stride */            1, 
        /* padding */           1, 
        /* kernel_dim */        2, 
        /* input */             up_block1_upsize, 
        /* weights */           upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            up_block1_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // concatenate upsampled tensor 
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {38,"up_block1_conv12_concat2_temp+32","tiled_matmul_auto",
      {{(const void*)(up_block1_upsample),600,32,32,1},{(const void*)(identity_32),1,sizeof(identity_32)/1,sizeof(identity_32)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block1_conv12_concat2_temp+32),600,32,96,1},
      {(double)(float)(600),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(1),(double)(float)(96),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(600, 32, 32, 
        up_block1_upsample, identity_32, z_bias, up_block1_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */
 

    end =  read_cycles();
    conv_cycles += end - start;
    // printf("ub_1_upsample cycles: %llu \n", end - start);


    // up block 1, conv_11
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {39,"up_block1_conv11_out","tiled_matmul_auto",
      {{(const void*)(up_block1_conv12_concat2_temp),up_block1_conv11_params.I,up_block1_conv11_params.K,96,1},{(const void*)(up_block1_conv11_w),1,sizeof(up_block1_conv11_w)/1,sizeof(up_block1_conv11_w)/1,1},{(const void*)(up_block1_conv11_b),1,sizeof(up_block1_conv11_b)/4,sizeof(up_block1_conv11_b)/4,4}},{(const void*)(up_block1_conv11_out),up_block1_conv11_params.I,up_block1_conv11_params.J,up_block1_conv11_params.J,1},
      {(double)(float)(up_block1_conv11_params.I),(double)(float)(up_block1_conv11_params.J),(double)(float)(up_block1_conv11_params.K),(double)(float)(96),(double)(float)(up_block1_conv11_params.J),(double)(float)(up_block1_conv11_params.J),(double)(float)(up_block1_conv11_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block1_conv11_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block1_conv11_params.I, up_block1_conv11_params.J, up_block1_conv11_params.K,
        up_block1_conv12_concat2_temp, up_block1_conv11_w, up_block1_conv11_b, up_block1_conv11_out,
        96, up_block1_conv11_params.J, up_block1_conv11_params.J, up_block1_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block1_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_1_conv_11 (matmul) cycles: %llu \n", end - start);


    // up block 1, conv_12
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {40,"up_block1_conv12_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(up_block1_conv11_out),(up_block1_conv12_params.batch_size)*(up_block1_conv12_params.in_row_dim)*(up_block1_conv12_params.in_col_dim),up_block1_conv12_params.in_channels,up_block1_conv12_params.in_channels,1},{(const void*)(up_block1_conv12_w),1,sizeof(up_block1_conv12_w)/1,sizeof(up_block1_conv12_w)/1,1},{(const void*)(up_block1_conv12_b),1,sizeof(up_block1_conv12_b)/4,sizeof(up_block1_conv12_b)/4,4}},{(const void*)(up_block1_conv12_concat2_temp+64),(up_block1_conv12_params.batch_size)*(((up_block1_conv12_params.pool_stride)==0?(up_block1_conv12_params.out_row_dim):((up_block1_conv12_params.out_row_dim)+2*(up_block1_conv12_params.pool_padding)-(up_block1_conv12_params.pool_size))/(up_block1_conv12_params.pool_stride)+1))*(((up_block1_conv12_params.pool_stride)==0?(up_block1_conv12_params.out_col_dim):((up_block1_conv12_params.out_col_dim)+2*(up_block1_conv12_params.pool_padding)-(up_block1_conv12_params.pool_size))/(up_block1_conv12_params.pool_stride)+1)),up_block1_conv12_params.out_channels,96,1},
      {(double)(float)(up_block1_conv12_params.batch_size),(double)(float)(up_block1_conv12_params.in_row_dim),(double)(float)(up_block1_conv12_params.in_col_dim),(double)(float)(up_block1_conv12_params.in_channels),(double)(float)(up_block1_conv12_params.out_channels),(double)(float)(up_block1_conv12_params.out_row_dim),(double)(float)(up_block1_conv12_params.out_col_dim),(double)(float)(up_block1_conv12_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block1_conv12_params.padding),(double)(float)(up_block1_conv12_params.kernel_size),(double)(float)(up_block1_conv12_params.in_channels),(double)(float)(up_block1_conv12_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block1_conv12_params.output_scale),(double)(float)(up_block1_conv12_params.pool_size),(double)(float)(up_block1_conv12_params.pool_stride),(double)(float)(up_block1_conv12_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        up_block1_conv12_params.batch_size, 
        /* in_row_dim */        up_block1_conv12_params.in_row_dim, 
        /* in_col_dim */        up_block1_conv12_params.in_col_dim, 
        /* in_channels */       up_block1_conv12_params.in_channels,
        /* out_channels */      up_block1_conv12_params.out_channels, 
        /* out_row_dim */       up_block1_conv12_params.out_row_dim, 
        /* out_col_dim */       up_block1_conv12_params.out_col_dim,
        /* stride */            up_block1_conv12_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block1_conv12_params.padding, 
        /* kernel_dim */        up_block1_conv12_params.kernel_size,
        /* in_stride */         up_block1_conv12_params.in_channels, 
        /* weight_stride */     up_block1_conv12_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             up_block1_conv11_out, 
        /* weights */           up_block1_conv12_w, 
        /* bias */              up_block1_conv12_b, 
        /* output */            up_block1_conv12_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             up_block1_conv12_params.output_scale, 
        /* pool_size */         up_block1_conv12_params.pool_size, 
        /* pool_stride */       up_block1_conv12_params.pool_stride, 
        /* pool_padding */      up_block1_conv12_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_1_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block1_conv12_concat2_out)[20][30][96] = (elem_t (*)[20][30][96]) up_block1_conv12_concat2_temp;

    
    
    // up block 1, conv_21
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {41,"up_block1_conv21_out","tiled_matmul_auto",
      {{(const void*)(up_block1_conv12_concat2_out),up_block1_conv21_params.I,up_block1_conv21_params.K,up_block1_conv21_params.K,1},{(const void*)(up_block1_conv21_w),1,sizeof(up_block1_conv21_w)/1,sizeof(up_block1_conv21_w)/1,1},{(const void*)(up_block1_conv21_b),1,sizeof(up_block1_conv21_b)/4,sizeof(up_block1_conv21_b)/4,4}},{(const void*)(up_block1_conv21_out),up_block1_conv21_params.I,up_block1_conv21_params.J,up_block1_conv21_params.J,1},
      {(double)(float)(up_block1_conv21_params.I),(double)(float)(up_block1_conv21_params.J),(double)(float)(up_block1_conv21_params.K),(double)(float)(up_block1_conv21_params.K),(double)(float)(up_block1_conv21_params.J),(double)(float)(up_block1_conv21_params.J),(double)(float)(up_block1_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block1_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block1_conv21_params.I, up_block1_conv21_params.J, up_block1_conv21_params.K,
        up_block1_conv12_concat2_out, up_block1_conv21_w, up_block1_conv21_b, up_block1_conv21_out,
        up_block1_conv21_params.K, up_block1_conv21_params.J, up_block1_conv21_params.J, up_block1_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block1_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_1_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 1, conv_22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {42,"up_block1_conv22_out_relu","tiled_conv_auto",
      {{(const void*)(up_block1_conv21_out),(up_block1_conv22_params.batch_size)*(up_block1_conv22_params.in_row_dim)*(up_block1_conv22_params.in_col_dim),up_block1_conv22_params.in_channels,up_block1_conv22_params.in_channels,1},{(const void*)(up_block1_conv22_w),1,sizeof(up_block1_conv22_w)/1,sizeof(up_block1_conv22_w)/1,1},{(const void*)(up_block1_conv22_b),1,sizeof(up_block1_conv22_b)/4,sizeof(up_block1_conv22_b)/4,4}},{(const void*)(up_block1_conv22_out_relu),(up_block1_conv22_params.batch_size)*(((up_block1_conv22_params.pool_stride)==0?(up_block1_conv22_params.out_row_dim):((up_block1_conv22_params.out_row_dim)+2*(up_block1_conv22_params.pool_padding)-(up_block1_conv22_params.pool_size))/(up_block1_conv22_params.pool_stride)+1))*(((up_block1_conv22_params.pool_stride)==0?(up_block1_conv22_params.out_col_dim):((up_block1_conv22_params.out_col_dim)+2*(up_block1_conv22_params.pool_padding)-(up_block1_conv22_params.pool_size))/(up_block1_conv22_params.pool_stride)+1)),up_block1_conv22_params.out_channels,up_block1_conv22_params.out_channels,1},
      {(double)(float)(up_block1_conv22_params.batch_size),(double)(float)(up_block1_conv22_params.in_row_dim),(double)(float)(up_block1_conv22_params.in_col_dim),(double)(float)(up_block1_conv22_params.in_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(up_block1_conv22_params.out_row_dim),(double)(float)(up_block1_conv22_params.out_col_dim),(double)(float)(up_block1_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block1_conv22_params.padding),(double)(float)(up_block1_conv22_params.kernel_size),(double)(float)(up_block1_conv22_params.in_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block1_conv22_params.output_scale),(double)(float)(up_block1_conv22_params.pool_size),(double)(float)(up_block1_conv22_params.pool_stride),(double)(float)(up_block1_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block1_conv22_params.batch_size, 
        /* in_row_dim */        up_block1_conv22_params.in_row_dim, 
        /* in_col_dim */        up_block1_conv22_params.in_col_dim, 
        /* in_channels */       up_block1_conv22_params.in_channels,
        /* out_channels */      up_block1_conv22_params.out_channels, 
        /* out_row_dim */       up_block1_conv22_params.out_row_dim, 
        /* out_col_dim */       up_block1_conv22_params.out_col_dim,
        /* stride */            up_block1_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block1_conv22_params.padding, 
        /* kernel_dim */        up_block1_conv22_params.kernel_size,
        false, false, false, false, false,
        
        /* input */             up_block1_conv21_out, 
        /* weights */           up_block1_conv22_w, 
        /* bias */              up_block1_conv22_b, 
        /* output */            up_block1_conv22_out_relu,

        /* activation */        RELU, 
        /* scale */             up_block1_conv22_params.output_scale, 
        /* pool_size */         up_block1_conv22_params.pool_size, 
        /* pool_stride */       up_block1_conv22_params.pool_stride, 
        /* pool_padding */      up_block1_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_1_conv_22_concat2 cycles: %llu \n", end - start);


// up block 2

    // input dilation with 0s
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {43,"up_block2_upsize","tiled_conv_auto",
      {{(const void*)(up_block1_conv22_out_relu),(up_block1_conv22_params.batch_size)*(up_block1_conv22_params.out_row_dim)*(up_block1_conv22_params.out_col_dim),up_block1_conv22_params.in_channels,up_block1_conv22_params.in_channels,1},{(const void*)(upsize_w),1,sizeof(upsize_w)/1,sizeof(upsize_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block2_upsize),(up_block1_conv22_params.batch_size)*(((1)==0?(up_block2_conv11_params.in_row_dim):((up_block2_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block2_conv11_params.in_col_dim):((up_block2_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block1_conv22_params.out_channels,up_block1_conv22_params.out_channels,1},
      {(double)(float)(up_block1_conv22_params.batch_size),(double)(float)(up_block1_conv22_params.out_row_dim),(double)(float)(up_block1_conv22_params.out_col_dim),(double)(float)(up_block1_conv22_params.in_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(up_block2_conv11_params.in_row_dim),(double)(float)(up_block2_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(2),(double)(float)(1),(double)(float)(0),(double)(float)(1),(double)(float)(up_block1_conv22_params.in_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block1_conv22_params.batch_size, 
        /* in_row_dim */        up_block1_conv22_params.out_row_dim, 
        /* in_col_dim */        up_block1_conv22_params.out_col_dim, 
        /* in_channels */       up_block1_conv22_params.in_channels, 
        /* out_channels */      up_block1_conv22_params.out_channels, 
        /* out_row_dim */       up_block2_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block2_conv11_params.in_col_dim, 
        /* stride */            1,  
        /* input_dilation */    2,  
        /* kernel_dilation */   1,  
        /* padding */           0, 
        /* kernel_dim */        1, 
        false, false, false, false, false, 
        /* input */             up_block1_conv22_out_relu, 
        /* weights */           upsize_w, 
        /* bias */              z_bias, 
        /* output */            up_block2_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // nearest neighbor upsample
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {44,"up_block2_upsample","tiled_conv_dw_auto",
      {{(const void*)(up_block2_upsize),(up_block1_conv22_params.batch_size)*(up_block2_conv11_params.in_row_dim)*(up_block2_conv11_params.in_col_dim),up_block1_conv22_params.out_channels,up_block1_conv22_params.out_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block2_upsample),(up_block1_conv22_params.batch_size)*(((1)==0?(up_block2_conv11_params.in_row_dim):((up_block2_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block2_conv11_params.in_col_dim):((up_block2_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block1_conv22_params.out_channels,up_block1_conv22_params.out_channels,1},
      {(double)(float)(up_block1_conv22_params.batch_size),(double)(float)(up_block2_conv11_params.in_row_dim),(double)(float)(up_block2_conv11_params.in_col_dim),(double)(float)(up_block1_conv22_params.out_channels),(double)(float)(up_block2_conv11_params.in_row_dim),(double)(float)(up_block2_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(1),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        up_block1_conv22_params.batch_size, 
        /* in_row_dim */        up_block2_conv11_params.in_row_dim, 
        /* in_col_dim */        up_block2_conv11_params.in_col_dim, 
        /* channels */          up_block1_conv22_params.out_channels, 
        /* out_row_dim */       up_block2_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block2_conv11_params.in_col_dim, 
        /* stride */            1, 
        /* padding */           1, 
        /* kernel_dim */        2, 
        /* input */             up_block2_upsize, 
        /* weights */           upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            up_block2_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // concatenate upsampled tensor 
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {45,"up_block2_conv12_concat2_temp+32","tiled_matmul_auto",
      {{(const void*)(up_block2_upsample),2400,32,32,1},{(const void*)(identity_32),1,sizeof(identity_32)/1,sizeof(identity_32)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block2_conv12_concat2_temp+32),2400,32,96,1},
      {(double)(float)(2400),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(1),(double)(float)(96),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(2400, 32, 32, 
        up_block2_upsample, identity_32, z_bias, up_block2_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end =  read_cycles();
    conv_cycles += end - start;
    // printf("ub_2_upsample cycles: %llu \n", end - start);

    // up block 2, conv_11
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {46,"up_block2_conv11_out","tiled_matmul_auto",
      {{(const void*)(up_block2_conv12_concat2_temp),up_block2_conv11_params.I,up_block2_conv11_params.K,96,1},{(const void*)(up_block2_conv11_w),1,sizeof(up_block2_conv11_w)/1,sizeof(up_block2_conv11_w)/1,1},{(const void*)(up_block2_conv11_b),1,sizeof(up_block2_conv11_b)/4,sizeof(up_block2_conv11_b)/4,4}},{(const void*)(up_block2_conv11_out),up_block2_conv11_params.I,up_block2_conv11_params.J,up_block2_conv11_params.J,1},
      {(double)(float)(up_block2_conv11_params.I),(double)(float)(up_block2_conv11_params.J),(double)(float)(up_block2_conv11_params.K),(double)(float)(96),(double)(float)(up_block2_conv11_params.J),(double)(float)(up_block2_conv11_params.J),(double)(float)(up_block2_conv11_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block2_conv11_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block2_conv11_params.I, up_block2_conv11_params.J, up_block2_conv11_params.K,
        up_block2_conv12_concat2_temp, up_block2_conv11_w, up_block2_conv11_b, up_block2_conv11_out,
        96, up_block2_conv11_params.J, up_block2_conv11_params.J, up_block2_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block2_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_2_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 2, conv_12
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {47,"up_block2_conv12_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(up_block2_conv11_out),(up_block2_conv12_params.batch_size)*(up_block2_conv12_params.in_row_dim)*(up_block2_conv12_params.in_col_dim),up_block2_conv12_params.in_channels,up_block2_conv12_params.in_channels,1},{(const void*)(up_block2_conv12_w),1,sizeof(up_block2_conv12_w)/1,sizeof(up_block2_conv12_w)/1,1},{(const void*)(up_block2_conv12_b),1,sizeof(up_block2_conv12_b)/4,sizeof(up_block2_conv12_b)/4,4}},{(const void*)(up_block2_conv12_concat2_temp+64),(up_block2_conv12_params.batch_size)*(((up_block2_conv12_params.pool_stride)==0?(up_block2_conv12_params.out_row_dim):((up_block2_conv12_params.out_row_dim)+2*(up_block2_conv12_params.pool_padding)-(up_block2_conv12_params.pool_size))/(up_block2_conv12_params.pool_stride)+1))*(((up_block2_conv12_params.pool_stride)==0?(up_block2_conv12_params.out_col_dim):((up_block2_conv12_params.out_col_dim)+2*(up_block2_conv12_params.pool_padding)-(up_block2_conv12_params.pool_size))/(up_block2_conv12_params.pool_stride)+1)),up_block2_conv12_params.out_channels,96,1},
      {(double)(float)(up_block2_conv12_params.batch_size),(double)(float)(up_block2_conv12_params.in_row_dim),(double)(float)(up_block2_conv12_params.in_col_dim),(double)(float)(up_block2_conv12_params.in_channels),(double)(float)(up_block2_conv12_params.out_channels),(double)(float)(up_block2_conv12_params.out_row_dim),(double)(float)(up_block2_conv12_params.out_col_dim),(double)(float)(up_block2_conv12_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block2_conv12_params.padding),(double)(float)(up_block2_conv12_params.kernel_size),(double)(float)(up_block2_conv12_params.in_channels),(double)(float)(up_block2_conv12_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block2_conv12_params.output_scale),(double)(float)(up_block2_conv12_params.pool_size),(double)(float)(up_block2_conv12_params.pool_stride),(double)(float)(up_block2_conv12_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        up_block2_conv12_params.batch_size, 
        /* in_row_dim */        up_block2_conv12_params.in_row_dim, 
        /* in_col_dim */        up_block2_conv12_params.in_col_dim, 
        /* in_channels */       up_block2_conv12_params.in_channels,
        /* out_channels */      up_block2_conv12_params.out_channels, 
        /* out_row_dim */       up_block2_conv12_params.out_row_dim, 
        /* out_col_dim */       up_block2_conv12_params.out_col_dim,
        /* stride */            up_block2_conv12_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block2_conv12_params.padding, 
        /* kernel_dim */        up_block2_conv12_params.kernel_size,
        /* in_stride */         up_block2_conv12_params.in_channels, 
        /* weight_stride */     up_block2_conv12_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             up_block2_conv11_out, 
        /* weights */           up_block2_conv12_w, 
        /* bias */              up_block2_conv12_b, 
        /* output */            up_block2_conv12_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             up_block2_conv12_params.output_scale, 
        /* pool_size */         up_block2_conv12_params.pool_size, 
        /* pool_stride */       up_block2_conv12_params.pool_stride, 
        /* pool_padding */      up_block2_conv12_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_2_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block2_conv12_concat2_out)[40][60][96] = (elem_t (*)[40][60][96]) up_block2_conv12_concat2_temp;


    // up block 2, conv_21
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {48,"up_block2_conv21_out","tiled_matmul_auto",
      {{(const void*)(up_block2_conv12_concat2_out),up_block2_conv21_params.I,up_block2_conv21_params.K,up_block2_conv21_params.K,1},{(const void*)(up_block2_conv21_w),1,sizeof(up_block2_conv21_w)/1,sizeof(up_block2_conv21_w)/1,1},{(const void*)(up_block2_conv21_b),1,sizeof(up_block2_conv21_b)/4,sizeof(up_block2_conv21_b)/4,4}},{(const void*)(up_block2_conv21_out),up_block2_conv21_params.I,up_block2_conv21_params.J,up_block2_conv21_params.J,1},
      {(double)(float)(up_block2_conv21_params.I),(double)(float)(up_block2_conv21_params.J),(double)(float)(up_block2_conv21_params.K),(double)(float)(up_block2_conv21_params.K),(double)(float)(up_block2_conv21_params.J),(double)(float)(up_block2_conv21_params.J),(double)(float)(up_block2_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block2_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block2_conv21_params.I, up_block2_conv21_params.J, up_block2_conv21_params.K,
        up_block2_conv12_concat2_out, up_block2_conv21_w, up_block2_conv21_b, up_block2_conv21_out,
        up_block2_conv21_params.K, up_block2_conv21_params.J, up_block2_conv21_params.J, up_block2_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block2_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_2_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 2, conv_22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {49,"up_block2_conv22_out_relu","tiled_conv_auto",
      {{(const void*)(up_block2_conv21_out),(up_block2_conv22_params.batch_size)*(up_block2_conv22_params.in_row_dim)*(up_block2_conv22_params.in_col_dim),up_block2_conv22_params.in_channels,up_block2_conv22_params.in_channels,1},{(const void*)(up_block2_conv22_w),1,sizeof(up_block2_conv22_w)/1,sizeof(up_block2_conv22_w)/1,1},{(const void*)(up_block2_conv22_b),1,sizeof(up_block2_conv22_b)/4,sizeof(up_block2_conv22_b)/4,4}},{(const void*)(up_block2_conv22_out_relu),(up_block2_conv22_params.batch_size)*(((up_block2_conv22_params.pool_stride)==0?(up_block2_conv22_params.out_row_dim):((up_block2_conv22_params.out_row_dim)+2*(up_block2_conv22_params.pool_padding)-(up_block2_conv22_params.pool_size))/(up_block2_conv22_params.pool_stride)+1))*(((up_block2_conv22_params.pool_stride)==0?(up_block2_conv22_params.out_col_dim):((up_block2_conv22_params.out_col_dim)+2*(up_block2_conv22_params.pool_padding)-(up_block2_conv22_params.pool_size))/(up_block2_conv22_params.pool_stride)+1)),up_block2_conv22_params.out_channels,up_block2_conv22_params.out_channels,1},
      {(double)(float)(up_block2_conv22_params.batch_size),(double)(float)(up_block2_conv22_params.in_row_dim),(double)(float)(up_block2_conv22_params.in_col_dim),(double)(float)(up_block2_conv22_params.in_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(up_block2_conv22_params.out_row_dim),(double)(float)(up_block2_conv22_params.out_col_dim),(double)(float)(up_block2_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block2_conv22_params.padding),(double)(float)(up_block2_conv22_params.kernel_size),(double)(float)(up_block2_conv22_params.in_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block2_conv22_params.output_scale),(double)(float)(up_block2_conv22_params.pool_size),(double)(float)(up_block2_conv22_params.pool_stride),(double)(float)(up_block2_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block2_conv22_params.batch_size, 
        /* in_row_dim */        up_block2_conv22_params.in_row_dim, 
        /* in_col_dim */        up_block2_conv22_params.in_col_dim, 
        /* in_channels */       up_block2_conv22_params.in_channels,
        /* out_channels */      up_block2_conv22_params.out_channels, 
        /* out_row_dim */       up_block2_conv22_params.out_row_dim, 
        /* out_col_dim */       up_block2_conv22_params.out_col_dim,
        /* stride */            up_block2_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block2_conv22_params.padding, 
        /* kernel_dim */        up_block2_conv22_params.kernel_size,
        false, false, false, false, false,
        
        /* input */             up_block2_conv21_out, 
        /* weights */           up_block2_conv22_w, 
        /* bias */              up_block2_conv22_b, 
        /* output */            up_block2_conv22_out_relu,

        /* activation */        RELU, 
        /* scale */             up_block2_conv22_params.output_scale, 
        /* pool_size */         up_block2_conv22_params.pool_size, 
        /* pool_stride */       up_block2_conv22_params.pool_stride, 
        /* pool_padding */      up_block2_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_2_conv_22_concat2 cycles: %llu \n", end - start);


// up block 3

    // input dilation with 0s
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {50,"up_block3_upsize","tiled_conv_auto",
      {{(const void*)(up_block2_conv22_out_relu),(up_block2_conv22_params.batch_size)*(up_block2_conv22_params.out_row_dim)*(up_block2_conv22_params.out_col_dim),up_block2_conv22_params.in_channels,up_block2_conv22_params.in_channels,1},{(const void*)(upsize_w),1,sizeof(upsize_w)/1,sizeof(upsize_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block3_upsize),(up_block2_conv22_params.batch_size)*(((1)==0?(up_block3_conv11_params.in_row_dim):((up_block3_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block3_conv11_params.in_col_dim):((up_block3_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block2_conv22_params.out_channels,up_block2_conv22_params.out_channels,1},
      {(double)(float)(up_block2_conv22_params.batch_size),(double)(float)(up_block2_conv22_params.out_row_dim),(double)(float)(up_block2_conv22_params.out_col_dim),(double)(float)(up_block2_conv22_params.in_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(up_block3_conv11_params.in_row_dim),(double)(float)(up_block3_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(2),(double)(float)(1),(double)(float)(0),(double)(float)(1),(double)(float)(up_block2_conv22_params.in_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block2_conv22_params.batch_size, 
        /* in_row_dim */        up_block2_conv22_params.out_row_dim, 
        /* in_col_dim */        up_block2_conv22_params.out_col_dim, 
        /* in_channels */       up_block2_conv22_params.in_channels, 
        /* out_channels */      up_block2_conv22_params.out_channels, 
        /* out_row_dim */       up_block3_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block3_conv11_params.in_col_dim, 
        /* stride */            1,  
        /* input_dilation */    2,  
        /* kernel_dilation */   1,  
        /* padding */           0, 
        /* kernel_dim */        1, 
        false, false, false, false, false, 
        /* input */             up_block2_conv22_out_relu, 
        /* weights */           upsize_w, 
        /* bias */              z_bias, 
        /* output */            up_block3_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // nearest neighbor upsample
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {51,"up_block3_upsample","tiled_conv_dw_auto",
      {{(const void*)(up_block3_upsize),(up_block2_conv22_params.batch_size)*(up_block3_conv11_params.in_row_dim)*(up_block3_conv11_params.in_col_dim),up_block2_conv22_params.out_channels,up_block2_conv22_params.out_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block3_upsample),(up_block2_conv22_params.batch_size)*(((1)==0?(up_block3_conv11_params.in_row_dim):((up_block3_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block3_conv11_params.in_col_dim):((up_block3_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block2_conv22_params.out_channels,up_block2_conv22_params.out_channels,1},
      {(double)(float)(up_block2_conv22_params.batch_size),(double)(float)(up_block3_conv11_params.in_row_dim),(double)(float)(up_block3_conv11_params.in_col_dim),(double)(float)(up_block2_conv22_params.out_channels),(double)(float)(up_block3_conv11_params.in_row_dim),(double)(float)(up_block3_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(1),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        up_block2_conv22_params.batch_size, 
        /* in_row_dim */        up_block3_conv11_params.in_row_dim, 
        /* in_col_dim */        up_block3_conv11_params.in_col_dim, 
        /* channels */          up_block2_conv22_params.out_channels, 
        /* out_row_dim */       up_block3_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block3_conv11_params.in_col_dim, 
        /* stride */            1, 
        /* padding */           1, 
        /* kernel_dim */        2, 
        /* input */             up_block3_upsize, 
        /* weights */           upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            up_block3_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // concatenate upsampled tensor 
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {52,"up_block3_conv12_concat2_temp+32","tiled_matmul_auto",
      {{(const void*)(up_block3_upsample),9600,32,32,1},{(const void*)(identity_32),1,sizeof(identity_32)/1,sizeof(identity_32)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block3_conv12_concat2_temp+32),9600,32,96,1},
      {(double)(float)(9600),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(1),(double)(float)(96),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(9600, 32, 32, 
        up_block3_upsample, identity_32, z_bias, up_block3_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */



    end =  read_cycles();
    conv_cycles += end - start;
    // printf("ub_3_upsample cycles: %llu \n", end - start);

    // up block 3, conv_11
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {53,"up_block3_conv11_out","tiled_matmul_auto",
      {{(const void*)(up_block3_conv12_concat2_temp),up_block3_conv11_params.I,up_block3_conv11_params.K,96,1},{(const void*)(up_block3_conv11_w),1,sizeof(up_block3_conv11_w)/1,sizeof(up_block3_conv11_w)/1,1},{(const void*)(up_block3_conv11_b),1,sizeof(up_block3_conv11_b)/4,sizeof(up_block3_conv11_b)/4,4}},{(const void*)(up_block3_conv11_out),up_block3_conv11_params.I,up_block3_conv11_params.J,up_block3_conv11_params.J,1},
      {(double)(float)(up_block3_conv11_params.I),(double)(float)(up_block3_conv11_params.J),(double)(float)(up_block3_conv11_params.K),(double)(float)(96),(double)(float)(up_block3_conv11_params.J),(double)(float)(up_block3_conv11_params.J),(double)(float)(up_block3_conv11_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block3_conv11_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block3_conv11_params.I, up_block3_conv11_params.J, up_block3_conv11_params.K,
        up_block3_conv12_concat2_temp, up_block3_conv11_w, up_block3_conv11_b, up_block3_conv11_out,
        96, up_block3_conv11_params.J, up_block3_conv11_params.J, up_block3_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block3_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_3_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 3, conv_12
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {54,"up_block3_conv12_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(up_block3_conv11_out),(up_block3_conv12_params.batch_size)*(up_block3_conv12_params.in_row_dim)*(up_block3_conv12_params.in_col_dim),up_block3_conv12_params.in_channels,up_block3_conv12_params.in_channels,1},{(const void*)(up_block3_conv12_w),1,sizeof(up_block3_conv12_w)/1,sizeof(up_block3_conv12_w)/1,1},{(const void*)(up_block3_conv12_b),1,sizeof(up_block3_conv12_b)/4,sizeof(up_block3_conv12_b)/4,4}},{(const void*)(up_block3_conv12_concat2_temp+64),(up_block3_conv12_params.batch_size)*(((up_block3_conv12_params.pool_stride)==0?(up_block3_conv12_params.out_row_dim):((up_block3_conv12_params.out_row_dim)+2*(up_block3_conv12_params.pool_padding)-(up_block3_conv12_params.pool_size))/(up_block3_conv12_params.pool_stride)+1))*(((up_block3_conv12_params.pool_stride)==0?(up_block3_conv12_params.out_col_dim):((up_block3_conv12_params.out_col_dim)+2*(up_block3_conv12_params.pool_padding)-(up_block3_conv12_params.pool_size))/(up_block3_conv12_params.pool_stride)+1)),up_block3_conv12_params.out_channels,96,1},
      {(double)(float)(up_block3_conv12_params.batch_size),(double)(float)(up_block3_conv12_params.in_row_dim),(double)(float)(up_block3_conv12_params.in_col_dim),(double)(float)(up_block3_conv12_params.in_channels),(double)(float)(up_block3_conv12_params.out_channels),(double)(float)(up_block3_conv12_params.out_row_dim),(double)(float)(up_block3_conv12_params.out_col_dim),(double)(float)(up_block3_conv12_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block3_conv12_params.padding),(double)(float)(up_block3_conv12_params.kernel_size),(double)(float)(up_block3_conv12_params.in_channels),(double)(float)(up_block3_conv12_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block3_conv12_params.output_scale),(double)(float)(up_block3_conv12_params.pool_size),(double)(float)(up_block3_conv12_params.pool_stride),(double)(float)(up_block3_conv12_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        up_block3_conv12_params.batch_size, 
        /* in_row_dim */        up_block3_conv12_params.in_row_dim, 
        /* in_col_dim */        up_block3_conv12_params.in_col_dim, 
        /* in_channels */       up_block3_conv12_params.in_channels,
        /* out_channels */      up_block3_conv12_params.out_channels, 
        /* out_row_dim */       up_block3_conv12_params.out_row_dim, 
        /* out_col_dim */       up_block3_conv12_params.out_col_dim,
        /* stride */            up_block3_conv12_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block3_conv12_params.padding, 
        /* kernel_dim */        up_block3_conv12_params.kernel_size,
        /* in_stride */         up_block3_conv12_params.in_channels, 
        /* weight_stride */     up_block3_conv12_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             up_block3_conv11_out, 
        /* weights */           up_block3_conv12_w, 
        /* bias */              up_block3_conv12_b, 
        /* output */            up_block3_conv12_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             up_block3_conv12_params.output_scale, 
        /* pool_size */         up_block3_conv12_params.pool_size, 
        /* pool_stride */       up_block3_conv12_params.pool_stride, 
        /* pool_padding */      up_block3_conv12_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_3_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block3_conv12_concat2_out)[80][120][96] = (elem_t (*)[80][120][96]) up_block3_conv12_concat2_temp;


    // up block 3, conv_21
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {55,"up_block3_conv21_out","tiled_matmul_auto",
      {{(const void*)(up_block3_conv12_concat2_out),up_block3_conv21_params.I,up_block3_conv21_params.K,up_block3_conv21_params.K,1},{(const void*)(up_block3_conv21_w),1,sizeof(up_block3_conv21_w)/1,sizeof(up_block3_conv21_w)/1,1},{(const void*)(up_block3_conv21_b),1,sizeof(up_block3_conv21_b)/4,sizeof(up_block3_conv21_b)/4,4}},{(const void*)(up_block3_conv21_out),up_block3_conv21_params.I,up_block3_conv21_params.J,up_block3_conv21_params.J,1},
      {(double)(float)(up_block3_conv21_params.I),(double)(float)(up_block3_conv21_params.J),(double)(float)(up_block3_conv21_params.K),(double)(float)(up_block3_conv21_params.K),(double)(float)(up_block3_conv21_params.J),(double)(float)(up_block3_conv21_params.J),(double)(float)(up_block3_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block3_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block3_conv21_params.I, up_block3_conv21_params.J, up_block3_conv21_params.K,
        up_block3_conv12_concat2_out, up_block3_conv21_w, up_block3_conv21_b, up_block3_conv21_out,
        up_block3_conv21_params.K, up_block3_conv21_params.J, up_block3_conv21_params.J, up_block3_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block3_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_3_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 3, conv_22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {56,"up_block3_conv22_out_relu","tiled_conv_auto",
      {{(const void*)(up_block3_conv21_out),(up_block3_conv22_params.batch_size)*(up_block3_conv22_params.in_row_dim)*(up_block3_conv22_params.in_col_dim),up_block3_conv22_params.in_channels,up_block3_conv22_params.in_channels,1},{(const void*)(up_block3_conv22_w),1,sizeof(up_block3_conv22_w)/1,sizeof(up_block3_conv22_w)/1,1},{(const void*)(up_block3_conv22_b),1,sizeof(up_block3_conv22_b)/4,sizeof(up_block3_conv22_b)/4,4}},{(const void*)(up_block3_conv22_out_relu),(up_block3_conv22_params.batch_size)*(((up_block3_conv22_params.pool_stride)==0?(up_block3_conv22_params.out_row_dim):((up_block3_conv22_params.out_row_dim)+2*(up_block3_conv22_params.pool_padding)-(up_block3_conv22_params.pool_size))/(up_block3_conv22_params.pool_stride)+1))*(((up_block3_conv22_params.pool_stride)==0?(up_block3_conv22_params.out_col_dim):((up_block3_conv22_params.out_col_dim)+2*(up_block3_conv22_params.pool_padding)-(up_block3_conv22_params.pool_size))/(up_block3_conv22_params.pool_stride)+1)),up_block3_conv22_params.out_channels,up_block3_conv22_params.out_channels,1},
      {(double)(float)(up_block3_conv22_params.batch_size),(double)(float)(up_block3_conv22_params.in_row_dim),(double)(float)(up_block3_conv22_params.in_col_dim),(double)(float)(up_block3_conv22_params.in_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(up_block3_conv22_params.out_row_dim),(double)(float)(up_block3_conv22_params.out_col_dim),(double)(float)(up_block3_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block3_conv22_params.padding),(double)(float)(up_block3_conv22_params.kernel_size),(double)(float)(up_block3_conv22_params.in_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block3_conv22_params.output_scale),(double)(float)(up_block3_conv22_params.pool_size),(double)(float)(up_block3_conv22_params.pool_stride),(double)(float)(up_block3_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block3_conv22_params.batch_size, 
        /* in_row_dim */        up_block3_conv22_params.in_row_dim, 
        /* in_col_dim */        up_block3_conv22_params.in_col_dim, 
        /* in_channels */       up_block3_conv22_params.in_channels,
        /* out_channels */      up_block3_conv22_params.out_channels, 
        /* out_row_dim */       up_block3_conv22_params.out_row_dim, 
        /* out_col_dim */       up_block3_conv22_params.out_col_dim,
        /* stride */            up_block3_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block3_conv22_params.padding, 
        /* kernel_dim */        up_block3_conv22_params.kernel_size,
        false, false, false, false, false,
        
        /* input */             up_block3_conv21_out, 
        /* weights */           up_block3_conv22_w, 
        /* bias */              up_block3_conv22_b, 
        /* output */            up_block3_conv22_out_relu,

        /* activation */        RELU, 
        /* scale */             up_block3_conv22_params.output_scale, 
        /* pool_size */         up_block3_conv22_params.pool_size, 
        /* pool_stride */       up_block3_conv22_params.pool_stride, 
        /* pool_padding */      up_block3_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_3_conv_22_concat2 cycles: %llu \n", end - start);


// up block 4

    // input dilation with 0s
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {57,"up_block4_upsize","tiled_conv_auto",
      {{(const void*)(up_block3_conv22_out_relu),(up_block3_conv22_params.batch_size)*(up_block3_conv22_params.out_row_dim)*(up_block3_conv22_params.out_col_dim),up_block3_conv22_params.in_channels,up_block3_conv22_params.in_channels,1},{(const void*)(upsize_w),1,sizeof(upsize_w)/1,sizeof(upsize_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block4_upsize),(up_block3_conv22_params.batch_size)*(((1)==0?(up_block4_conv11_params.in_row_dim):((up_block4_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block4_conv11_params.in_col_dim):((up_block4_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block3_conv22_params.out_channels,up_block3_conv22_params.out_channels,1},
      {(double)(float)(up_block3_conv22_params.batch_size),(double)(float)(up_block3_conv22_params.out_row_dim),(double)(float)(up_block3_conv22_params.out_col_dim),(double)(float)(up_block3_conv22_params.in_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(up_block4_conv11_params.in_row_dim),(double)(float)(up_block4_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(2),(double)(float)(1),(double)(float)(0),(double)(float)(1),(double)(float)(up_block3_conv22_params.in_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block3_conv22_params.batch_size, 
        /* in_row_dim */        up_block3_conv22_params.out_row_dim, 
        /* in_col_dim */        up_block3_conv22_params.out_col_dim, 
        /* in_channels */       up_block3_conv22_params.in_channels, 
        /* out_channels */      up_block3_conv22_params.out_channels, 
        /* out_row_dim */       up_block4_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block4_conv11_params.in_col_dim, 
        /* stride */            1,  
        /* input_dilation */    2,  
        /* kernel_dilation */   1,  
        /* padding */           0, 
        /* kernel_dim */        1, 
        false, false, false, false, false, 
        /* input */             up_block3_conv22_out_relu, 
        /* weights */           upsize_w, 
        /* bias */              z_bias, 
        /* output */            up_block4_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // nearest neighbor upsample
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {58,"up_block4_upsample","tiled_conv_dw_auto",
      {{(const void*)(up_block4_upsize),(up_block3_conv22_params.batch_size)*(up_block4_conv11_params.in_row_dim)*(up_block4_conv11_params.in_col_dim),up_block3_conv22_params.out_channels,up_block3_conv22_params.out_channels,1},{(const void*)(upsample_dw_w),1,sizeof(upsample_dw_w)/1,sizeof(upsample_dw_w)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block4_upsample),(up_block3_conv22_params.batch_size)*(((1)==0?(up_block4_conv11_params.in_row_dim):((up_block4_conv11_params.in_row_dim)+2*(0)-(1))/(1)+1))*(((1)==0?(up_block4_conv11_params.in_col_dim):((up_block4_conv11_params.in_col_dim)+2*(0)-(1))/(1)+1)),up_block3_conv22_params.out_channels,up_block3_conv22_params.out_channels,1},
      {(double)(float)(up_block3_conv22_params.batch_size),(double)(float)(up_block4_conv11_params.in_row_dim),(double)(float)(up_block4_conv11_params.in_col_dim),(double)(float)(up_block3_conv22_params.out_channels),(double)(float)(up_block4_conv11_params.in_row_dim),(double)(float)(up_block4_conv11_params.in_col_dim),(double)(float)(1),(double)(float)(1),(double)(float)(2),(double)(float)(NO_ACTIVATION),(double)(float)(1),(double)(float)(1),(double)(float)(1),(double)(float)(0)},14};
      rd_begin(&rd_op);
#endif
tiled_conv_dw_auto(
        /* batch_size */        up_block3_conv22_params.batch_size, 
        /* in_row_dim */        up_block4_conv11_params.in_row_dim, 
        /* in_col_dim */        up_block4_conv11_params.in_col_dim, 
        /* channels */          up_block3_conv22_params.out_channels, 
        /* out_row_dim */       up_block4_conv11_params.in_row_dim, 
        /* out_col_dim */       up_block4_conv11_params.in_col_dim, 
        /* stride */            1, 
        /* padding */           1, 
        /* kernel_dim */        2, 
        /* input */             up_block4_upsize, 
        /* weights */           upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            up_block4_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    // concatenate upsampled tensor 
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {59,"up_block4_conv12_concat2_temp+32","tiled_matmul_auto",
      {{(const void*)(up_block4_upsample),38400,32,32,1},{(const void*)(identity_32),1,sizeof(identity_32)/1,sizeof(identity_32)/1,1},{(const void*)(z_bias),1,sizeof(z_bias)/4,sizeof(z_bias)/4,4}},{(const void*)(up_block4_conv12_concat2_temp+32),38400,32,96,1},
      {(double)(float)(38400),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(32),(double)(float)(1),(double)(float)(96),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(ACC_SCALE_IDENTITY),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(38400, 32, 32, 
        up_block4_upsample, identity_32, z_bias, up_block4_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */


    end =  read_cycles();
    conv_cycles += end - start;
    // printf("ub_4_upsample cycles: %llu \n", end - start);

    // up block 4, conv_11
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {60,"up_block4_conv11_out","tiled_matmul_auto",
      {{(const void*)(up_block4_conv12_concat2_temp),up_block4_conv11_params.I,up_block4_conv11_params.K,96,1},{(const void*)(up_block4_conv11_w),1,sizeof(up_block4_conv11_w)/1,sizeof(up_block4_conv11_w)/1,1},{(const void*)(up_block4_conv11_b),1,sizeof(up_block4_conv11_b)/4,sizeof(up_block4_conv11_b)/4,4}},{(const void*)(up_block4_conv11_out),up_block4_conv11_params.I,up_block4_conv11_params.J,up_block4_conv11_params.J,1},
      {(double)(float)(up_block4_conv11_params.I),(double)(float)(up_block4_conv11_params.J),(double)(float)(up_block4_conv11_params.K),(double)(float)(96),(double)(float)(up_block4_conv11_params.J),(double)(float)(up_block4_conv11_params.J),(double)(float)(up_block4_conv11_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block4_conv11_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block4_conv11_params.I, up_block4_conv11_params.J, up_block4_conv11_params.K,
        up_block4_conv12_concat2_temp, up_block4_conv11_w, up_block4_conv11_b, up_block4_conv11_out,
        96, up_block4_conv11_params.J, up_block4_conv11_params.J, up_block4_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block4_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_4_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 4, conv_12
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {61,"up_block4_conv12_concat2_temp+64","tiled_conv_stride_auto",
      {{(const void*)(up_block4_conv11_out),(up_block4_conv12_params.batch_size)*(up_block4_conv12_params.in_row_dim)*(up_block4_conv12_params.in_col_dim),up_block4_conv12_params.in_channels,up_block4_conv12_params.in_channels,1},{(const void*)(up_block4_conv12_w),1,sizeof(up_block4_conv12_w)/1,sizeof(up_block4_conv12_w)/1,1},{(const void*)(up_block4_conv12_b),1,sizeof(up_block4_conv12_b)/4,sizeof(up_block4_conv12_b)/4,4}},{(const void*)(up_block4_conv12_concat2_temp+64),(up_block4_conv12_params.batch_size)*(((up_block4_conv12_params.pool_stride)==0?(up_block4_conv12_params.out_row_dim):((up_block4_conv12_params.out_row_dim)+2*(up_block4_conv12_params.pool_padding)-(up_block4_conv12_params.pool_size))/(up_block4_conv12_params.pool_stride)+1))*(((up_block4_conv12_params.pool_stride)==0?(up_block4_conv12_params.out_col_dim):((up_block4_conv12_params.out_col_dim)+2*(up_block4_conv12_params.pool_padding)-(up_block4_conv12_params.pool_size))/(up_block4_conv12_params.pool_stride)+1)),up_block4_conv12_params.out_channels,96,1},
      {(double)(float)(up_block4_conv12_params.batch_size),(double)(float)(up_block4_conv12_params.in_row_dim),(double)(float)(up_block4_conv12_params.in_col_dim),(double)(float)(up_block4_conv12_params.in_channels),(double)(float)(up_block4_conv12_params.out_channels),(double)(float)(up_block4_conv12_params.out_row_dim),(double)(float)(up_block4_conv12_params.out_col_dim),(double)(float)(up_block4_conv12_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block4_conv12_params.padding),(double)(float)(up_block4_conv12_params.kernel_size),(double)(float)(up_block4_conv12_params.in_channels),(double)(float)(up_block4_conv12_params.out_channels),(double)(float)(96),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block4_conv12_params.output_scale),(double)(float)(up_block4_conv12_params.pool_size),(double)(float)(up_block4_conv12_params.pool_stride),(double)(float)(up_block4_conv12_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_stride_auto(
        /* batch_size */        up_block4_conv12_params.batch_size, 
        /* in_row_dim */        up_block4_conv12_params.in_row_dim, 
        /* in_col_dim */        up_block4_conv12_params.in_col_dim, 
        /* in_channels */       up_block4_conv12_params.in_channels,
        /* out_channels */      up_block4_conv12_params.out_channels, 
        /* out_row_dim */       up_block4_conv12_params.out_row_dim, 
        /* out_col_dim */       up_block4_conv12_params.out_col_dim,
        /* stride */            up_block4_conv12_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block4_conv12_params.padding, 
        /* kernel_dim */        up_block4_conv12_params.kernel_size,
        /* in_stride */         up_block4_conv12_params.in_channels, 
        /* weight_stride */     up_block4_conv12_params.out_channels, 
        /* out_stride */        96,
        false, false, false, false, false,
        
        /* input */             up_block4_conv11_out, 
        /* weights */           up_block4_conv12_w, 
        /* bias */              up_block4_conv12_b, 
        /* output */            up_block4_conv12_concat2_temp+64,

        /* activation */        RELU, 
        /* scale */             up_block4_conv12_params.output_scale, 
        /* pool_size */         up_block4_conv12_params.pool_size, 
        /* pool_stride */       up_block4_conv12_params.pool_stride, 
        /* pool_padding */      up_block4_conv12_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_4_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block4_conv12_concat2_out)[160][240][96] = (elem_t (*)[160][240][96]) up_block4_conv12_concat2_temp;


    // up block 4, conv_21
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {62,"up_block4_conv21_out","tiled_matmul_auto",
      {{(const void*)(up_block4_conv12_concat2_out),up_block4_conv21_params.I,up_block4_conv21_params.K,up_block4_conv21_params.K,1},{(const void*)(up_block4_conv21_w),1,sizeof(up_block4_conv21_w)/1,sizeof(up_block4_conv21_w)/1,1},{(const void*)(up_block4_conv21_b),1,sizeof(up_block4_conv21_b)/4,sizeof(up_block4_conv21_b)/4,4}},{(const void*)(up_block4_conv21_out),up_block4_conv21_params.I,up_block4_conv21_params.J,up_block4_conv21_params.J,1},
      {(double)(float)(up_block4_conv21_params.I),(double)(float)(up_block4_conv21_params.J),(double)(float)(up_block4_conv21_params.K),(double)(float)(up_block4_conv21_params.K),(double)(float)(up_block4_conv21_params.J),(double)(float)(up_block4_conv21_params.J),(double)(float)(up_block4_conv21_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(up_block4_conv21_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(up_block4_conv21_params.I, up_block4_conv21_params.J, up_block4_conv21_params.K,
        up_block4_conv12_concat2_out, up_block4_conv21_w, up_block4_conv21_b, up_block4_conv21_out,
        up_block4_conv21_params.K, up_block4_conv21_params.J, up_block4_conv21_params.J, up_block4_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block4_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("ub_4_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 4, conv_22
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {63,"up_block4_conv22_out_relu","tiled_conv_auto",
      {{(const void*)(up_block4_conv21_out),(up_block4_conv22_params.batch_size)*(up_block4_conv22_params.in_row_dim)*(up_block4_conv22_params.in_col_dim),up_block4_conv22_params.in_channels,up_block4_conv22_params.in_channels,1},{(const void*)(up_block4_conv22_w),1,sizeof(up_block4_conv22_w)/1,sizeof(up_block4_conv22_w)/1,1},{(const void*)(up_block4_conv22_b),1,sizeof(up_block4_conv22_b)/4,sizeof(up_block4_conv22_b)/4,4}},{(const void*)(up_block4_conv22_out_relu),(up_block4_conv22_params.batch_size)*(((up_block4_conv22_params.pool_stride)==0?(up_block4_conv22_params.out_row_dim):((up_block4_conv22_params.out_row_dim)+2*(up_block4_conv22_params.pool_padding)-(up_block4_conv22_params.pool_size))/(up_block4_conv22_params.pool_stride)+1))*(((up_block4_conv22_params.pool_stride)==0?(up_block4_conv22_params.out_col_dim):((up_block4_conv22_params.out_col_dim)+2*(up_block4_conv22_params.pool_padding)-(up_block4_conv22_params.pool_size))/(up_block4_conv22_params.pool_stride)+1)),up_block4_conv22_params.out_channels,up_block4_conv22_params.out_channels,1},
      {(double)(float)(up_block4_conv22_params.batch_size),(double)(float)(up_block4_conv22_params.in_row_dim),(double)(float)(up_block4_conv22_params.in_col_dim),(double)(float)(up_block4_conv22_params.in_channels),(double)(float)(up_block4_conv22_params.out_channels),(double)(float)(up_block4_conv22_params.out_row_dim),(double)(float)(up_block4_conv22_params.out_col_dim),(double)(float)(up_block4_conv22_params.stride),(double)(float)(1),(double)(float)(1),(double)(float)(up_block4_conv22_params.padding),(double)(float)(up_block4_conv22_params.kernel_size),(double)(float)(up_block4_conv22_params.in_channels),(double)(float)(up_block4_conv22_params.out_channels),(double)(float)(up_block4_conv22_params.out_channels),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(RELU),(double)(float)(up_block4_conv22_params.output_scale),(double)(float)(up_block4_conv22_params.pool_size),(double)(float)(up_block4_conv22_params.pool_stride),(double)(float)(up_block4_conv22_params.pool_padding)},25};
      rd_begin(&rd_op);
#endif
tiled_conv_auto(
        /* batch_size */        up_block4_conv22_params.batch_size, 
        /* in_row_dim */        up_block4_conv22_params.in_row_dim, 
        /* in_col_dim */        up_block4_conv22_params.in_col_dim, 
        /* in_channels */       up_block4_conv22_params.in_channels,
        /* out_channels */      up_block4_conv22_params.out_channels, 
        /* out_row_dim */       up_block4_conv22_params.out_row_dim, 
        /* out_col_dim */       up_block4_conv22_params.out_col_dim,
        /* stride */            up_block4_conv22_params.stride, 
        /* input_dilation */    1, 
        /* kernel_dilation */   1, 
        /* padding */           up_block4_conv22_params.padding, 
        /* kernel_dim */        up_block4_conv22_params.kernel_size,
        false, false, false, false, false,
        
        /* input */             up_block4_conv21_out, 
        /* weights */           up_block4_conv22_w, 
        /* bias */              up_block4_conv22_b, 
        /* output */            up_block4_conv22_out_relu,

        /* activation */        RELU, 
        /* scale */             up_block4_conv22_params.output_scale, 
        /* pool_size */         up_block4_conv22_params.pool_size, 
        /* pool_stride */       up_block4_conv22_params.pool_stride, 
        /* pool_padding */      up_block4_conv22_params.pool_padding,
        tiled_matmul_type);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end = read_cycles();
    conv_cycles += end - start;
    // printf("ub_4_conv_22_concat2 cycles: %llu \n", end - start);


// out 

    // out block
    ritnet_stage(++stage); start = read_cycles();
    
/* RITNET_DIAG_BEGIN */
#ifdef RITNET_DIAGNOSTICS
    { const struct rd_operation rd_op = {64,"out","tiled_matmul_auto",
      {{(const void*)(up_block4_conv22_out_relu),out_conv_params.I,out_conv_params.K,out_conv_params.K,1},{(const void*)(out_conv1_w),1,sizeof(out_conv1_w)/1,sizeof(out_conv1_w)/1,1},{(const void*)(out_conv1_b),1,sizeof(out_conv1_b)/4,sizeof(out_conv1_b)/4,4}},{(const void*)(out),out_conv_params.I,out_conv_params.J,out_conv_params.J,1},
      {(double)(float)(out_conv_params.I),(double)(float)(out_conv_params.J),(double)(float)(out_conv_params.K),(double)(float)(out_conv_params.K),(double)(float)(out_conv_params.J),(double)(float)(out_conv_params.J),(double)(float)(out_conv_params.J),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(MVIN_SCALE_IDENTITY),(double)(float)(NO_ACTIVATION),(double)(float)(out_conv_params.output_scale),(double)(float)(0),(double)(float)(true),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(false),(double)(float)(0)},19};
      rd_begin(&rd_op);
#endif
tiled_matmul_auto(out_conv_params.I, out_conv_params.J, out_conv_params.K,
        up_block4_conv22_out_relu, out_conv1_w, out_conv1_b, out,
        out_conv_params.K, out_conv_params.J, out_conv_params.J, out_conv_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, out_conv_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        RITNET_EXECUTION_TYPE);
#ifdef RITNET_DIAGNOSTICS
      if(rd_end()) return -3;
    }
#endif
/* RITNET_DIAG_END */

    end =  read_cycles();
    matmul_cycles += end - start;
    // printf("out (matmul) cycles: %llu \n", end - start);



    gemmini_fence();
    if(ritnet_workspace_check()) return -2;
#ifdef RITNET_DIAGNOSTICS
    rd_inference_end();
#endif
    return ritnet_decode(&out[0][0][0][0],result);
}

size_t ritnet_workspace_size(void) { return sizeof(down_block1_concat1_temp_guard) + sizeof(down_block1_conv21_out_guard) + sizeof(down_block1_concat2_temp_guard) + sizeof(down_block1_conv31_out_guard) + sizeof(down_block1_conv32_out_relu_guard) + sizeof(down_block1_conv32_avg_pool_guard) + sizeof(down_block1_conv32_avg_pool_2_guard) + sizeof(down_block1_conv32_avg_pool_3_guard) + sizeof(down_block2_concat1_temp_guard) + sizeof(down_block2_conv21_out_guard) + sizeof(down_block2_concat2_temp_guard) + sizeof(down_block2_conv31_out_guard) + sizeof(down_block3_conv21_out_guard) + sizeof(down_block3_conv22_concat2_temp_guard) + sizeof(down_block3_conv31_out_guard) + sizeof(down_block4_conv21_out_guard) + sizeof(down_block4_conv22_concat2_temp_guard) + sizeof(down_block4_conv31_out_guard) + sizeof(down_block5_conv21_out_guard) + sizeof(down_block5_conv22_concat2_temp_guard) + sizeof(down_block5_conv31_out_guard) + sizeof(down_block5_conv32_out_relu_guard) + sizeof(up_block1_upsize_guard) + sizeof(up_block1_upsample_guard) + sizeof(tester_guard) + sizeof(up_block1_conv11_out_guard) + sizeof(up_block1_conv12_concat2_temp_guard) + sizeof(up_block1_conv21_out_guard) + sizeof(up_block1_conv22_out_relu_guard) + sizeof(up_block2_upsize_guard) + sizeof(up_block2_upsample_guard) + sizeof(up_block2_conv11_out_guard) + sizeof(up_block2_conv12_concat2_temp_guard) + sizeof(up_block2_conv21_out_guard) + sizeof(up_block2_conv22_out_relu_guard) + sizeof(up_block3_upsize_guard) + sizeof(up_block3_upsample_guard) + sizeof(up_block3_conv11_out_guard) + sizeof(up_block3_conv12_concat2_temp_guard) + sizeof(up_block3_conv21_out_guard) + sizeof(up_block3_conv22_out_relu_guard) + sizeof(up_block4_upsize_guard) + sizeof(up_block4_upsample_guard) + sizeof(up_block4_concat1_temp_guard) + sizeof(up_block4_conv11_out_guard) + sizeof(up_block4_conv12_concat2_temp_guard) + sizeof(up_block4_conv21_out_guard) + sizeof(up_block4_conv22_out_relu_guard) + sizeof(out_guard); }
