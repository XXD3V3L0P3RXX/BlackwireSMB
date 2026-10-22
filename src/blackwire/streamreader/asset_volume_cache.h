#ifndef BW_ASSET_VOLUME_CACHE_H
#define BW_ASSET_VOLUME_CACHE_H
#include "asset_volume.h"
#include "cache.h"

/* One existing 64 MiB data cache for a single admitted volume/session. This is
 * the session's cache, not an additional cache beside the normal reader cache.
 * Key 0 stores physical volume blocks; key source_id+1 stores original STREAM
 * blocks. Both use one allocation and the same owning source/handle pool.
 * Borrows stable volume/ops; serialize calls and close before either changes.
 * No process access, staging-file writes, mount or filesystem operations.
 */
struct bw_asset_cache {
    struct bw_cache cache;
    struct bw_asset_volume *volume;
    struct bw_asset_ops ops;
    uint64_t binding_id;
    int ready,failed;
};
/* Zero-initialize once; close before reopening. Source admission belongs to
 * the owning transport. Rebinding without clearing cached data is forbidden.
 */
int bw_asset_cache_open(struct bw_asset_cache *,struct bw_asset_volume *,const struct bw_asset_ops *);
int bw_asset_cache_read(struct bw_asset_cache *,uint64_t offset,void *,size_t length,size_t capacity);
int bw_asset_cache_read_source(struct bw_asset_cache *,uint32_t file_id,uint64_t offset,
    void *,size_t length,size_t capacity);
void bw_asset_cache_close(struct bw_asset_cache *);
#endif
