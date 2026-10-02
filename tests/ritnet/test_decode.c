#include "ritnet.h"
#include <assert.h>
#include <string.h>
static int8_t tensor[160*240*4];
int main(void) {
 struct ritnet_result r;
 assert(ritnet_decode(0,&r)==-1);
 assert(ritnet_decode(tensor,0)==-1);
 assert(!ritnet_decode(tensor,&r)&&!r.valid&&r.foreground==0);
 tensor[(30*240+20)*4+2]=1;
 assert(!ritnet_decode(tensor,&r)&&r.valid&&r.x==20&&r.y==30&&r.foreground==1);
 tensor[(50*240+40)*4+3]=1;
 assert(!ritnet_decode(tensor,&r)&&r.x==30&&r.y==40&&r.foreground==2);
 // A class-0 tie must remain background.
 tensor[(10*240+10)*4]=1;tensor[(10*240+10)*4+1]=1;
 assert(!ritnet_decode(tensor,&r)&&r.foreground==2);
 memset(tensor,-128,sizeof(tensor));tensor[(159*240+239)*4+3]=127;
 assert(!ritnet_decode(tensor,&r)&&r.x==239&&r.y==159&&r.foreground==1);
 return 0;
}
