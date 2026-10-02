#include "ritnet.h"
int ritnet_decode(const int8_t *tensor,struct ritnet_result *result) {
    if(!tensor || !result) return -1;
    uint64_t sx=0,sy=0,n=0;
    uint64_t hash=14695981039346656037ull;
    for (unsigned y=0;y<160;y++) for(unsigned x=0;x<240;x++) {
        unsigned best=0;
        for(unsigned c=0;c<4;c++) {
            hash ^= (uint8_t)tensor[(y*240+x)*4+c]; hash *= 1099511628211ull;
            if(tensor[(y*240+x)*4+c]>tensor[(y*240+x)*4+best]) best=c;
        }
        if(best) { sx+=x; sy+=y; ++n; }
    }
    result->foreground=n; result->valid=n!=0;
    result->x=n ? (double)sx/n : 0; result->y=n ? (double)sy/n : 0;
    result->output_hash=hash;
    return 0;
}
