#include "ritnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
void ritnet_stage(unsigned stage) { fprintf(stderr,"RITNET_HOST_STAGE %u\n",stage); }
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 struct ritnet_result a,b;
 if(ritnet_infer(ritnet_sample(),&a)) return 3;
 FILE *f=fopen(argv[1],"wb");
 if(!f || fwrite(ritnet_output(),1,160*240*4,f)!=160*240*4 || fclose(f)) return 4;
 int8_t *saved=malloc(160*240*4);if(!saved)return 5;
 memcpy(saved,ritnet_output(),160*240*4);
 if(ritnet_infer(ritnet_sample(),&b) || memcmp(saved,ritnet_output(),160*240*4)) return 6;
 printf("RITNET_HOST_RESULT {\"repeatable\":true,\"valid\":%d,\"x\":%.17g,\"y\":%.17g,\"foreground\":%u,\"hash\":\"%016" PRIx64 "\"}\n",a.valid,a.x,a.y,a.foreground,a.output_hash);
 free(saved);return a.valid?0:7;
}
