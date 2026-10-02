#include "ritnet.h"
#include "diagnostics.h"
#include "../../third_party/ritnet/reference/expected.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
static const char *directory;
static FILE *manifest,*header;
static unsigned char packed[RD_CAPTURE_BYTES],previous[RD_CAPTURE_BYTES];
static void tensor_file(const struct rd_record *r,struct rd_view v,int index) {
 if(!v.data)return;
 size_t n=rd_pack(v,packed,sizeof(packed));if(n==SIZE_MAX)abort();
 char path[4096];snprintf(path,sizeof(path),"%s/op%02u-%s%d.bin",directory,r->op.id,index<0?"output":"input",index<0?0:index);
 FILE *f=fopen(path,r->inference==1?"wb":"rb");if(!f)abort();
 if(r->inference==1) {if(fwrite(packed,1,n,f)!=n)abort();}
 else if(fread(previous,1,n,f)!=n || memcmp(previous,packed,n))abort();
 if(fclose(f))abort();
}
void rd_reference_inputs(const struct rd_record *r) {
 for(unsigned i=0;i<3;i++)tensor_file(r,r->op.input[i],i);
}
void rd_reference_tensor(const struct rd_record *r) {
 tensor_file(r,r->op.output,-1);
 if(r->inference!=1)return;
 fprintf(header,"{{UINT64_C(0x%016" PRIx64 "),UINT64_C(0x%016" PRIx64 "),UINT64_C(0x%016" PRIx64 ")},UINT64_C(0x%016" PRIx64 ")},\n",r->input_hash[0],r->input_hash[1],r->input_hash[2],r->output_hash);
 fprintf(manifest,"{\"id\":%u,\"name\":\"%s\",\"kind\":\"%s\",\"input_hash\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],\"output_hash\":%" PRIu64 ",\"image_hash\":%" PRIu64 ",\"immutable_hash\":%" PRIu64 ",\"views\":[",r->op.id,r->op.name,r->op.kind,r->input_hash[0],r->input_hash[1],r->input_hash[2],r->output_hash,r->image_hash,r->immutable_hash);
 for(unsigned i=0;i<4;i++) {
  struct rd_view v=i<3?r->op.input[i]:r->op.output;
  fprintf(manifest,"%s{\"rows\":%zu,\"cols\":%zu,\"stride\":%zu,\"element_bytes\":%zu}",i?",":"",v.rows,v.cols,v.stride,v.element_bytes);
 }
 fprintf(manifest,"],\"parameters\":[");
 for(unsigned i=0;i<r->op.parameter_count;i++)fprintf(manifest,"%s%.17g",i?",":"",r->op.parameters[i]);
 fprintf(manifest,"]}\n");
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;directory=argv[1];char path[4096];
 snprintf(path,sizeof(path),"%s/operations.jsonl",directory);manifest=fopen(path,"w");
 snprintf(path,sizeof(path),"%s/ritnet_diagnostic_expected.h",directory);header=fopen(path,"w");if(!manifest||!header)return 2;
 fprintf(header,"#pragma once\n#include <stdint.h>\nstatic const struct rd_expected rd_expected_operations[RD_OPERATIONS]={\n");
 for(unsigned i=0;i<2;i++) {
  struct ritnet_result r={0};int status=ritnet_infer(ritnet_sample(),&r);
  if(status || !r.valid || r.output_hash!=RITNET_EXPECTED_HASH) {fprintf(stderr,"reference failed %u status=%d diagnostic=%u hash=%" PRIu64 "\n",i,status,ritnet_diag_error,r.output_hash);return 3;}
  fprintf(stderr,"reference inference %u exact\n",i+1);
 }
 if(ritnet_diag_count!=2*RD_OPERATIONS)return 4;
 fprintf(header,"};\nstatic const uint64_t rd_expected_image=UINT64_C(0x%016" PRIx64 ");\nstatic const uint64_t rd_expected_immutable=UINT64_C(0x%016" PRIx64 ");\n",ritnet_diag_records[0].image_hash,ritnet_diag_records[0].immutable_hash);
 fclose(header);fclose(manifest);return 0;
}
