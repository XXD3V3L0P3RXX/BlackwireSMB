#include "cache.h"

#include <stdlib.h>
#include <string.h>

#define NO_SLOT SIZE_MAX

struct bw_cache_entry {
    uint64_t block;
    size_t size, newer, older;
    uint32_t pak;
    uint32_t hash_next,free_next;
    unsigned char valid;
};

static void initialize_free(struct bw_cache *cache){
    cache->_free=1;
    for(uint32_t i=0;i<BW_CACHE_SLOTS;i++)cache->_entries[i].free_next=i+1u<BW_CACHE_SLOTS?i+2u:0;
}
static size_t bucket(uint32_t pak,uint64_t block){
    uint64_t value=block^((uint64_t)pak*UINT64_C(0x9e3779b97f4a7c15));
    value^=value>>30;value*=UINT64_C(0xbf58476d1ce4e5b9);
    value^=value>>27;value*=UINT64_C(0x94d049bb133111eb);value^=value>>31;
    return (size_t)value&(BW_CACHE_BUCKETS-1u);
}
static size_t find(const struct bw_cache *cache,uint32_t pak,uint64_t block){
    for(uint32_t link=cache->_buckets[bucket(pak,block)];link;link=cache->_entries[link-1u].hash_next){
        const struct bw_cache_entry *entry=&cache->_entries[link-1u];
        if(entry->pak==pak&&entry->block==block)return link-1u;
    }
    return NO_SLOT;
}
static void unhash(struct bw_cache *cache,size_t index){
    struct bw_cache_entry *entry=&cache->_entries[index];
    uint32_t *link=&cache->_buckets[bucket(entry->pak,entry->block)];
    while(*link&&*link!=index+1u)link=&cache->_entries[*link-1u].hash_next;
    if(*link)*link=entry->hash_next;
    entry->hash_next=0;
}

int bw_cache_init(struct bw_cache *cache) {
    if (!cache) return -1;
    memset(cache, 0, sizeof *cache);
    cache->_newest = cache->_oldest = NO_SLOT;
    cache->_entries = calloc(BW_CACHE_SLOTS, sizeof *cache->_entries);
    cache->_storage = malloc((size_t)BW_CACHE_SLOTS * BW_CACHE_BLOCK);
    if (!cache->_entries || !cache->_storage) {
        bw_cache_destroy(cache);
        return -1;
    }
    cache->allocation_bytes = (size_t)BW_CACHE_SLOTS *
                              (BW_CACHE_BLOCK + sizeof *cache->_entries);
    initialize_free(cache);
    return 0;
}

int bw_cache_init_with_storage(struct bw_cache *cache,void *storage,size_t bytes) {
    if (!cache) return -1;
    memset(cache,0,sizeof *cache);
    cache->_newest=cache->_oldest=NO_SLOT;
    if (!storage || bytes!=BW_CACHE_BYTES ||
        (uintptr_t)storage>UINTPTR_MAX-(BW_CACHE_BYTES-1u)) return -1;
    cache->_entries=calloc(BW_CACHE_SLOTS,sizeof *cache->_entries);
    if (!cache->_entries) {
        bw_cache_destroy(cache);
        return -2;
    }
    cache->_storage=storage;
    cache->_external_storage=1;
    cache->allocation_bytes=(size_t)BW_CACHE_SLOTS *
                            (BW_CACHE_BLOCK + sizeof *cache->_entries);
    initialize_free(cache);
    return 0;
}

void bw_cache_destroy(struct bw_cache *cache) {
    if (!cache) return;
    free(cache->_entries);
    if (!cache->_external_storage) free(cache->_storage);
    memset(cache, 0, sizeof *cache);
}

int bw_cache_contains(const struct bw_cache *cache,uint32_t pak,uint64_t block) {
    if (!cache || !cache->_entries || !cache->_storage) return 0;
    return find(cache,pak,block)!=NO_SLOT;
}

static void unlink_entry(struct bw_cache *cache, size_t index) {
    struct bw_cache_entry *entry = &cache->_entries[index];
    if (entry->newer == NO_SLOT) cache->_newest = entry->older;
    else cache->_entries[entry->newer].older = entry->older;
    if (entry->older == NO_SLOT) cache->_oldest = entry->newer;
    else cache->_entries[entry->older].newer = entry->newer;
}

static void make_newest(struct bw_cache *cache, size_t index) {
    struct bw_cache_entry *entry = &cache->_entries[index];
    entry->newer = NO_SLOT;
    entry->older = cache->_newest;
    if (cache->_newest == NO_SLOT) cache->_oldest = index;
    else cache->_entries[cache->_newest].newer = index;
    cache->_newest = index;
}

int bw_cache_get(struct bw_cache *cache, uint32_t pak, uint64_t block,
                 bw_cache_loader loader, void *opaque,
                 const uint8_t **data, size_t *size) {
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!cache || !cache->_entries || !cache->_storage || !data || !size) return -1;

    /* A fixed index avoids scanning all 1024 entries on each hit and each
     * contiguous-run planning query. LRU order and payload capacity are unchanged. */
    size_t found=find(cache,pak,block);
    if(found!=NO_SLOT){
        struct bw_cache_entry *entry=&cache->_entries[found];
        cache->hits++;
        unlink_entry(cache, found);
        make_newest(cache, found);
        *data = cache->_storage + found * BW_CACHE_BLOCK;
        *size = entry->size;
        return 0;
    }

    cache->misses++;
    if (!loader) return -1;
    size_t index;
    if (!cache->_free) {
        index = cache->_oldest;
        unlink_entry(cache, index);
        unhash(cache,index);
        cache->_entries[index].valid = 0;
        cache->resident_count--;
        cache->evictions++;
    }else{
        index=cache->_free-1u;cache->_free=cache->_entries[index].free_next;
    }

    struct bw_cache_entry *entry = &cache->_entries[index];
    uint8_t *dest = cache->_storage + index * BW_CACHE_BLOCK;
    size_t loaded = 0;
    /* Invalidate before allowing the loader to overwrite an evicted buffer. */
    entry->valid = 0;
    entry->size = 0;
    if (loader(opaque, pak, block, dest, BW_CACHE_BLOCK, &loaded) != 0 ||
        loaded == 0 || loaded > BW_CACHE_BLOCK){
        entry->free_next=cache->_free;cache->_free=(uint32_t)index+1u;return -1;
    }

    entry->pak = pak;
    entry->block = block;
    entry->size = loaded;
    make_newest(cache, index);
    entry->valid = 1;
    size_t hash=bucket(pak,block);entry->hash_next=cache->_buckets[hash];cache->_buckets[hash]=(uint32_t)index+1u;
    cache->resident_count++;
    cache->loaded_bytes += loaded;
    *data = dest;
    *size = loaded;
    return 0;
}
