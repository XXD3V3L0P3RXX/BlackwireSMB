/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "idle_log_cache.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static struct bw_idle_log_cache *owner,*nested;
static struct bw_startup_idle_request request;
static const char op[]="0123456789abcdef0123456789abcdef";
static double ceiling;
static int enter=1,inner_result=-9;
static int guard(void *opaque){
    (void)opaque;
    if(enter){enter=0;inner_result=bw_idle_log_cache_begin(&owner,op,ceiling,&request);nested=owner;}
    return 0;
}
int main(int argc,char **argv){
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 2;
    ceiling=(double)t.tv_sec+(double)t.tv_nsec/1e9+20;
    request=(struct bw_startup_idle_request){.deadline=ceiling,.current=guard,.game_log="/tmp/blackwire-begin-reentrancy-fixture"};
    int outer=bw_idle_log_cache_begin(&owner,op,ceiling,&request);
    int overwritten=!inner_result&&nested&&owner!=nested;
    int preserved=!inner_result&&nested&&owner==nested&&outer==-1;
    struct bw_idle_log_cache *published=owner;struct bw_idle_log_cache_release release={0};
    int closed=bw_idle_log_cache_close(&owner,&release);
    if(nested!=published){int rc=bw_idle_log_cache_close(&nested,&release);if(rc)closed=rc;}
    else nested=NULL;
    int expected=argc==2&&!strcmp(argv[1],"--expect-overwrite")?overwritten:preserved;
    printf("{\"passed\":%s,\"innerResult\":%d,\"outerResult\":%d,\"sameSlotOverwritten\":%s,\"nestedOwnerPreserved\":%s,\"allOwnersClosed\":%s}\n",
       expected&&!closed?"true":"false",inner_result,outer,overwritten?"true":"false",preserved?"true":"false",!closed&&!owner&&!nested?"true":"false");
    return expected&&!closed&&!owner&&!nested?0:1;
}
