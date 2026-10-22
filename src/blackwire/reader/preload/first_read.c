/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "first_read.h"
#include <stdio.h>
#include <string.h>
int bp_first_read_format(struct bp_first_read *state,const struct bp_read_witness *w,int64_t completed,int canceled,char *out,size_t capacity){
    if(out&&capacity)out[0]=0;
    if(!state||!w||!out||!capacity||state->emitted||canceled||!w->valid||completed<=0||
       w->returned!=(uint64_t)completed||w->returned>w->requested||!w->fetched_payload_bytes||
       w->digest_offset!=w->offset||w->digest_size!=(w->returned>65536?65536:w->returned))return 0;
    char hex[65];static const char alphabet[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){hex[2*i]=alphabet[w->digest_sha256[i]>>4];hex[2*i+1]=alphabet[w->digest_sha256[i]&15];}hex[64]=0;
    int n=snprintf(out,capacity,"[BWREAD1] event=first-positive-remote-v1 mapId=%u offset=%llu requested=%llu returned=%lld digestOffset=%llu digestBytes=%u sha256=%s fetchedPayloadBytes=%llu copiedBytes=%llu",
        w->map_id,(unsigned long long)w->offset,(unsigned long long)w->requested,(long long)completed,
        (unsigned long long)w->digest_offset,w->digest_size,hex,(unsigned long long)w->fetched_payload_bytes,(unsigned long long)w->returned);
    if(n<=0||(size_t)n>=capacity){out[0]=0;return -1;}
    state->emitted=1;return n;
}
