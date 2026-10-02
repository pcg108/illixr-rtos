#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ritnet_result { double x,y; uint64_t output_hash; uint32_t foreground; int valid; };
/* Serialized entry: caller owns the singleton workspace through completion. */
int ritnet_infer(const int8_t *input, struct ritnet_result *result);
int ritnet_decode(const int8_t *tensor, struct ritnet_result *result);
size_t ritnet_workspace_size(void);
const int8_t *ritnet_sample(void);
const int8_t *ritnet_output(void);
void ritnet_stage(unsigned stage);
#ifdef __cplusplus
}
#endif
