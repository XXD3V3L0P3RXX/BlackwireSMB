#ifndef BW_CONTAINER_CACHE_H
#define BW_CONTAINER_CACHE_H
#include "cache.h"
#include "container_source.h"

/* Borrows the existing initialized 64 MiB cache and immutable file tokens;
 * allocates no second cache. Destroy/clear the cache between source sessions.
 * All borrowed objects must outlive this binding; serialize every operation.
 * Discard read output on error. Cached get output follows cache.h lifetime.
 */
struct bw_container_cache {
    struct bw_cache *cache;
    struct bw_container *source;
    const struct bw_container_file *files;
    uint64_t session;
    uint32_t count;
    int failed,last_error;
};
int bw_container_cache_bind(struct bw_container_cache *,struct bw_cache *,struct bw_container *,const struct bw_container_file *,uint32_t);
int bw_container_cached_get(struct bw_container_cache *,uint32_t,uint64_t,const uint8_t **,size_t *);
int bw_container_cached_read(struct bw_container_cache *,uint32_t,uint64_t,void *,size_t);
#endif
