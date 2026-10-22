#ifndef BLACKWIRE_CACHE_H
#define BLACKWIRE_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define BW_CACHE_BLOCK 65536u
#define BW_CACHE_SLOTS 1024u
#define BW_CACHE_BYTES ((size_t)BW_CACHE_SLOTS * BW_CACHE_BLOCK)

typedef int (*bw_cache_loader)(void *opaque, uint32_t pak, uint64_t block,
                               uint8_t *dest, size_t capacity, size_t *out_size);

struct bw_cache_entry;
struct bw_cache {
    /* Internal storage and list indices: callers must not modify these. */
    struct bw_cache_entry *_entries;
    uint8_t *_storage;
    size_t _newest, _oldest;

    uint64_t hits, misses, evictions, loaded_bytes;
    size_t allocation_bytes; /* Tracked payload storage + fixed entry metadata. */
    size_t resident_count;
    unsigned char _external_storage; /* Borrowed payload; destroy must not free. */
};

/* Single-threaded; serialize access externally if shared. Initialize once,
 * then destroy before reinitializing. No allocations occur during get(). */
int bw_cache_init(struct bw_cache *cache);
/* Exact fixed-size caller-owned payload; no payload allocation or clearing.
 * Storage must be valid, exclusive, disjoint from cache, and remain alive
 * through destroy and all loader calls. The entry table still uses calloc.
 * Return0 success, -1 invalid storage, -2 entry-table allocation failed.
 * Failure leaves an empty/destroyable cache and never frees borrowed bytes.
 * Destroy frees only entries; the caller retains ownership of storage. */
int bw_cache_init_with_storage(struct bw_cache *cache,void *storage,size_t bytes);
void bw_cache_destroy(struct bw_cache *cache);

/* Single-worker planning query. Does not expose storage, count a hit/miss,
 * change LRU order or allocate. Returns1 only for an already-valid block. */
int bw_cache_contains(const struct bw_cache *,uint32_t pak,uint64_t block);

/* 0 on success, -1 on invalid input, loader error, or invalid loader length.
 * A successful loader must return 1..BW_CACHE_BLOCK bytes. Partial EOF blocks
 * are retained at their exact length. Failed loads are never published.
 * Outputs are cleared on failure. A returned pointer remains valid until that
 * entry is evicted or the cache is destroyed; copy it to staging before another
 * get() can evict it. An eviction is counted even if its replacement load fails.
 */
int bw_cache_get(struct bw_cache *cache, uint32_t pak, uint64_t block,
                 bw_cache_loader loader, void *opaque,
                 const uint8_t **data, size_t *size);

#endif
