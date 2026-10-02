#include "include/gemmini.h"
#include "include/ritnet_helpers.h"
#include "include/ritnet_params.h"
#include "include/ritnet_weights.h"

void gemmini_inference(elem_t * images, float eye_x, float eye_y) {
    
    printf("Hello we have started gemmini inference\n");

    gemmini_flush(0);
    enum tiled_matmul_type_t tiled_matmul_type = WS;

    uint64_t start, end;
    uint64_t im2col_cycles = 0, matmul_cycles = 0, conv_cycles = 0, pool_cycles = 0, conv_dw_cycles = 0, res_add_cycles = 0, other_cycles = 0;

    // down block 1
    
    // copying image into column 0 of concat1, applying the same quantization factor as conv1 so that they can be concatenated
    start = read_cycles();
    tiled_matmul_auto(38400, 1, 1, 
        images, (elem_t*) identity_kernel, NULL, down_block1_concat1_temp,
        1, 1, 0, 65,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, db1_conv1_x_scale/db1_conv1_y_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();

    printf("image copy cycles: %llu \n", end - start);
    
    // conv 1
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block1_conv1_w, 
        /* bias */              down_block1_conv1_b,
        /* output */            (elem_t*) down_block1_concat1_temp+1,

        /* activation */        RELU_ACC, 
        /* scale */             down_block1_conv1_params.output_scale, 
        /* pool_size */         down_block1_conv1_params.pool_size, 
        /* pool_stride */       down_block1_conv1_params.pool_stride, 
        /* pool_padding */      down_block1_conv1_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    printf("db_1_conv1 cycles: %llu \n", end - start);


    // conv 21 uses concat1
    start = read_cycles();
    tiled_matmul_auto(down_block1_conv21_params.I, down_block1_conv21_params.J, down_block1_conv21_params.K,
        down_block1_concat1_temp, (elem_t*) down_block1_conv21_w, down_block1_conv21_b, down_block1_conv21_out,
        65, down_block1_conv21_params.J, down_block1_conv21_params.J, down_block1_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block1_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_1_conv_21 (matmul) cycles: %llu \n", end - start); 

    // conv 22
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block1_conv22_w, 
        /* bias */              down_block1_conv22_b, 
        /* output */            (elem_t*) down_block1_concat2_temp+33,

        /* activation */        RELU_ACC, 
        /* scale */             down_block1_conv22_params.output_scale, 
        /* pool_size */         down_block1_conv22_params.pool_size, 
        /* pool_stride */       down_block1_conv22_params.pool_stride, 
        /* pool_padding */      down_block1_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_1_conv_22 cycles: %llu \n", end - start);


    // concat 2
    start = read_cycles();

    // add concat 1 (uses columns 0->32) to concat 2 (uses 33->65)
    tiled_resadd_auto(38400, 65,
        /* A_scale*/ db1_conv1_y_scale/db1_conv22_y_scale, // dequantize from conv1 and requantize to conv22
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        down_block1_concat1_temp, 
        down_block1_concat2_temp,
        down_block1_concat2_temp,
        false,
        tiled_matmul_type);

    // write the input quantized with conv22 scale into concat2
    tiled_matmul_auto(38400, 1, 1, 
        images, (elem_t*) identity_kernel, NULL, down_block1_concat2_temp,
        1, 1, 1, 65,
        /* A_scale */ 1.0/db1_conv22_y_scale, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true, // quantize to conv22
        false, false,
        false, false,
        0,
        WS);

    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_1_concat2 cycles: %llu \n", end - start);

    elem_t (*down_block1_concat2_out)[160][240][65] = (elem_t (*)[160][240][65]) down_block1_concat2_temp;


    // down block 1, conv_31
    start = read_cycles();
    tiled_matmul_auto(down_block1_conv31_params.I, down_block1_conv31_params.J, down_block1_conv31_params.K,
        (elem_t*) down_block1_concat2_out, (elem_t*) down_block1_conv31_w, down_block1_conv31_b, down_block1_conv31_out,
        down_block1_conv31_params.K, down_block1_conv31_params.J, down_block1_conv31_params.J, down_block1_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block1_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("db_1_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 1, conv_32 relu 
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block1_conv32_params.output_scale,  
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block1_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_1_conv_32_relu cycles: %llu \n", end - start);


    // average pooling
    start = read_cycles();
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              NULL, 
        /* output */            (elem_t*) down_block1_conv32_avg_pool, 
        /* activation */ NO_ACTIVATION, /* scale */ db1_conv32_y_scale*0.25/db2_conv1_x_scale, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
    
    // average pooling again to write into the concat 1 of the next block (for some reason I can't just dequantize the first one)
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              NULL, 
        /* output */            (elem_t*) down_block1_conv32_avg_pool_2, 
        /* activation */ NO_ACTIVATION, /* scale */ db1_conv32_y_scale*0.25/db2_conv21_x_scale, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);
    
    
    end = read_cycles();
    pool_cycles += end - start;
    printf("db_1_avg_pool cycles: %llu \n", end - start);



// down block 2


    // copying the output of conv32 into the first 32 channels of concat1
    tiled_matmul_auto(down_block2_conv1_params.I, down_block2_conv1_params.J, down_block2_conv1_params.K,
        down_block1_conv32_avg_pool_2, (elem_t*) identity_32, NULL, down_block2_concat1_temp,
        down_block2_conv1_params.K, down_block2_conv1_params.J, down_block2_conv1_params.J, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
        NO_ACTIVATION, 1.0, 0, true,
        false, false,
        false, false,
        0,
        WS);


    // down block 2, conv_1
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block2_conv1_w, 
        /* bias */              down_block2_conv1_b, 
        /* output */            (elem_t*) down_block2_concat1_temp+32,

        /* activation */        RELU_ACC, 
        /* scale */             down_block2_conv1_params.output_scale, 
        /* pool_size */         down_block2_conv1_params.pool_size, 
        /* pool_stride */       down_block2_conv1_params.pool_stride, 
        /* pool_padding */      down_block2_conv1_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_2_conv1 cycles: %llu \n", end - start);

    // conv 21
    start = read_cycles();
    tiled_matmul_auto(down_block2_conv21_params.I, down_block2_conv21_params.J, down_block2_conv21_params.K,
        down_block2_concat1_temp, (elem_t*) down_block2_conv21_w, down_block2_conv21_b, down_block2_conv21_out,
        96, down_block2_conv21_params.J, down_block2_conv21_params.J, down_block2_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block2_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_2_conv_21 (matmul) cycles: %llu \n", end - start);

    // conv 22
    start = read_cycles();
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
        
        /* input */             (elem_t*) down_block2_conv21_out, 
        /* weights */           (elem_t*) down_block2_conv22_w, 
        /* bias */              down_block2_conv22_b, 
        /* output */            (elem_t*) down_block2_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             down_block2_conv22_params.output_scale, 
        /* pool_size */         down_block2_conv22_params.pool_size, 
        /* pool_stride */       down_block2_conv22_params.pool_stride, 
        /* pool_padding */      down_block2_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_2_conv_22 cycles: %llu \n", end - start);


    // concat 2
    start = read_cycles();
    tiled_resadd_auto(38400, 65,
        /* A_scale*/ db2_conv1_y_scale/db2_conv22_y_scale, // dequantize from conv1 and requantize to conv22
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        down_block2_concat1_temp, 
        down_block2_concat2_temp,
        down_block2_concat2_temp,
        false,
        tiled_matmul_type);
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
    //     WS);
    // end = read_cycles();
    // matmul_cycles += end - start;
    // // printf("db_2_concat2 cycles: %llu \n", end - start);

    // elem_t (*layer_test)[80][120][96] = (elem_t (*)[80][120][96]) down_block2_concat2_temp;    
    // for (int i = 0; i < 80; i++) {
    //     for (int j = 0; j < 120; j++) { 
    //         for (int k = 0; k < 96; k++) { 
    //             if (layer_test[0][i][j][k] != test[0][i][j][k]) {
    //                 // printf("mismatch at: i=%d, j=%d, k=%d: %" PRId8 " vs %" PRId8 "\n", i, j, k, layer_test[0][i][j][k], test[0][i][j][k]); 
    //             }
    //         }  
    //     }  
    // } 
    // // printf("matched layer\n"); 
    // return 0; 


    elem_t (*down_block2_concat2_out)[80][120][96] = (elem_t (*)[80][120][96]) down_block2_concat2_temp;


    // down block 2, conv_31
    start = read_cycles();
    tiled_matmul_auto(down_block2_conv31_params.I, down_block2_conv31_params.J, down_block2_conv31_params.K,
        (elem_t*) down_block2_concat2_out, (elem_t*) down_block2_conv31_w, down_block2_conv31_b, down_block2_conv31_out,
        down_block2_conv31_params.K, down_block2_conv31_params.J, down_block2_conv31_params.J, down_block2_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block2_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("db_2_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 2, conv_32 relu (for concat later)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block2_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block2_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_2_conv_32_relu cycles: %llu \n", end - start);


    // down block 2, conv_32 (for relu and pool)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block2_conv32_params.output_scale,
        /* pool_size */     down_block2_conv32_params.pool_size, 
        /* pool_stride */   down_block2_conv32_params.pool_stride, 
        /* pool_padding */  down_block2_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    // printf("db_2_conv_32_relu_pool cycles: %llu \n", end - start);
    
// down block 3

    // down block 3, conv_1
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block3_conv1_w, 
        /* bias */              down_block3_conv1_b, 
        /* output */            (elem_t*) down_block3_conv22_concat2_temp+32,

        /* activation */        RELU_ACC, 
        /* scale */             down_block3_conv1_params.output_scale, 
        /* pool_size */         down_block3_conv1_params.pool_size, 
        /* pool_stride */       down_block3_conv1_params.pool_stride, 
        /* pool_padding */      down_block3_conv1_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_3_conv1_concat1 cycles: %llu \n", end - start);


    start = read_cycles();
    tiled_matmul_auto(down_block3_conv21_params.I, down_block3_conv21_params.J, down_block3_conv21_params.K,
        down_block3_conv22_concat2_temp, (elem_t*) down_block3_conv21_w, down_block3_conv21_b, down_block3_conv21_out,
        96, down_block3_conv21_params.J, down_block3_conv21_params.J, down_block3_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block3_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_3_conv_21 (matmul) cycles: %llu \n", end - start);

    start = read_cycles();
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
        
        /* input */             (elem_t*) down_block3_conv21_out, 
        /* weights */           (elem_t*) down_block3_conv22_w, 
        /* bias */              down_block3_conv22_b, 
        /* output */            (elem_t*) down_block3_conv22_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             down_block2_conv22_params.output_scale, 
        /* pool_size */         down_block2_conv22_params.pool_size, 
        /* pool_stride */       down_block2_conv22_params.pool_stride, 
        /* pool_padding */      down_block2_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_3_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block3_conv22_concat2_out)[40][60][96] = (elem_t (*)[40][60][96]) down_block3_conv22_concat2_temp;


    // down block 3, conv_31
    start = read_cycles();
    tiled_matmul_auto(down_block3_conv31_params.I, down_block3_conv31_params.J, down_block3_conv31_params.K,
        (elem_t*) down_block3_conv22_concat2_out, (elem_t*) down_block3_conv31_w, down_block3_conv31_b, down_block3_conv31_out,
        down_block3_conv31_params.K, down_block3_conv31_params.J, down_block3_conv31_params.J, down_block3_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block3_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("db_3_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 3, conv_32 relu (for concat later)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block3_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block3_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_3_conv_32_relu cycles: %llu \n", end - start);


    // down block 3, conv_32 (for relu and pool)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block3_conv32_params.output_scale,
        /* pool_size */     down_block3_conv32_params.pool_size, 
        /* pool_stride */   down_block3_conv32_params.pool_stride, 
        /* pool_padding */  down_block3_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_3_conv_32_relu_pool cycles: %llu \n", end - start);
    

// down block 4

    // down block 4, conv_1
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block4_conv1_w, 
        /* bias */              down_block4_conv1_b, 
        /* output */            (elem_t*) down_block4_conv22_concat2_temp+32,

        /* activation */        RELU_ACC, 
        /* scale */             down_block4_conv1_params.output_scale, 
        /* pool_size */         down_block4_conv1_params.pool_size, 
        /* pool_stride */       down_block4_conv1_params.pool_stride, 
        /* pool_padding */      down_block4_conv1_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_4_conv1_concat1 cycles: %llu \n", end - start);


    start = read_cycles();
    tiled_matmul_auto(down_block4_conv21_params.I, down_block4_conv21_params.J, down_block4_conv21_params.K,
        down_block4_conv22_concat2_temp, (elem_t*) down_block4_conv21_w, down_block4_conv21_b, down_block4_conv21_out,
        96, down_block4_conv21_params.J, down_block4_conv21_params.J, down_block4_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block4_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_4_conv_21 (matmul) cycles: %llu \n", end - start);

    start = read_cycles();
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
        
        /* input */             (elem_t*) down_block4_conv21_out, 
        /* weights */           (elem_t*) down_block4_conv22_w, 
        /* bias */              down_block4_conv22_b, 
        /* output */            (elem_t*) down_block4_conv22_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             down_block4_conv22_params.output_scale, 
        /* pool_size */         down_block4_conv22_params.pool_size, 
        /* pool_stride */       down_block4_conv22_params.pool_stride, 
        /* pool_padding */      down_block4_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_4_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block4_conv22_concat2_out)[20][30][96] = (elem_t (*)[20][30][96]) down_block4_conv22_concat2_temp;


    // down block 4, conv_31
    start = read_cycles();
    tiled_matmul_auto(down_block4_conv31_params.I, down_block4_conv31_params.J, down_block4_conv31_params.K,
        (elem_t*) down_block4_conv22_concat2_out, (elem_t*) down_block4_conv31_w, down_block4_conv31_b, down_block4_conv31_out,
        down_block4_conv31_params.K, down_block4_conv31_params.J, down_block4_conv31_params.J, down_block4_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block4_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("db_4_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 4, conv_32 relu (for concat later)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block4_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block4_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_4_conv_32_relu cycles: %llu \n", end - start);


    // down block 4, conv_32 (for relu and pool)
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block4_conv32_params.output_scale,
        /* pool_size */     down_block4_conv32_params.pool_size, 
        /* pool_stride */   down_block4_conv32_params.pool_stride, 
        /* pool_padding */  down_block4_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_4_conv_32_relu_pool cycles: %llu \n", end - start);
    

// down block 5

    // down block 5, conv_1
    start = read_cycles();
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
        /* weights */           (elem_t*) down_block5_conv1_w, 
        /* bias */              down_block5_conv1_b, 
        /* output */            (elem_t*) down_block5_conv22_concat2_temp+32,

        /* activation */        RELU_ACC, 
        /* scale */             down_block5_conv1_params.output_scale, 
        /* pool_size */         down_block5_conv1_params.pool_size, 
        /* pool_stride */       down_block5_conv1_params.pool_stride, 
        /* pool_padding */      down_block5_conv1_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_5_conv1_concat1 cycles: %llu \n", end - start);


    start = read_cycles();
    tiled_matmul_auto(down_block5_conv21_params.I, down_block5_conv21_params.J, down_block5_conv21_params.K,
        down_block5_conv22_concat2_temp, (elem_t*) down_block5_conv21_w, down_block5_conv21_b, down_block5_conv21_out,
        96, down_block5_conv21_params.J, down_block5_conv21_params.J, down_block5_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block5_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end = read_cycles();
    matmul_cycles += end - start;
    printf("db_5_conv_21 (matmul) cycles: %llu \n", end - start);

    start = read_cycles();
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
        
        /* input */             (elem_t*) down_block5_conv21_out, 
        /* weights */           (elem_t*) down_block5_conv22_w, 
        /* bias */              down_block5_conv22_b, 
        /* output */            (elem_t*) down_block5_conv22_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             down_block5_conv22_params.output_scale, 
        /* pool_size */         down_block5_conv22_params.pool_size, 
        /* pool_stride */       down_block5_conv22_params.pool_stride, 
        /* pool_padding */      down_block5_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("db_5_conv_22_concat2 cycles: %llu \n", end - start);


    elem_t (*down_block5_conv22_concat2_out)[10][15][96] = (elem_t (*)[10][15][96]) down_block5_conv22_concat2_temp;


    // down block 5, conv_31
    start = read_cycles();
    tiled_matmul_auto(down_block5_conv31_params.I, down_block5_conv31_params.J, down_block5_conv31_params.K,
        (elem_t*) down_block5_conv22_concat2_out, (elem_t*) down_block5_conv31_w, down_block5_conv31_b, down_block5_conv31_out,
        down_block5_conv31_params.K, down_block5_conv31_params.J, down_block5_conv31_params.J, down_block5_conv31_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, down_block5_conv31_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("db_5_conv_31 (matmul) cycles: %llu \n", end - start);


    // down block 5, conv_32 relu 
    start = read_cycles();
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

        /* activation */    RELU_ACC, 
        /* scale */         down_block5_conv32_params.output_scale,
        /* pool_size */     1, 
        /* pool_stride */   1, 
        /* pool_padding */  down_block5_conv32_params.pool_padding,

        tiled_matmul_type);

    end = read_cycles();
    conv_cycles += end - start;
    printf("db_5_conv_32_relu cycles: %llu \n", end - start);

    
// up block 1

    // resize down block 5, conv_32 relu from 10x15 up to 20x30 using nearest neighbor interpolation

    // input dilation with 0s
    start = read_cycles();
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
        /* input */             (elem_t*) down_block5_conv32_out_relu, 
        /* weights */           (elem_t*) upsize_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block1_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // nearest neighbor upsample
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block1_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // concatenate upsampled tensor 
    tiled_matmul_auto(600, 32, 32, 
        up_block1_upsample, (elem_t*) identity_32, z_bias, up_block1_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        WS); 

    end =  read_cycles();
    conv_cycles += end - start;
    printf("ub_1_upsample cycles: %llu \n", end - start);


    // up block 1, conv_11
    start = read_cycles();
    tiled_matmul_auto(up_block1_conv11_params.I, up_block1_conv11_params.J, up_block1_conv11_params.K,
        up_block1_conv12_concat2_temp, (elem_t*) up_block1_conv11_w, up_block1_conv11_b, up_block1_conv11_out,
        96, up_block1_conv11_params.J, up_block1_conv11_params.J, up_block1_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block1_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_1_conv_11 (matmul) cycles: %llu \n", end - start);


    // up block 1, conv_12
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block1_conv11_out, 
        /* weights */           (elem_t*) up_block1_conv12_w, 
        /* bias */              up_block1_conv12_b, 
        /* output */            (elem_t*) up_block1_conv12_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             up_block1_conv12_params.output_scale, 
        /* pool_size */         up_block1_conv12_params.pool_size, 
        /* pool_stride */       up_block1_conv12_params.pool_stride, 
        /* pool_padding */      up_block1_conv12_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_1_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block1_conv12_concat2_out)[20][30][96] = (elem_t (*)[20][30][96]) up_block1_conv12_concat2_temp;

    
    
    // up block 1, conv_21
    start = read_cycles();
    tiled_matmul_auto(up_block1_conv21_params.I, up_block1_conv21_params.J, up_block1_conv21_params.K,
        (elem_t*) up_block1_conv12_concat2_out, (elem_t*) up_block1_conv21_w, up_block1_conv21_b, up_block1_conv21_out,
        up_block1_conv21_params.K, up_block1_conv21_params.J, up_block1_conv21_params.J, up_block1_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block1_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_1_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 1, conv_22
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block1_conv21_out, 
        /* weights */           (elem_t*) up_block1_conv22_w, 
        /* bias */              up_block1_conv22_b, 
        /* output */            (elem_t*) up_block1_conv22_out_relu,

        /* activation */        RELU_ACC, 
        /* scale */             up_block1_conv22_params.output_scale, 
        /* pool_size */         up_block1_conv22_params.pool_size, 
        /* pool_stride */       up_block1_conv22_params.pool_stride, 
        /* pool_padding */      up_block1_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_1_conv_22_concat2 cycles: %llu \n", end - start);


// up block 2

    // input dilation with 0s
    start = read_cycles();
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
        /* input */             (elem_t*) up_block1_conv22_out_relu, 
        /* weights */           (elem_t*) upsize_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block2_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // nearest neighbor upsample
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block2_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // concatenate upsampled tensor 
    tiled_matmul_auto(2400, 32, 32, 
        up_block2_upsample, (elem_t*) identity_32, z_bias, up_block2_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        WS);

    end =  read_cycles();
    conv_cycles += end - start;
    printf("ub_2_upsample cycles: %llu \n", end - start);

    // up block 2, conv_11
    start = read_cycles();
    tiled_matmul_auto(up_block2_conv11_params.I, up_block2_conv11_params.J, up_block2_conv11_params.K,
        up_block2_conv12_concat2_temp, (elem_t*) up_block2_conv11_w, up_block2_conv11_b, up_block2_conv11_out,
        96, up_block2_conv11_params.J, up_block2_conv11_params.J, up_block2_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block2_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_2_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 2, conv_12
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block2_conv11_out, 
        /* weights */           (elem_t*) up_block2_conv12_w, 
        /* bias */              up_block2_conv12_b, 
        /* output */            (elem_t*) up_block2_conv12_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             up_block2_conv12_params.output_scale, 
        /* pool_size */         up_block2_conv12_params.pool_size, 
        /* pool_stride */       up_block2_conv12_params.pool_stride, 
        /* pool_padding */      up_block2_conv12_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_2_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block2_conv12_concat2_out)[40][60][96] = (elem_t (*)[40][60][96]) up_block2_conv12_concat2_temp;


    // up block 2, conv_21
    start = read_cycles();
    tiled_matmul_auto(up_block2_conv21_params.I, up_block2_conv21_params.J, up_block2_conv21_params.K,
        (elem_t*) up_block2_conv12_concat2_out, (elem_t*) up_block2_conv21_w, up_block2_conv21_b, up_block2_conv21_out,
        up_block2_conv21_params.K, up_block2_conv21_params.J, up_block2_conv21_params.J, up_block2_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block2_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_2_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 2, conv_22
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block2_conv21_out, 
        /* weights */           (elem_t*) up_block2_conv22_w, 
        /* bias */              up_block2_conv22_b, 
        /* output */           (elem_t*) up_block2_conv22_out_relu,

        /* activation */        RELU_ACC, 
        /* scale */             up_block2_conv22_params.output_scale, 
        /* pool_size */         up_block2_conv22_params.pool_size, 
        /* pool_stride */       up_block2_conv22_params.pool_stride, 
        /* pool_padding */      up_block2_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_2_conv_22_concat2 cycles: %llu \n", end - start);


    // up block 3

    // input dilation with 0s
    start = read_cycles();
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
        /* input */             (elem_t*) up_block2_conv22_out_relu, 
        /* weights */           (elem_t*) upsize_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block3_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // nearest neighbor upsample
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block3_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // concatenate upsampled tensor 
    tiled_matmul_auto(9600, 32, 32, 
        up_block3_upsample, (elem_t*) identity_32, z_bias, up_block3_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        WS);


    end =  read_cycles();
    conv_cycles += end - start;
    printf("ub_3_upsample cycles: %llu \n", end - start);

    // up block 3, conv_11
    start = read_cycles();
    tiled_matmul_auto(up_block3_conv11_params.I, up_block3_conv11_params.J, up_block3_conv11_params.K,
        up_block3_conv12_concat2_temp, (elem_t*) up_block3_conv11_w, up_block3_conv11_b, up_block3_conv11_out,
        96, up_block3_conv11_params.J, up_block3_conv11_params.J, up_block3_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block3_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_3_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 3, conv_12
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block3_conv11_out, 
        /* weights */           (elem_t*) up_block3_conv12_w, 
        /* bias */              up_block3_conv12_b, 
        /* output */            (elem_t*) up_block3_conv12_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             up_block3_conv12_params.output_scale, 
        /* pool_size */         up_block3_conv12_params.pool_size, 
        /* pool_stride */       up_block3_conv12_params.pool_stride, 
        /* pool_padding */      up_block3_conv12_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_3_conv_12_concat2 cycles: %llu \n", end - start);


    elem_t (*up_block3_conv12_concat2_out)[80][120][96] = (elem_t (*)[80][120][96]) up_block3_conv12_concat2_temp;


    // up block 3, conv_21
    start = read_cycles();
    tiled_matmul_auto(up_block3_conv21_params.I, up_block3_conv21_params.J, up_block3_conv21_params.K,
        (elem_t*) up_block3_conv12_concat2_out, (elem_t*) up_block3_conv21_w, up_block3_conv21_b, up_block3_conv21_out,
        up_block3_conv21_params.K, up_block3_conv21_params.J, up_block3_conv21_params.J, up_block3_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block3_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_3_conv_21 (matmul) cycles: %llu \n", end - start);


    // up block 3, conv_22
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block3_conv21_out, 
        /* weights */           (elem_t*) up_block3_conv22_w, 
        /* bias */              up_block3_conv22_b, 
        /* output */            (elem_t*) up_block3_conv22_out_relu,

        /* activation */        RELU_ACC, 
        /* scale */             up_block3_conv22_params.output_scale, 
        /* pool_size */         up_block3_conv22_params.pool_size, 
        /* pool_stride */       up_block3_conv22_params.pool_stride, 
        /* pool_padding */      up_block3_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_3_conv_22_concat2 cycles: %llu \n", end - start);


// up block 4

    // input dilation with 0s
    start = read_cycles();
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
        /* input */             (elem_t*) up_block3_conv22_out_relu, 
        /* weights */           (elem_t*) upsize_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block4_upsize, 
        /* activation */        NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // nearest neighbor upsample
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
        /* weights */           (elem_t*) upsample_dw_w, 
        /* bias */              z_bias, 
        /* output */            (elem_t*) up_block4_upsample, 
        /* activation */ NO_ACTIVATION, /* scale */ 1, /* pool_size */ 1, /* pool_stride */ 1, /* pool_padding */ 0, tiled_matmul_type);

    // concatenate upsampled tensor 
    tiled_matmul_auto(38400, 32, 32, 
        up_block4_upsample, (elem_t*) identity_32, z_bias, up_block4_conv12_concat2_temp+32,
        32, 32, 1, 96,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
        false, false,
        false, false,
        0,
        WS);

    end =  read_cycles();
    conv_cycles += end - start;
    printf("ub_4_upsample cycles: %llu \n", end - start);

    // up block 4, conv_11
    start = read_cycles();
    tiled_matmul_auto(up_block4_conv11_params.I, up_block4_conv11_params.J, up_block4_conv11_params.K,
        up_block4_conv12_concat2_temp, (elem_t*) up_block4_conv11_w, up_block4_conv11_b, up_block4_conv11_out,
        96, up_block4_conv11_params.J, up_block4_conv11_params.J, up_block4_conv11_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block4_conv11_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_4_conv_11 (matmul) cycles: %llu \n", end - start);

    // up block 4, conv_12
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block4_conv11_out, 
        /* weights */           (elem_t*) up_block4_conv12_w, 
        /* bias */              up_block4_conv12_b, 
        /* output */            (elem_t*) up_block4_conv12_concat2_temp+64,

        /* activation */        RELU_ACC, 
        /* scale */             up_block4_conv12_params.output_scale, 
        /* pool_size */         up_block4_conv12_params.pool_size, 
        /* pool_stride */       up_block4_conv12_params.pool_stride, 
        /* pool_padding */      up_block4_conv12_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_4_conv_12_concat2 cycles: %llu \n", end - start);
    
    
    elem_t (*up_block4_conv12_concat2_out)[160][240][96] = (elem_t (*)[160][240][96]) up_block4_conv12_concat2_temp;
    
    
    // up block 4, conv_21
    start = read_cycles();
    tiled_matmul_auto(up_block4_conv21_params.I, up_block4_conv21_params.J, up_block4_conv21_params.K,
        (elem_t*) up_block4_conv12_concat2_out, (elem_t*) up_block4_conv21_w, up_block4_conv21_b, up_block4_conv21_out,
        up_block4_conv21_params.K, up_block4_conv21_params.J, up_block4_conv21_params.J, up_block4_conv21_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, up_block4_conv21_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("ub_4_conv_21 (matmul) cycles: %llu \n", end - start);
    
    
    // up block 4, conv_22
    start = read_cycles();
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
        
        /* input */             (elem_t*) up_block4_conv21_out, 
        /* weights */           (elem_t*) up_block4_conv22_w, 
        /* bias */              up_block4_conv22_b, 
        /* output */            (elem_t*) up_block4_conv22_out_relu,

        /* activation */        RELU_ACC, 
        /* scale */             up_block4_conv22_params.output_scale, 
        /* pool_size */         up_block4_conv22_params.pool_size, 
        /* pool_stride */       up_block4_conv22_params.pool_stride, 
        /* pool_padding */      up_block4_conv22_params.pool_padding,
        tiled_matmul_type);
    end = read_cycles();
    conv_cycles += end - start;
    printf("ub_4_conv_22_concat2 cycles: %llu \n", end - start);
    
    
    // out 
    
    // out block
    start = read_cycles();
    tiled_matmul_auto(out_conv_params.I, out_conv_params.J, out_conv_params.K,
        (elem_t*) up_block4_conv22_out_relu, (elem_t*) out_conv1_w, out_conv1_b, out,
        out_conv_params.K, out_conv_params.J, out_conv_params.J, out_conv_params.J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION, out_conv_params.output_scale, 0, true,
        false, false,
        false, false,
        0,
        WS);
    end =  read_cycles();
    matmul_cycles += end - start;
    printf("out (matmul) cycles: %llu \n", end - start);
    
    
    // int indices[160][240];
    // for (int h = 0; h < 160; h++) {
    //     for (int w = 0; w < 240; w++) {
    //         int max_idx = 0;
    //         elem_t max_val = out_test[0][h][w][0];
    //         for (int c = 1; c < 4; c++) {
    //             if (out_test[0][h][w][c] > max_val) {
    //                 max_val = out_test[0][h][w][c];
    //                 max_idx = c;
    //             }
    //         }
    //         indices[h][w] = max_idx;
    //     }
    // }
    
    // for (int i = 0; i < 160; i++) { 
    //     for (int j = 0; j < 240; j++) {
    //         printf("%d ", indices[i][j]);
    //     }
    //     printf("\n");
    // }
    
    
    // double weighted_sum_y = 0;
    // double weighted_sum_x = 0;
    // double total_weight = 0;

    // for (int y = 0; y < 160; y++) {
    //     for (int x = 0; x < 240; x++) {
    //         int value = indices[y][x];
    //         weighted_sum_y += y * value;
    //         weighted_sum_x += x * value;
    //         total_weight += value;
    //     }
    // }

    // float com_x, com_y;
    // if (total_weight == 0) {
    //     com_y = -1.0f;  // Undefined, or no mass
    //     com_x = -1.0f; 
    // } else {
    //     com_y = (float)weighted_sum_y / total_weight;
    //     com_x = (float)weighted_sum_x / total_weight;
    // }
    // // printf("Fovea: %f, %f\n", com_y, com_x);


    uint64_t total_cycles = im2col_cycles + matmul_cycles + pool_cycles + conv_cycles + conv_dw_cycles + res_add_cycles + other_cycles;

    printf("\nTotal cycles: %llu (100%%)\n", total_cycles);
    printf("Matmul cycles: %llu (%d%%)\n", matmul_cycles, (matmul_cycles * 100) / total_cycles);
    printf("Im2col cycles: %llu (%d%%)\n", im2col_cycles, (im2col_cycles * 100) / total_cycles);
    printf("Conv cycles: %llu (%d%%)\n", conv_cycles, (conv_cycles * 100) / total_cycles);
    printf("Pooling cycles: %llu (%d%%)\n", pool_cycles, (pool_cycles * 100) / total_cycles);
    printf("Depthwise convolution cycles: %llu (%d%%)\n", conv_dw_cycles, (conv_dw_cycles * 100) / total_cycles);
    printf("Res add cycles: %llu (%d%%)\n", res_add_cycles, (res_add_cycles * 100) / total_cycles);
    printf("Other cycles: %llu (%d%%)\n", other_cycles, (other_cycles * 100) / total_cycles);



    // printf("PASS\n");
}
    
