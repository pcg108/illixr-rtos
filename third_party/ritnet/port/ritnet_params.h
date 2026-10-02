#ifndef RITNET_PARAMETERS_H
#define RITNET_PARAMETERS_H

#include "include/gemmini_params.h"
#include <stdbool.h>


/////  down block 1 //////

/* Quantization parameters */
const acc_scale_t db1_conv1_x_scale = 1.0;
const acc_scale_t db1_conv1_w_scale = 0.005637120921164751;
const acc_scale_t db1_conv1_y_scale = 1.343885898590088;

const acc_scale_t db1_conv21_x_scale = 1.343885898590088;
const acc_scale_t db1_conv21_w_scale = 0.009015585295855999;
const acc_scale_t db1_conv21_y_scale = 2.810380458831787;

const acc_scale_t db1_conv22_x_scale = 2.810380458831787;
const acc_scale_t db1_conv22_w_scale = 0.008665069006383419;
const acc_scale_t db1_conv22_y_scale = 9.384881019592285;

const acc_scale_t db1_conv31_x_scale = 9.384881019592285;
const acc_scale_t db1_conv31_w_scale = 0.008936820551753044;
const acc_scale_t db1_conv31_y_scale = 9.975286483764648;

const acc_scale_t db1_conv32_x_scale = 9.975286483764648;
const acc_scale_t db1_conv32_w_scale = 0.00004176458969595842;
const acc_scale_t db1_conv32_y_scale = 0.1016097292304039;

static const struct ConvParams down_block1_conv1_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=3, .in_channels=1, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db1_conv1_x_scale*db1_conv1_w_scale/db1_conv1_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0}; // 

static struct { uint64_t before[8]; elem_t data[2496000]; uint64_t after[8]; } down_block1_concat1_temp_guard row_align(4);
#define down_block1_concat1_temp down_block1_concat1_temp_guard.data // 160x240x33 ... but making it 65 to add to concat2 later

static struct { uint64_t before[8]; elem_t data[1228800]; uint64_t after[8]; } down_block1_conv21_out_guard row_align(4);
#define down_block1_conv21_out down_block1_conv21_out_guard.data
static const struct ConvParams down_block1_conv21_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=1, .in_channels=33, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db1_conv21_x_scale*db1_conv21_w_scale/db1_conv21_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=38400, .J=32, .K=33};

static const struct ConvParams down_block1_conv22_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db1_conv22_x_scale*db1_conv22_w_scale/db1_conv22_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[2496000]; uint64_t after[8]; } down_block1_concat2_temp_guard row_align(4);
#define down_block1_concat2_temp down_block1_concat2_temp_guard.data // 160x240x65

static struct { uint64_t before[8]; elem_t data[1228800]; uint64_t after[8]; } down_block1_conv31_out_guard row_align(4);
#define down_block1_conv31_out down_block1_conv31_out_guard.data
static const struct ConvParams down_block1_conv31_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=1, .in_channels=65, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db1_conv31_x_scale*db1_conv31_w_scale/db1_conv31_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=38400, .J=32, .K=65};

static const struct ConvParams down_block1_conv32_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=80, .out_col_dim=120, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db1_conv32_x_scale*db1_conv32_w_scale/db1_conv32_y_scale, .pool_size=2, .pool_stride=2, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1228800]; uint64_t after[8]; } down_block1_conv32_out_relu_guard row_align(4);
#define down_block1_conv32_out_relu down_block1_conv32_out_relu_guard.data
static struct { uint64_t before[8]; elem_t data[307200]; uint64_t after[8]; } down_block1_conv32_avg_pool_guard row_align(4);
#define down_block1_conv32_avg_pool down_block1_conv32_avg_pool_guard.data
static struct { uint64_t before[8]; elem_t data[307200]; uint64_t after[8]; } down_block1_conv32_avg_pool_2_guard row_align(4);
#define down_block1_conv32_avg_pool_2 down_block1_conv32_avg_pool_2_guard.data
static struct { uint64_t before[8]; elem_t data[307200]; uint64_t after[8]; } down_block1_conv32_avg_pool_3_guard row_align(4);
#define down_block1_conv32_avg_pool_3 down_block1_conv32_avg_pool_3_guard.data

/////  down block 2 //////

const acc_scale_t db2_conv1_x_scale = 0.0450330451130867;
const acc_scale_t db2_conv1_w_scale = 0.014643686823546886;
const acc_scale_t db2_conv1_y_scale = 0.11783212423324585;

const acc_scale_t db2_conv21_x_scale = 0.11783212423324585;
const acc_scale_t db2_conv21_w_scale = 0.007549534551799297;
const acc_scale_t db2_conv21_y_scale = 0.14814910292625427;

const acc_scale_t db2_conv22_x_scale = 0.14814910292625427;
const acc_scale_t db2_conv22_w_scale = 0.008846118114888668;
const acc_scale_t db2_conv22_y_scale = 0.313230961561203;

const acc_scale_t db2_conv31_x_scale = 0.313230961561203;
const acc_scale_t db2_conv31_w_scale = 0.010525722056627274;
const acc_scale_t db2_conv31_y_scale = 0.32839348912239075;

const acc_scale_t db2_conv32_x_scale = 0;
const acc_scale_t db2_conv32_w_scale = 0;
const acc_scale_t db2_conv32_y_scale = 0;

static const struct ConvParams down_block2_conv1_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db2_conv1_x_scale*db2_conv1_w_scale/db2_conv1_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=9600, .J=32, .K=32};

static struct { uint64_t before[8]; elem_t data[921600]; uint64_t after[8]; } down_block2_concat1_temp_guard row_align(4);
#define down_block2_concat1_temp down_block2_concat1_temp_guard.data // 80x120x32 ... but making it 96 to add to concat2 later

static struct { uint64_t before[8]; elem_t data[1][80][120][32]; uint64_t after[8]; } down_block2_conv21_out_guard row_align(4);
#define down_block2_conv21_out down_block2_conv21_out_guard.data
static const struct ConvParams down_block2_conv21_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=1, .in_channels=64, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db2_conv21_x_scale*db2_conv21_w_scale/db2_conv21_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=9600, .J=32, .K=64};

static const struct ConvParams down_block2_conv22_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= db2_conv22_x_scale*db2_conv22_w_scale/db2_conv22_y_scale, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=9600, .J=32, .K=32};

static struct { uint64_t before[8]; elem_t data[921600]; uint64_t after[8]; } down_block2_concat2_temp_guard row_align(4);
#define down_block2_concat2_temp down_block2_concat2_temp_guard.data // 80x120x96

static struct { uint64_t before[8]; elem_t data[1][80][120][32]; uint64_t after[8]; } down_block2_conv31_out_guard row_align(4);
#define down_block2_conv31_out down_block2_conv31_out_guard.data
static const struct ConvParams down_block2_conv31_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0,.n_patches=0, .patch_size=0, .output_scale= 0.009044143371284008, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=9600, .J=32, .K=96};

static const struct ConvParams down_block2_conv32_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120,  .out_row_dim=40, .out_col_dim=60, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0006656512268818915, .pool_size=2, .pool_stride=2, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

/////  down block 3 //////

static const struct ConvParams down_block3_conv1_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.007413163781166077, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0,.I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][40][60][32]; uint64_t after[8]; } down_block3_conv21_out_guard row_align(4);
#define down_block3_conv21_out down_block3_conv21_out_guard.data
static const struct ConvParams down_block3_conv21_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=3, .in_channels=64, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.007439598441123962, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=2400, .J=32, .K=64};

static const struct ConvParams down_block3_conv22_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=1, .in_channels=32, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.00491486769169569, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[230400]; uint64_t after[8]; } down_block3_conv22_concat2_temp_guard row_align(4);
#define down_block3_conv22_concat2_temp down_block3_conv22_concat2_temp_guard.data // 40x60x96

static struct { uint64_t before[8]; elem_t data[1][40][60][32]; uint64_t after[8]; } down_block3_conv31_out_guard row_align(4);
#define down_block3_conv31_out down_block3_conv31_out_guard.data
static const struct ConvParams down_block3_conv31_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.007977241650223732, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=2400, .J=32, .K=96};

static const struct ConvParams down_block3_conv32_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=20, .out_col_dim=30, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0008915129001252353, .pool_size=2, .pool_stride=2, .pool_padding=0, .out_dim_pooled=0,  .I=0, .J=0, .K=0};

/////  down block 4 //////

static const struct ConvParams down_block4_conv1_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.006781130563467741, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][20][30][32]; uint64_t after[8]; } down_block4_conv21_out_guard row_align(4);
#define down_block4_conv21_out down_block4_conv21_out_guard.data
static const struct ConvParams down_block4_conv21_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=3, .in_channels=64, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.008193261921405792 , .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=600, .J=32, .K=64};

static const struct ConvParams down_block4_conv22_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=1, .in_channels=32, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005357084795832634, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[57600]; uint64_t after[8]; } down_block4_conv22_concat2_temp_guard row_align(4);
#define down_block4_conv22_concat2_temp down_block4_conv22_concat2_temp_guard.data // 20x30x96

static struct { uint64_t before[8]; elem_t data[1][20][30][32]; uint64_t after[8]; } down_block4_conv31_out_guard row_align(4);
#define down_block4_conv31_out down_block4_conv31_out_guard.data
static const struct ConvParams down_block4_conv31_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.008536163717508316, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=600, .J=32, .K=96};

static const struct ConvParams down_block4_conv32_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=10, .out_col_dim=15, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0005973001825623214, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

/////  down block 5 //////

static const struct ConvParams down_block5_conv1_params = {.batch_size=1, .in_row_dim=10, .in_col_dim=15, .out_row_dim=10, .out_col_dim=15, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0072044488042593, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};


static struct { uint64_t before[8]; elem_t data[1][10][15][32]; uint64_t after[8]; } down_block5_conv21_out_guard row_align(4);
#define down_block5_conv21_out down_block5_conv21_out_guard.data
static const struct ConvParams down_block5_conv21_params = {.batch_size=1, .in_row_dim=10, .in_col_dim=15, .out_row_dim=10, .out_col_dim=15, .kernel_size=3, .in_channels=64, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0,.n_patches=0, .patch_size=0, .output_scale= 0.007998989894986153, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=150, .J=32, .K=64};

static const struct ConvParams down_block5_conv22_params = {.batch_size=1, .in_row_dim=10, .in_col_dim=15, .out_row_dim=10, .out_col_dim=15, .kernel_size=1, .in_channels=32, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005667577031999826, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[14400]; uint64_t after[8]; } down_block5_conv22_concat2_temp_guard row_align(4);
#define down_block5_conv22_concat2_temp down_block5_conv22_concat2_temp_guard.data // 10x15x96

static struct { uint64_t before[8]; elem_t data[1][10][15][32]; uint64_t after[8]; } down_block5_conv31_out_guard row_align(4);
#define down_block5_conv31_out down_block5_conv31_out_guard.data
static const struct ConvParams down_block5_conv31_params = {.batch_size=1, .in_row_dim=10, .in_col_dim=15, .out_row_dim=10, .out_col_dim=15, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.008167427033185959, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=150, .J=32, .K=96};

static struct { uint64_t before[8]; elem_t data[1][10][15][32]; uint64_t after[8]; } down_block5_conv32_out_relu_guard row_align(4);
#define down_block5_conv32_out_relu down_block5_conv32_out_relu_guard.data
static const struct ConvParams down_block5_conv32_params = {.batch_size=1, .in_row_dim=10, .in_col_dim=15, .out_row_dim=10, .out_col_dim=15, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0004809283127542585, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};


///// up block 1 //////

static struct { uint64_t before[8]; elem_t data[19200]; uint64_t after[8]; } up_block1_upsize_guard row_align(4);
#define up_block1_upsize up_block1_upsize_guard.data // 20x30x32
static struct { uint64_t before[8]; elem_t data[19200]; uint64_t after[8]; } up_block1_upsample_guard row_align(4);
#define up_block1_upsample up_block1_upsample_guard.data

static struct { uint64_t before[8]; elem_t data[19200]; uint64_t after[8]; } tester_guard row_align(4);
#define tester tester_guard.data

static const struct ConvParams up_block1_conv11_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=1, .in_channels=64, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.009320328943431377, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=600, .J=32, .K=64};

static struct { uint64_t before[8]; elem_t data[1][20][30][32]; uint64_t after[8]; } up_block1_conv11_out_guard row_align(4);
#define up_block1_conv11_out up_block1_conv11_out_guard.data

static const struct ConvParams up_block1_conv12_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.006091064307838678, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[57600]; uint64_t after[8]; } up_block1_conv12_concat2_temp_guard row_align(4);
#define up_block1_conv12_concat2_temp up_block1_conv12_concat2_temp_guard.data // 96x20x30

static const struct ConvParams up_block1_conv21_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.008485074155032635, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=600, .J=32, .K=96};

static struct { uint64_t before[8]; elem_t data[1][20][30][32]; uint64_t after[8]; } up_block1_conv21_out_guard row_align(4);
#define up_block1_conv21_out up_block1_conv21_out_guard.data

static const struct ConvParams up_block1_conv22_params = {.batch_size=1, .in_row_dim=20, .in_col_dim=30, .out_row_dim=20, .out_col_dim=30, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005706024821847677, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][20][30][32]; uint64_t after[8]; } up_block1_conv22_out_relu_guard row_align(4);
#define up_block1_conv22_out_relu up_block1_conv22_out_relu_guard.data

///// up block 2 //////

static struct { uint64_t before[8]; elem_t data[76800]; uint64_t after[8]; } up_block2_upsize_guard row_align(4);
#define up_block2_upsize up_block2_upsize_guard.data // 40x60x32
static struct { uint64_t before[8]; elem_t data[76800]; uint64_t after[8]; } up_block2_upsample_guard row_align(4);
#define up_block2_upsample up_block2_upsample_guard.data

static const struct ConvParams up_block2_conv11_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=1, .in_channels=64, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.00958976149559021, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=2400, .J=32, .K=64};

static struct { uint64_t before[8]; elem_t data[1][40][60][32]; uint64_t after[8]; } up_block2_conv11_out_guard row_align(4);
#define up_block2_conv11_out up_block2_conv11_out_guard.data

static const struct ConvParams up_block2_conv12_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.004849789198487997, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[230400]; uint64_t after[8]; } up_block2_conv12_concat2_temp_guard row_align(4);
#define up_block2_conv12_concat2_temp up_block2_conv12_concat2_temp_guard.data // 96x40x60

static const struct ConvParams up_block2_conv21_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.007872502319514751, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=2400, .J=32, .K=96};

static struct { uint64_t before[8]; elem_t data[1][40][60][32]; uint64_t after[8]; } up_block2_conv21_out_guard row_align(4);
#define up_block2_conv21_out up_block2_conv21_out_guard.data

static const struct ConvParams up_block2_conv22_params = {.batch_size=1, .in_row_dim=40, .in_col_dim=60, .out_row_dim=40, .out_col_dim=60, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.009555966593325138, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][40][60][32]; uint64_t after[8]; } up_block2_conv22_out_relu_guard row_align(4);
#define up_block2_conv22_out_relu up_block2_conv22_out_relu_guard.data


///// up block 3 //////

static struct { uint64_t before[8]; elem_t data[307200]; uint64_t after[8]; } up_block3_upsize_guard row_align(4);
#define up_block3_upsize up_block3_upsize_guard.data // 80x120x32
static struct { uint64_t before[8]; elem_t data[307200]; uint64_t after[8]; } up_block3_upsample_guard row_align(4);
#define up_block3_upsample up_block3_upsample_guard.data

static const struct ConvParams up_block3_conv11_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=1, .in_channels=64, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.007717613596469164, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=9600, .J=32, .K=64};

static struct { uint64_t before[8]; elem_t data[1][80][120][32]; uint64_t after[8]; } up_block3_conv11_out_guard row_align(4);
#define up_block3_conv11_out up_block3_conv11_out_guard.data

static const struct ConvParams up_block3_conv12_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0065065003000199795, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[921600]; uint64_t after[8]; } up_block3_conv12_concat2_temp_guard row_align(4);
#define up_block3_conv12_concat2_temp up_block3_conv12_concat2_temp_guard.data // 96x80x120

static const struct ConvParams up_block3_conv21_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.009617337025702, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0,.I=9600, .J=32, .K=96};

static struct { uint64_t before[8]; elem_t data[1][80][120][32]; uint64_t after[8]; } up_block3_conv21_out_guard row_align(4);
#define up_block3_conv21_out up_block3_conv21_out_guard.data

static const struct ConvParams up_block3_conv22_params = {.batch_size=1, .in_row_dim=80, .in_col_dim=120, .out_row_dim=80, .out_col_dim=120, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005706360563635826, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][80][120][32]; uint64_t after[8]; } up_block3_conv22_out_relu_guard row_align(4);
#define up_block3_conv22_out_relu up_block3_conv22_out_relu_guard.data


///// up block 4 //////

static struct { uint64_t before[8]; elem_t data[1228800]; uint64_t after[8]; } up_block4_upsize_guard row_align(4);
#define up_block4_upsize up_block4_upsize_guard.data // 160x240x32
static struct { uint64_t before[8]; elem_t data[1228800]; uint64_t after[8]; } up_block4_upsample_guard row_align(4);
#define up_block4_upsample up_block4_upsample_guard.data

static struct { uint64_t before[8]; elem_t data[2457600]; uint64_t after[8]; } up_block4_concat1_temp_guard row_align(4);
#define up_block4_concat1_temp up_block4_concat1_temp_guard.data // 64x160x240

static const struct ConvParams up_block4_conv11_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=1, .in_channels=64, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.00866206455975771, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=38400, .J=32, .K=64};

static struct { uint64_t before[8]; elem_t data[1][160][240][32]; uint64_t after[8]; } up_block4_conv11_out_guard row_align(4);
#define up_block4_conv11_out up_block4_conv11_out_guard.data

static const struct ConvParams up_block4_conv12_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005745391361415386, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0,.I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[3686400]; uint64_t after[8]; } up_block4_conv12_concat2_temp_guard row_align(4);
#define up_block4_conv12_concat2_temp up_block4_conv12_concat2_temp_guard.data // 96x160x240

static const struct ConvParams up_block4_conv21_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=1, .in_channels=96, .out_channels=32, .stride=1, .padding=0, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.0068373903632164, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=38400, .J=32, .K=96};

static struct { uint64_t before[8]; elem_t data[1][160][240][32]; uint64_t after[8]; } up_block4_conv21_out_guard row_align(4);
#define up_block4_conv21_out up_block4_conv21_out_guard.data

static const struct ConvParams up_block4_conv22_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=3, .in_channels=32, .out_channels=32, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.005886749364435673, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=0, .J=0, .K=0};

static struct { uint64_t before[8]; elem_t data[1][160][240][32]; uint64_t after[8]; } up_block4_conv22_out_relu_guard row_align(4);
#define up_block4_conv22_out_relu up_block4_conv22_out_relu_guard.data


// out block 

static const struct ConvParams out_conv_params = {.batch_size=1, .in_row_dim=160, .in_col_dim=240, .out_row_dim=160, .out_col_dim=240, .kernel_size=1, .in_channels=32, .out_channels=4, .stride=1, .padding=1, .bias=0, .depthwise=0, .n_patches=0, .patch_size=0, .output_scale= 0.011369828134775162/11.642108917236328, .pool_size=1, .pool_stride=1, .pool_padding=0, .out_dim_pooled=0, .I=38400, .J=4, .K=32};

static struct { uint64_t before[8]; elem_t data[1][160][240][4]; uint64_t after[8]; } out_guard row_align(4);
#define out out_guard.data

#endif


static void ritnet_workspace_reset(void) {
 memset(down_block1_concat1_temp,0,sizeof(down_block1_concat1_temp));
 for(unsigned i=0;i<8;i++) down_block1_concat1_temp_guard.before[i]=down_block1_concat1_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv21_out,0,sizeof(down_block1_conv21_out));
 for(unsigned i=0;i<8;i++) down_block1_conv21_out_guard.before[i]=down_block1_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_concat2_temp,0,sizeof(down_block1_concat2_temp));
 for(unsigned i=0;i<8;i++) down_block1_concat2_temp_guard.before[i]=down_block1_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv31_out,0,sizeof(down_block1_conv31_out));
 for(unsigned i=0;i<8;i++) down_block1_conv31_out_guard.before[i]=down_block1_conv31_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv32_out_relu,0,sizeof(down_block1_conv32_out_relu));
 for(unsigned i=0;i<8;i++) down_block1_conv32_out_relu_guard.before[i]=down_block1_conv32_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv32_avg_pool,0,sizeof(down_block1_conv32_avg_pool));
 for(unsigned i=0;i<8;i++) down_block1_conv32_avg_pool_guard.before[i]=down_block1_conv32_avg_pool_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv32_avg_pool_2,0,sizeof(down_block1_conv32_avg_pool_2));
 for(unsigned i=0;i<8;i++) down_block1_conv32_avg_pool_2_guard.before[i]=down_block1_conv32_avg_pool_2_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block1_conv32_avg_pool_3,0,sizeof(down_block1_conv32_avg_pool_3));
 for(unsigned i=0;i<8;i++) down_block1_conv32_avg_pool_3_guard.before[i]=down_block1_conv32_avg_pool_3_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block2_concat1_temp,0,sizeof(down_block2_concat1_temp));
 for(unsigned i=0;i<8;i++) down_block2_concat1_temp_guard.before[i]=down_block2_concat1_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block2_conv21_out,0,sizeof(down_block2_conv21_out));
 for(unsigned i=0;i<8;i++) down_block2_conv21_out_guard.before[i]=down_block2_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block2_concat2_temp,0,sizeof(down_block2_concat2_temp));
 for(unsigned i=0;i<8;i++) down_block2_concat2_temp_guard.before[i]=down_block2_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block2_conv31_out,0,sizeof(down_block2_conv31_out));
 for(unsigned i=0;i<8;i++) down_block2_conv31_out_guard.before[i]=down_block2_conv31_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block3_conv21_out,0,sizeof(down_block3_conv21_out));
 for(unsigned i=0;i<8;i++) down_block3_conv21_out_guard.before[i]=down_block3_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block3_conv22_concat2_temp,0,sizeof(down_block3_conv22_concat2_temp));
 for(unsigned i=0;i<8;i++) down_block3_conv22_concat2_temp_guard.before[i]=down_block3_conv22_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block3_conv31_out,0,sizeof(down_block3_conv31_out));
 for(unsigned i=0;i<8;i++) down_block3_conv31_out_guard.before[i]=down_block3_conv31_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block4_conv21_out,0,sizeof(down_block4_conv21_out));
 for(unsigned i=0;i<8;i++) down_block4_conv21_out_guard.before[i]=down_block4_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block4_conv22_concat2_temp,0,sizeof(down_block4_conv22_concat2_temp));
 for(unsigned i=0;i<8;i++) down_block4_conv22_concat2_temp_guard.before[i]=down_block4_conv22_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block4_conv31_out,0,sizeof(down_block4_conv31_out));
 for(unsigned i=0;i<8;i++) down_block4_conv31_out_guard.before[i]=down_block4_conv31_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block5_conv21_out,0,sizeof(down_block5_conv21_out));
 for(unsigned i=0;i<8;i++) down_block5_conv21_out_guard.before[i]=down_block5_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block5_conv22_concat2_temp,0,sizeof(down_block5_conv22_concat2_temp));
 for(unsigned i=0;i<8;i++) down_block5_conv22_concat2_temp_guard.before[i]=down_block5_conv22_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block5_conv31_out,0,sizeof(down_block5_conv31_out));
 for(unsigned i=0;i<8;i++) down_block5_conv31_out_guard.before[i]=down_block5_conv31_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(down_block5_conv32_out_relu,0,sizeof(down_block5_conv32_out_relu));
 for(unsigned i=0;i<8;i++) down_block5_conv32_out_relu_guard.before[i]=down_block5_conv32_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_upsize,0,sizeof(up_block1_upsize));
 for(unsigned i=0;i<8;i++) up_block1_upsize_guard.before[i]=up_block1_upsize_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_upsample,0,sizeof(up_block1_upsample));
 for(unsigned i=0;i<8;i++) up_block1_upsample_guard.before[i]=up_block1_upsample_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(tester,0,sizeof(tester));
 for(unsigned i=0;i<8;i++) tester_guard.before[i]=tester_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_conv11_out,0,sizeof(up_block1_conv11_out));
 for(unsigned i=0;i<8;i++) up_block1_conv11_out_guard.before[i]=up_block1_conv11_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_conv12_concat2_temp,0,sizeof(up_block1_conv12_concat2_temp));
 for(unsigned i=0;i<8;i++) up_block1_conv12_concat2_temp_guard.before[i]=up_block1_conv12_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_conv21_out,0,sizeof(up_block1_conv21_out));
 for(unsigned i=0;i<8;i++) up_block1_conv21_out_guard.before[i]=up_block1_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block1_conv22_out_relu,0,sizeof(up_block1_conv22_out_relu));
 for(unsigned i=0;i<8;i++) up_block1_conv22_out_relu_guard.before[i]=up_block1_conv22_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_upsize,0,sizeof(up_block2_upsize));
 for(unsigned i=0;i<8;i++) up_block2_upsize_guard.before[i]=up_block2_upsize_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_upsample,0,sizeof(up_block2_upsample));
 for(unsigned i=0;i<8;i++) up_block2_upsample_guard.before[i]=up_block2_upsample_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_conv11_out,0,sizeof(up_block2_conv11_out));
 for(unsigned i=0;i<8;i++) up_block2_conv11_out_guard.before[i]=up_block2_conv11_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_conv12_concat2_temp,0,sizeof(up_block2_conv12_concat2_temp));
 for(unsigned i=0;i<8;i++) up_block2_conv12_concat2_temp_guard.before[i]=up_block2_conv12_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_conv21_out,0,sizeof(up_block2_conv21_out));
 for(unsigned i=0;i<8;i++) up_block2_conv21_out_guard.before[i]=up_block2_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block2_conv22_out_relu,0,sizeof(up_block2_conv22_out_relu));
 for(unsigned i=0;i<8;i++) up_block2_conv22_out_relu_guard.before[i]=up_block2_conv22_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_upsize,0,sizeof(up_block3_upsize));
 for(unsigned i=0;i<8;i++) up_block3_upsize_guard.before[i]=up_block3_upsize_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_upsample,0,sizeof(up_block3_upsample));
 for(unsigned i=0;i<8;i++) up_block3_upsample_guard.before[i]=up_block3_upsample_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_conv11_out,0,sizeof(up_block3_conv11_out));
 for(unsigned i=0;i<8;i++) up_block3_conv11_out_guard.before[i]=up_block3_conv11_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_conv12_concat2_temp,0,sizeof(up_block3_conv12_concat2_temp));
 for(unsigned i=0;i<8;i++) up_block3_conv12_concat2_temp_guard.before[i]=up_block3_conv12_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_conv21_out,0,sizeof(up_block3_conv21_out));
 for(unsigned i=0;i<8;i++) up_block3_conv21_out_guard.before[i]=up_block3_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block3_conv22_out_relu,0,sizeof(up_block3_conv22_out_relu));
 for(unsigned i=0;i<8;i++) up_block3_conv22_out_relu_guard.before[i]=up_block3_conv22_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_upsize,0,sizeof(up_block4_upsize));
 for(unsigned i=0;i<8;i++) up_block4_upsize_guard.before[i]=up_block4_upsize_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_upsample,0,sizeof(up_block4_upsample));
 for(unsigned i=0;i<8;i++) up_block4_upsample_guard.before[i]=up_block4_upsample_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_concat1_temp,0,sizeof(up_block4_concat1_temp));
 for(unsigned i=0;i<8;i++) up_block4_concat1_temp_guard.before[i]=up_block4_concat1_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_conv11_out,0,sizeof(up_block4_conv11_out));
 for(unsigned i=0;i<8;i++) up_block4_conv11_out_guard.before[i]=up_block4_conv11_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_conv12_concat2_temp,0,sizeof(up_block4_conv12_concat2_temp));
 for(unsigned i=0;i<8;i++) up_block4_conv12_concat2_temp_guard.before[i]=up_block4_conv12_concat2_temp_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_conv21_out,0,sizeof(up_block4_conv21_out));
 for(unsigned i=0;i<8;i++) up_block4_conv21_out_guard.before[i]=up_block4_conv21_out_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(up_block4_conv22_out_relu,0,sizeof(up_block4_conv22_out_relu));
 for(unsigned i=0;i<8;i++) up_block4_conv22_out_relu_guard.before[i]=up_block4_conv22_out_relu_guard.after[i]=UINT64_C(0xabcddcba98766789);
 memset(out,0,sizeof(out));
 for(unsigned i=0;i<8;i++) out_guard.before[i]=out_guard.after[i]=UINT64_C(0xabcddcba98766789);
}
static int ritnet_workspace_check(void) {
 for(unsigned i=0;i<8;i++) if(down_block1_concat1_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_concat1_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv31_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv31_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv32_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv32_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv32_avg_pool_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv32_avg_pool_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv32_avg_pool_2_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv32_avg_pool_2_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block1_conv32_avg_pool_3_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block1_conv32_avg_pool_3_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block2_concat1_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block2_concat1_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block2_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block2_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block2_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block2_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block2_conv31_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block2_conv31_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block3_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block3_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block3_conv22_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block3_conv22_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block3_conv31_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block3_conv31_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block4_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block4_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block4_conv22_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block4_conv22_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block4_conv31_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block4_conv31_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block5_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block5_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block5_conv22_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block5_conv22_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block5_conv31_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block5_conv31_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(down_block5_conv32_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || down_block5_conv32_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_upsize_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_upsize_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_upsample_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_upsample_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(tester_guard.before[i]!=UINT64_C(0xabcddcba98766789) || tester_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_conv11_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_conv11_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_conv12_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_conv12_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block1_conv22_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block1_conv22_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_upsize_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_upsize_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_upsample_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_upsample_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_conv11_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_conv11_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_conv12_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_conv12_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block2_conv22_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block2_conv22_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_upsize_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_upsize_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_upsample_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_upsample_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_conv11_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_conv11_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_conv12_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_conv12_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block3_conv22_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block3_conv22_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_upsize_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_upsize_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_upsample_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_upsample_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_concat1_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_concat1_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_conv11_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_conv11_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_conv12_concat2_temp_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_conv12_concat2_temp_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_conv21_out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_conv21_out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(up_block4_conv22_out_relu_guard.before[i]!=UINT64_C(0xabcddcba98766789) || up_block4_conv22_out_relu_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 for(unsigned i=0;i<8;i++) if(out_guard.before[i]!=UINT64_C(0xabcddcba98766789) || out_guard.after[i]!=UINT64_C(0xabcddcba98766789)) return -1;
 return 0;
}
