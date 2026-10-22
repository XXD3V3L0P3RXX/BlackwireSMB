#include "cache.h"

#include <stdlib.h>
#include <string.h>

#define NO_SLOT SIZE_MAX

struct bw_cache_entry {
    uint64_t block;
    size_t size, newer, older;
    uint32_t pak;
    unsigned char valid;
};

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
    for (size_t i=0;i<BW_CACHE_SLOTS;i++) {
        const struct bw_cache_entry *entry=&cache->_entries[i];
        if (entry->valid && entry->pak==pak && entry->block==block) return 1;
    }
    return 0;
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
    size_t empty = NO_SLOT;
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!cache || !cache->_entries || !cache->_storage || !data || !size) return -1;

    /* A bounded scan avoids an additional hash-table allocation and keeps
     * replacement deterministic. The list updates themselves are constant time. */
    for (size_t i = 0; i < BW_CACHE_SLOTS; i++) {
        struct bw_cache_entry *entry = &cache->_entries[i];
        if (!entry->valid) {
            if (empty == NO_SLOT) empty = i;
            continue;
        }
        if (entry->pak != pak || entry->block != block) continue;
        cache->hits++;
        unlink_entry(cache, i);
        make_newest(cache, i);
        *data = cache->_storage + i * BW_CACHE_BLOCK;
        *size = entry->size;
        return 0;
    }

    cache->misses++;
    if (!loader) return -1;
    size_t index = empty;
    if (index == NO_SLOT) {
        index = cache->_oldest;
        unlink_entry(cache, index);
        cache->_entries[index].valid = 0;
        cache->resident_count--;
        cache->evictions++;
    }

    struct bw_cache_entry *entry = &cache->_entries[index];
    uint8_t *dest = cache->_storage + index * BW_CACHE_BLOCK;
    size_t loaded = 0;
    /* Invalidate before allowing the loader to overwrite an evicted buffer. */
    entry->valid = 0;
    entry->size = 0;
    if (loader(opaque, pak, block, dest, BW_CACHE_BLOCK, &loaded) != 0 ||
        loaded == 0 || loaded > BW_CACHE_BLOCK) return -1;

    entry->pak = pak;
    entry->block = block;
    entry->size = loaded;
    make_newest(cache, index);
    entry->valid = 1;
    cache->resident_count++;
    cache->loaded_bytes += loaded;
    *data = dest;
    *size = loaded;
    return 0;
}
