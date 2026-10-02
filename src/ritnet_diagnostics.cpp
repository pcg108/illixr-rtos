#include "../third_party/ritnet/port/diagnostics.h"
#include "trace_output.hpp"
namespace ILLIXR::ritnet_diagnostics {
void dump() {
 trace_output::print("ILLIXR_RITNET_DIAG {\"version\":2,\"mode\":%u,\"drain_mask\":%llu,\"records\":%u,\"error\":%u,\"max_inferences\":%u,\"capture_size\":%u,\"capture_operation\":%u,\"capture_inference\":%u,\"record_bytes\":%zu,\"capture_capacity\":%u,\"post_records\":%u}\n",ritnet_diag_mode,(unsigned long long)ritnet_diag_drain_mask,ritnet_diag_count,ritnet_diag_error,ritnet_diag_inferences,ritnet_diag_capture_size,ritnet_diag_capture_id,ritnet_diag_capture_inference,sizeof(ritnet_diag_records),RD_CAPTURE_BYTES,ritnet_diag_post_count);
 for(unsigned i=0;i<ritnet_diag_count;i++) {
  const auto &r=ritnet_diag_records[i];const auto &o=r.op;
  trace_output::print("ILLIXR_RITNET_OP {\"inference\":%llu,\"id\":%u,\"name\":\"%s\",\"kind\":\"%s\",\"begin_cycle\":%llu,\"end_cycle\":%llu,\"overhead_cycles\":%llu,\"hart\":%u,\"end_hart\":%u,\"mode\":%u,\"drained\":%u,\"guard_error\":%u,\"mismatch\":%u,\"image_hash\":%llu,\"immutable_hash\":%llu,\"output_hash\":%llu,\"padding_hash\":%llu,\"input_hash\":[%llu,%llu,%llu],\"views\":[",
   (unsigned long long)r.inference,o.id,o.name,o.kind,(unsigned long long)r.begin_cycle,(unsigned long long)r.end_cycle,(unsigned long long)r.overhead_cycles,r.hart,r.end_hart,r.mode,r.drained,r.guard_error,r.mismatch,(unsigned long long)r.image_hash,(unsigned long long)r.immutable_hash,(unsigned long long)r.output_hash,(unsigned long long)r.padding_hash,(unsigned long long)r.input_hash[0],(unsigned long long)r.input_hash[1],(unsigned long long)r.input_hash[2]);
  for(unsigned j=0;j<4;j++) {
   const auto &v=j<3?o.input[j]:o.output;
   trace_output::print("%s{\"address\":%llu,\"rows\":%zu,\"cols\":%zu,\"stride\":%zu,\"element_bytes\":%zu}",j?",":"",(unsigned long long)(uintptr_t)v.data,v.rows,v.cols,v.stride,v.element_bytes);
  }
  trace_output::print("],\"parameters\":[");
  for(unsigned j=0;j<o.parameter_count;j++)trace_output::print("%s%.17g",j?",":"",o.parameters[j]);
  trace_output::print("]}\n");
 }
 for(unsigned i=0;i<ritnet_diag_post_count;i++) {
  const auto &r=ritnet_diag_post_records[i];
  trace_output::print("ILLIXR_RITNET_POST {\"observation\":\"after_failed_inference\",\"inference\":%llu,\"id\":%u,\"begin_cycle\":%llu,\"end_cycle\":%llu,\"hart\":%u,\"end_hart\":%u,\"drained\":%u,\"guard_error\":%u,\"mismatch\":%u,\"image_hash\":%llu,\"immutable_hash\":%llu,\"input_hash\":[%llu,%llu,%llu],\"output_hash\":%llu}\n",
   (unsigned long long)r.inference,r.op.id,(unsigned long long)r.begin_cycle,(unsigned long long)r.end_cycle,r.hart,r.end_hart,r.drained,r.guard_error,r.mismatch,(unsigned long long)r.image_hash,(unsigned long long)r.immutable_hash,(unsigned long long)r.input_hash[0],(unsigned long long)r.input_hash[1],(unsigned long long)r.input_hash[2],(unsigned long long)r.output_hash);
 }
 constexpr char hex[]="0123456789abcdef";
 for(unsigned offset=0;offset<ritnet_diag_capture_size;offset+=256) {
  const auto n=ritnet_diag_capture_size-offset<256?ritnet_diag_capture_size-offset:256;
  char encoded[513];for(unsigned j=0;j<n;j++) {auto b=ritnet_diag_capture[offset+j];encoded[2*j]=hex[b>>4];encoded[2*j+1]=hex[b&15];}encoded[2*n]=0;
  trace_output::print("ILLIXR_RITNET_CAPTURE {\"offset\":%u,\"hex\":\"%s\"}\n",offset,encoded);
 }
}
}
