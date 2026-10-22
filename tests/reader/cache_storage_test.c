/* Actual canonical cache implementation with counted allocation/free calls.
 * No native heap/VM availability claim. Borrowed payload is real static memory. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned cases,checks,calloc_calls,malloc_calls,free_calls,loads;
static int fail_entries,fail_payload,fail_load;
static void *allocated_entries,*allocated_payload;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"cache-storage line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *counted_calloc(size_t n,size_t size){calloc_calls++;if(fail_entries){errno=0;return NULL;}allocated_entries=calloc(n,size);CHECK(allocated_entries);return allocated_entries;}
static void *counted_malloc(size_t size){malloc_calls++;CHECK(size==67108864);if(fail_payload){errno=0;return NULL;}allocated_payload=malloc(size);CHECK(allocated_payload);return allocated_payload;}
static void counted_free(void *p){if(!p)return;CHECK(p==allocated_entries||p==allocated_payload);if(p==allocated_entries)allocated_entries=NULL;if(p==allocated_payload)allocated_payload=NULL;free_calls++;free(p);}
#define calloc counted_calloc
#define malloc counted_malloc
#define free counted_free
#include "cache-under-test.c"
#undef calloc
#undef malloc
#undef free
static unsigned char storage[BW_CACHE_BYTES];
static int loader(void *opaque,uint32_t id,uint64_t block,uint8_t *out,size_t capacity,size_t *size){
    CHECK(opaque==&loads&&id==7&&capacity==BW_CACHE_BLOCK);loads++;
    memset(out,(int)(block%251),capacity);*size=block==9?7:capacity;return fail_load?-1:0;
}
static void reset(void){CHECK(!allocated_entries&&!allocated_payload);calloc_calls=malloc_calls=free_calls=loads=0;fail_entries=fail_payload=fail_load=0;storage[0]=0x31;storage[sizeof storage-1]=0x79;}
int main(void){
    struct bw_cache cache;const uint8_t *data=NULL;size_t size=0;
    reset();cases++;CHECK(!bw_cache_init(&cache));CHECK(calloc_calls==1&&malloc_calls==1&&!cache._external_storage&&cache.allocation_bytes==BW_CACHE_BYTES+BW_CACHE_SLOTS*sizeof(struct bw_cache_entry));bw_cache_destroy(&cache);CHECK(free_calls==2);bw_cache_destroy(&cache);CHECK(free_calls==2);
    reset();cases++;fail_entries=1;CHECK(bw_cache_init(&cache)==-1);CHECK(calloc_calls==1&&malloc_calls==1&&free_calls==1&&!cache._entries&&!cache._storage);bw_cache_destroy(&cache);
    reset();cases++;fail_payload=1;CHECK(bw_cache_init(&cache)==-1);CHECK(calloc_calls==1&&malloc_calls==1&&free_calls==1&&!cache._entries&&!cache._storage);bw_cache_destroy(&cache);
    reset();cases++;CHECK(bw_cache_init_with_storage(NULL,storage,sizeof storage)==-1);CHECK(bw_cache_init_with_storage(&cache,NULL,sizeof storage)==-1);CHECK(bw_cache_init_with_storage(&cache,storage,sizeof storage-1)==-1);CHECK(bw_cache_init_with_storage(&cache,storage,sizeof storage+1)==-1);CHECK(!calloc_calls&&!malloc_calls&&!free_calls);bw_cache_destroy(&cache);CHECK(storage[0]==0x31&&storage[sizeof storage-1]==0x79);
    reset();cases++;fail_entries=1;CHECK(bw_cache_init_with_storage(&cache,storage,sizeof storage)==-2);CHECK(calloc_calls==1&&!malloc_calls&&!free_calls&&!cache._entries&&!cache._storage);CHECK(storage[0]==0x31&&storage[sizeof storage-1]==0x79);bw_cache_destroy(&cache);CHECK(!free_calls);
    reset();cases++;CHECK(!bw_cache_init_with_storage(&cache,storage,sizeof storage));CHECK(calloc_calls==1&&!malloc_calls&&cache._external_storage&&cache._storage==storage);CHECK(cache.allocation_bytes==BW_CACHE_BYTES+BW_CACHE_SLOTS*sizeof(struct bw_cache_entry));CHECK(storage[0]==0x31&&storage[sizeof storage-1]==0x79);CHECK(!bw_cache_get(&cache,7,9,loader,&loads,&data,&size));CHECK(data==storage&&size==7&&data[0]==9&&loads==1);CHECK(!bw_cache_get(&cache,7,9,loader,&loads,&data,&size)&&loads==1&&cache.hits==1);bw_cache_destroy(&cache);CHECK(free_calls==1&&storage[0]==9&&storage[sizeof storage-1]==0x79);bw_cache_destroy(&cache);CHECK(free_calls==1);
    reset();cases++;CHECK(!bw_cache_init_with_storage(&cache,storage,sizeof storage));fail_load=1;CHECK(bw_cache_get(&cache,7,4,loader,&loads,&data,&size)==-1&&data==NULL&&size==0&&!cache.resident_count);fail_load=0;CHECK(!bw_cache_get(&cache,7,4,loader,&loads,&data,&size)&&size==BW_CACHE_BLOCK&&loads==2&&data[0]==4);bw_cache_destroy(&cache);CHECK(free_calls==1&&!malloc_calls);
    reset();cases++;CHECK(!bw_cache_init_with_storage(&cache,storage,sizeof storage));for(uint64_t block=0;block<=BW_CACHE_SLOTS;block++)CHECK(!bw_cache_get(&cache,7,block,loader,&loads,&data,&size));CHECK(cache.resident_count==BW_CACHE_SLOTS&&cache.evictions==1&&!malloc_calls);bw_cache_destroy(&cache);CHECK(free_calls==1);CHECK(!bw_cache_init_with_storage(&cache,storage,sizeof storage));CHECK(!cache.resident_count&&!cache.hits);bw_cache_destroy(&cache);CHECK(free_calls==2&&!malloc_calls);
    reset();cases++;CHECK(!bw_cache_contains(NULL,7,0));CHECK(!bw_cache_init_with_storage(&cache,storage,sizeof storage));
    CHECK(!bw_cache_contains(&cache,7,0));CHECK(!bw_cache_get(&cache,7,0,loader,&loads,&data,&size));
    struct bw_cache before=cache;struct bw_cache_entry entries[BW_CACHE_SLOTS];memcpy(entries,cache._entries,sizeof entries);
    CHECK(bw_cache_contains(&cache,7,0)&&!bw_cache_contains(&cache,8,0)&&!bw_cache_contains(&cache,7,1));
    CHECK(!memcmp(&before,&cache,sizeof cache)&&!memcmp(entries,cache._entries,sizeof entries)&&loads==1&&malloc_calls==0);
    fail_load=1;CHECK(bw_cache_get(&cache,7,1,loader,&loads,&data,&size)==-1&&!bw_cache_contains(&cache,7,1));
    bw_cache_destroy(&cache);CHECK(!bw_cache_contains(&cache,7,0));
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualCanonicalCache\":true,\"fixedPayloadBytes\":%zu,\"entryMetadataBytes\":%zu,\"normalMallocPathRetained\":true,\"borrowedPayloadNeverFreed\":true,\"nativeAllocationProved\":false}\n",cases,checks,(size_t)BW_CACHE_BYTES,(size_t)BW_CACHE_SLOTS*sizeof(struct bw_cache_entry));
}
