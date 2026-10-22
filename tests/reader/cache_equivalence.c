/* SPDX-License-Identifier: GPL-3.0-or-later
 * Differential checks against the unchanged released cache, plus a CPU-only
 * lookup microbenchmark. This does not predict PS5 game loading time. */
#define _POSIX_C_SOURCE 200809L
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
int reference_cache_init(struct bw_cache *);
void reference_cache_destroy(struct bw_cache *);
int reference_cache_contains(const struct bw_cache *,uint32_t,uint64_t);
int reference_cache_get(struct bw_cache *,uint32_t,uint64_t,bw_cache_loader,void *,const uint8_t **,size_t *);
static unsigned checks,fail_load;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"cache-equivalence line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int load(void *opaque,uint32_t id,uint64_t block,uint8_t *out,size_t capacity,size_t *size){
    (void)opaque;C(capacity==BW_CACHE_BLOCK);memset(out,(int)((id*31u+block*17u)%251),capacity);
    *size=block%19u==0?7:capacity;return fail_load?-1:0;}
static uint64_t now(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static void equivalence(void){
    struct bw_cache actual,reference;C(!bw_cache_init(&actual)&&!reference_cache_init(&reference));uint64_t rng=7391;
    for(unsigned i=0;i<30000;i++){
        rng=rng*UINT64_C(6364136223846793005)+1;uint32_t id=(uint32_t)(rng>>32)%8u;
        uint64_t block=(rng>>12)%(i%3u?128u:2048u);fail_load=i%37u==0;
        const uint8_t *a=NULL,*b=NULL;size_t an=0,bn=0;
        C(bw_cache_contains(&actual,id,block)==reference_cache_contains(&reference,id,block));
        int ar=bw_cache_get(&actual,id,block,load,NULL,&a,&an),br=reference_cache_get(&reference,id,block,load,NULL,&b,&bn);
        C(ar==br&&an==bn);if(!ar)C(a&&b&&!memcmp(a,b,an));else C(!a&&!b&&!an&&!bn);
        C(actual.hits==reference.hits&&actual.misses==reference.misses&&actual.evictions==reference.evictions&&
          actual.loaded_bytes==reference.loaded_bytes&&actual.resident_count==reference.resident_count);
    }
    bw_cache_destroy(&actual);reference_cache_destroy(&reference);
}
static uint64_t benchmark(int indexed){
    struct bw_cache cache;C(!(indexed?bw_cache_init(&cache):reference_cache_init(&cache)));fail_load=0;
    const uint8_t *data;size_t size;volatile uint64_t sink=0;
    for(unsigned i=0;i<BW_CACHE_SLOTS;i++)C(!(indexed?bw_cache_get(&cache,7,i,load,NULL,&data,&size):reference_cache_get(&cache,7,i,load,NULL,&data,&size)));
    uint64_t begin=now();
    for(unsigned i=0;i<1000000;i++){
        uint64_t key=(uint64_t)(i*811u)%BW_CACHE_SLOTS;
        C(!(indexed?bw_cache_get(&cache,7,key,load,NULL,&data,&size):reference_cache_get(&cache,7,key,load,NULL,&data,&size)));
        sink+=data[0];
    }
    uint64_t duration=now()-begin;C(sink>0);if(indexed)bw_cache_destroy(&cache);else reference_cache_destroy(&cache);return duration;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"benchmark")){
        uint64_t reference=benchmark(0),indexed=benchmark(1);
        printf("{\"passed\":true,\"referenceNanoseconds\":%" PRIu64 ",\"indexedNanoseconds\":%" PRIu64 ",\"lookups\":1000000,\"cpuOnly\":true}\n",reference,indexed);
    }else{equivalence();printf("{\"passed\":true,\"checks\":%u,\"operations\":30000,\"releasedLruAndFailureBehaviorPreserved\":true}\n",checks);}return 0;
}
