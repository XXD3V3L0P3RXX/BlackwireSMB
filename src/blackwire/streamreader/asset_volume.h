#ifndef BW_ASSET_VOLUME_H
#define BW_ASSET_VOLUME_H

#include "manifest.h"
#include <stddef.h>
#include <stdint.h>

#define BW_ASSET_VOLUME_HEADER 112u
#define BW_ASSET_VOLUME_RECORD 24u
#define BW_ASSET_VOLUME_PAGE UINT64_C(65536)
#define BW_ASSET_VOLUME_MAX_READ (4u * 1024u * 1024u)
#define BW_ASSET_VOLUME_MAX_LENGTH ((UINT64_C(1)<<48)-BW_ASSET_VOLUME_PAGE)

enum bw_asset_error {
    BW_ASSET_OK=0, BW_ASSET_ARGUMENT, BW_ASSET_FORMAT, BW_ASSET_GENERATION,
    BW_ASSET_RANGE, BW_ASSET_SOURCE, BW_ASSET_POISONED
};

/* Caller owns stable, immutable input buffers for the complete binding.
 * No retained allocation, filesystem operation, source write or game hook.
 * The shared manifest parser uses at most 4 MiB of temporary validation data.
 */
struct bw_asset_volume {
    const uint8_t *_records;
    struct bw_manifest source;
    uint64_t size;
    uint32_t count;
    uint8_t header[64],build_id[16];
    int ready,failed;
};

/* All callbacks are synchronous, serialized and read-only. healthy checks the
 * owning source/lifecycle binding; generation checks the admitted source file.
 * read_exact must either fill the entire range or fail. Never return successful
 * zeros for unavailable source data. Discard the complete output on any error.
 */
struct bw_asset_ops {
    void *context;
    int (*healthy)(void *);
    int (*generation)(void *,uint32_t file_id);
    int (*read_exact)(void *,uint32_t file_id,uint64_t offset,void *,size_t);
    /* Required by the cache adapter: nonzero admission epoch, changed on every
     * explicit transport rebind and zero when unavailable. Never wrap/reuse it.
     * The uncached range core does not require or invoke this callback. */
    uint64_t (*binding)(void *);
};

/* expected_map_sha must be independently pinned by the owning selection.
 * apr_sha is the digest of the actual immutable APR index for that selection.
 * Map admission binds every nonempty STREAM source exactly once, with complete
 * lengths, contiguous page-rounded placement and matching source/build hashes.
 * The genuine AMPRDAT3 header and CRC are validated independently. Failure
 * clears the output view, including its source parser state.
 */
enum bw_asset_error bw_asset_volume_open(struct bw_asset_volume *,
    const void *map,size_t map_size,const uint8_t expected_map_sha[32],
    const void *source_manifest,size_t manifest_size,const uint8_t apr_sha[32],
    const void *header,size_t header_size);

/* Binary-searches the selected extents; no data-sized temporary allocation.
 * Only fixed header/alignment padding is synthesized. Source/generation failure
 * poisons this binding permanently. Range/argument errors do not poison it.
 * A cached caller must guard even cache hits before and after copying data.
 */
enum bw_asset_error bw_asset_volume_guard(struct bw_asset_volume *,
    const struct bw_asset_ops *,uint64_t offset,size_t length);
enum bw_asset_error bw_asset_volume_read(struct bw_asset_volume *,
    const struct bw_asset_ops *,uint64_t offset,void *output,size_t length,size_t capacity);

/* The same admitted original STREAM files can be read without translating
 * through the physical volume. IDs stay in the original source manifest.
 * LOCAL/unknown IDs are argument errors; bytes are never synthesized here.
 * Empty ranges check health but need no source handle or generation callback.
 * Source failures poison the shared volume binding, including physical reads.
 */
enum bw_asset_error bw_asset_source_guard(struct bw_asset_volume *,
    const struct bw_asset_ops *,uint32_t file_id,uint64_t offset,size_t length);
enum bw_asset_error bw_asset_source_read(struct bw_asset_volume *,
    const struct bw_asset_ops *,uint32_t file_id,uint64_t offset,void *,size_t length,size_t capacity);
const char *bw_asset_error_string(enum bw_asset_error);
#endif
