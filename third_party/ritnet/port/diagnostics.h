#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RD_OPERATIONS 64
#define RD_MAX_INFERENCES 32
#define RD_CAPTURE_BYTES (4u*1024u*1024u)
/* 0 off, 1 metadata, 2 selected completion drains, 3 completed tensor checks. */
extern volatile uint32_t ritnet_diag_mode;
extern volatile uint32_t ritnet_diag_inferences;
extern volatile uint64_t ritnet_diag_drain_mask;
extern volatile uint32_t ritnet_diag_capture_operation;
struct rd_view { const void *data; size_t rows, cols, stride, element_bytes; };
struct rd_operation {
 uint32_t id; const char *name; const char *kind;
 struct rd_view input[3], output;
 double parameters[32]; uint32_t parameter_count;
};
struct rd_record {
 struct rd_operation op;
 uint64_t inference, begin_cycle, end_cycle, overhead_cycles;
 uint64_t input_hash[3], output_hash, padding_hash, immutable_hash, image_hash;
 uint32_t mode, hart, end_hart, guard_error, mismatch, drained;
};
struct rd_expected { uint64_t input[3], output; };
extern struct rd_record ritnet_diag_records[RD_OPERATIONS*RD_MAX_INFERENCES];
/* Completed-inference observations, never per-operation checkpoints. */
extern struct rd_record ritnet_diag_post_records[2];
extern uint32_t ritnet_diag_post_count;
extern uint32_t ritnet_diag_count,ritnet_diag_error;
extern uint32_t ritnet_diag_capture_size,ritnet_diag_capture_id,ritnet_diag_capture_inference;
extern unsigned char ritnet_diag_capture[RD_CAPTURE_BYTES];
uint64_t rd_hash(struct rd_view view);
size_t rd_pack(struct rd_view view, unsigned char *destination, size_t capacity);
int rd_inference_begin(const void *image,size_t bytes);
void rd_begin(const struct rd_operation *operation);
int rd_end(void);
void rd_inference_end(void);
void rd_final_failure_snapshot(void);
/* Defined beside the private model buffers; never export them to other plugins. */
void rd_drain(void);
int rd_guards(void);
uint64_t rd_immutable(void);
void rd_reference_tensor(const struct rd_record *record); /* Host-only override. */
void rd_reference_inputs(const struct rd_record *record); /* Before in-place writes. */
#ifdef __cplusplus
}
#endif
