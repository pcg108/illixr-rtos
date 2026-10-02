#include "diagnostics.h"
#include <string.h>
#ifdef RITNET_DIAG_EXPECTED
#include "ritnet_diagnostic_expected.h"
#endif
/* Nonzero initialized data permits same-layout runtime-mode ELF variants. */
volatile uint32_t ritnet_diag_mode=3;
volatile uint32_t ritnet_diag_inferences=32;
volatile uint64_t ritnet_diag_drain_mask=UINT64_MAX;
volatile uint32_t ritnet_diag_capture_operation=UINT32_MAX;
struct rd_record ritnet_diag_records[RD_OPERATIONS*RD_MAX_INFERENCES];
struct rd_record ritnet_diag_post_records[2];
uint32_t ritnet_diag_post_count;
uint32_t ritnet_diag_count,ritnet_diag_error;
uint32_t ritnet_diag_capture_size,ritnet_diag_capture_id,ritnet_diag_capture_inference;
unsigned char ritnet_diag_capture[RD_CAPTURE_BYTES];
static uint64_t inference,initial_image,initial_immutable;
static struct rd_view image;
static struct rd_record *current;
static uint64_t tick(void) {
#ifdef RITNET_HOST_REFERENCE
 static uint64_t clock;return ++clock;
#else
 uint64_t c;asm volatile("rdcycle %0":"=r"(c));return c;
#endif
}
static unsigned hart(void) {
#ifdef RITNET_HOST_REFERENCE
 return 0;
#else
 uintptr_t h;asm volatile("csrr %0,mhartid":"=r"(h));return h;
#endif
}
uint64_t rd_hash(struct rd_view v) {
 if(!v.data)return 0;
 const unsigned char *p=v.data;uint64_t hash=UINT64_C(14695981039346656037);
 for(size_t r=0;r<v.rows;r++)for(size_t c=0;c<v.cols*v.element_bytes;c++) {
  hash^=p[r*v.stride*v.element_bytes+c];hash*=UINT64_C(1099511628211);
 }
 return hash;
}
size_t rd_pack(struct rd_view v,unsigned char *destination,size_t capacity) {
 if(!v.data || !v.rows || !v.cols || !v.element_bytes || v.cols>v.stride ||
    v.stride>SIZE_MAX/v.element_bytes || v.rows>capacity/(v.cols*v.element_bytes))return SIZE_MAX;
 const size_t row=v.cols*v.element_bytes;
 if(v.rows-1>(SIZE_MAX-row)/(v.stride*v.element_bytes))return SIZE_MAX;
 for(size_t r=0;r<v.rows;r++)memcpy(destination+r*row,(const char*)v.data+r*v.stride*v.element_bytes,row);
 return v.rows*row;
}
__attribute__((weak)) void rd_reference_tensor(const struct rd_record *r) {(void)r;}
__attribute__((weak)) void rd_reference_inputs(const struct rd_record *r) {(void)r;}
int rd_inference_begin(const void *pixels,size_t bytes) {
 if(!ritnet_diag_mode)return 0;
 if(++inference>RD_MAX_INFERENCES || ritnet_diag_mode>3) {ritnet_diag_error=1;return -1;}
 image=(struct rd_view){pixels,1,bytes,bytes,1};
 /* Immutable data has no accelerator producer; safe without adding a drain. */
 if(ritnet_diag_mode==3) {
  initial_image=rd_hash(image);initial_immutable=rd_immutable();
#ifdef RITNET_DIAG_EXPECTED
  if(initial_image!=rd_expected_image || initial_immutable!=rd_expected_immutable) {ritnet_diag_error=2;return -1;}
#endif
 }
 return 0;
}
void rd_begin(const struct rd_operation *op) {
 current=0;
 if(!ritnet_diag_mode || ritnet_diag_error)return;
 if(ritnet_diag_count>=RD_OPERATIONS*RD_MAX_INFERENCES || !op->id || op->id>RD_OPERATIONS) {ritnet_diag_error=3;return;}
 current=&ritnet_diag_records[ritnet_diag_count++];
 current->op=*op;current->inference=inference;current->mode=ritnet_diag_mode;
 current->hart=hart();current->begin_cycle=tick();
 if(current->mode==3) {
  /* Tensor mode explicitly serializes before CPU reads. This must never be
     mistaken for the original unfenced timing. Drain-only is its control. */
  rd_drain();current->drained=1;
  for(unsigned i=0;i<3;i++)current->input_hash[i]=rd_hash(op->input[i]);
  current->image_hash=initial_image;current->immutable_hash=initial_immutable;
  rd_reference_inputs(current);
 }
 current->overhead_cycles=tick()-current->begin_cycle;
}
int rd_end(void) {
 if(!ritnet_diag_mode)return 0;
 if(!current)return ritnet_diag_error?-1:0;
 const uint64_t begin=tick();
 if(current->mode==3 || (current->mode==2 && (ritnet_diag_drain_mask & (UINT64_C(1)<<(current->op.id-1))))) {rd_drain();current->drained=1;}
 current->end_cycle=tick();current->end_hart=hart();
 if(current->mode==3) {
  current->guard_error=rd_guards();current->output_hash=rd_hash(current->op.output);
  struct rd_view out=current->op.output;
  /* Gaps between rows can contain other concatenation channels. They are
     reported separately, never compared as this operation's output. */
  if(out.rows>1 && out.stride>out.cols) {
   struct rd_view gaps={(const char*)out.data+out.cols*out.element_bytes,out.rows-1,out.stride-out.cols,out.stride,out.element_bytes};
   current->padding_hash=rd_hash(gaps);
  }
#ifdef RITNET_DIAG_EXPECTED
  const struct rd_expected *expected=&rd_expected_operations[current->op.id-1];
  for(unsigned i=0;i<3;i++)if(expected->input[i]!=current->input_hash[i])current->mismatch|=1u<<i;
  if(expected->output!=current->output_hash)current->mismatch|=8;
#endif
  if(current->guard_error)current->mismatch|=16;
  if(current->mismatch) {
   if(rd_hash(image)!=initial_image)current->mismatch|=32;
   if(rd_immutable()!=initial_immutable)current->mismatch|=64;
  }
  if(!ritnet_diag_capture_size && (current->mismatch || current->op.id==ritnet_diag_capture_operation)) {
   const size_t n=rd_pack(out,ritnet_diag_capture,RD_CAPTURE_BYTES);
   if(n==SIZE_MAX)ritnet_diag_error=4;
   else {ritnet_diag_capture_size=n;ritnet_diag_capture_id=current->op.id;ritnet_diag_capture_inference=inference;}
  }
  rd_reference_tensor(current);
 }
 current->overhead_cycles+=tick()-begin;
 if(current->mismatch)ritnet_diag_error=5;
 return ritnet_diag_error?-1:0;
}
void rd_inference_end(void) {current=0;}
void rd_final_failure_snapshot(void) {
 /* Called only after final-output validation fails. A successful inference
    performs no additional reads or drains. Operations 31/32 are the narrowed
    handoff; their logical input/output views survive later graph writes. */
 if(!ritnet_diag_mode || ritnet_diag_error || ritnet_diag_post_count ||
    ritnet_diag_count<RD_OPERATIONS ||
    ritnet_diag_records[ritnet_diag_count-1].op.id!=RD_OPERATIONS)return;
 const uint64_t begin=tick();rd_drain();
 const uint64_t image_hash=rd_hash(image),immutable_hash=rd_immutable();
 const unsigned ids[2]={31,32};
 for(unsigned j=0;j<2;j++) {
  const struct rd_record *source=0;
  for(unsigned i=ritnet_diag_count;i>0;i--) {
   const struct rd_record *r=&ritnet_diag_records[i-1];
   if(r->inference!=inference)break;
   if(r->op.id==ids[j]) {source=r;break;}
  }
  if(!source) {ritnet_diag_error=6;return;}
  struct rd_record *r=&ritnet_diag_post_records[ritnet_diag_post_count++];
  *r=*source;r->begin_cycle=begin;r->hart=hart();r->drained=1;
  r->mismatch=0;r->guard_error=rd_guards();
  r->image_hash=image_hash;r->immutable_hash=immutable_hash;
  for(unsigned i=0;i<3;i++)r->input_hash[i]=rd_hash(r->op.input[i]);
  r->output_hash=rd_hash(r->op.output);
#ifdef RITNET_DIAG_EXPECTED
  const struct rd_expected *e=&rd_expected_operations[r->op.id-1];
  for(unsigned i=0;i<3;i++)if(r->input_hash[i]!=e->input[i])r->mismatch|=1u<<i;
  if(r->output_hash!=e->output)r->mismatch|=8;
  if(image_hash!=rd_expected_image)r->mismatch|=32;
  if(immutable_hash!=rd_expected_immutable)r->mismatch|=64;
#endif
  if(r->guard_error)r->mismatch|=16;
  if(!ritnet_diag_capture_size && (r->mismatch&8)) {
   const size_t n=rd_pack(r->op.output,ritnet_diag_capture,RD_CAPTURE_BYTES);
   if(n==SIZE_MAX)ritnet_diag_error=4;
   else {ritnet_diag_capture_size=n;ritnet_diag_capture_id=r->op.id;ritnet_diag_capture_inference=inference;}
  }
  r->end_cycle=tick();r->end_hart=hart();r->overhead_cycles=r->end_cycle-begin;
 }
}
