#ifndef BW_ASSET_VOLUME_BRIDGE_H
#define BW_ASSET_VOLUME_BRIDGE_H
#include "asset_volume_cache.h"

#define BW_ASSET_PACK_INDEX "ampr_assets.index"
#define BW_ASSET_PACK_VOLUME "blackwire-assets-000.pak"
#define BW_ASSET_PACK_MAX_METADATA (64u*1024u*1024u)

/* File-map seam for the game reader. The selected local map is the exact
 * original source map followed by these two owned publication files:
 *   original_count:   LOCAL  ampr_assets.index
 *   original_count+1: STREAM blackwire-assets-000.pak
 * Original IDs, paths, flags, lengths and timestamps remain unchanged.
 * The extra records have mtime=0; they are not files on the PC share.
 *
 * Owns one fixed session cache. Borrows stable volume/source ops, map bytes and
 * metadata. The same ops and read-only handle pool serve original files and
 * the virtual volume. No filesystem, staging, process or hook operations.
 */
struct bw_asset_bridge {
    struct bw_asset_cache cache;
    struct bw_manifest selected;
    uint32_t index_id,volume_id;
    int ready;
};

/* The owner must validate the complete AMPRPAK4 format/RAW layout before
 * publishing it. This seam additionally pins actual published map/index bytes
 * to independent selection hashes and checks the index's version/build ID.
 * Hashes must not be calculated from unchecked inputs at this admission site.
 * Zero-initialize; close before reopening. No allocation remains on failure.
 */
int bw_asset_bridge_open(struct bw_asset_bridge *,struct bw_asset_volume *,const struct bw_asset_ops *,
    const void *selected_map,size_t map_size,const uint8_t expected_map_sha[32],
    const void *pack_index,size_t index_size,const uint8_t expected_index_sha[32]);
int bw_asset_bridge_read(struct bw_asset_bridge *,uint32_t selected_file_id,uint64_t offset,
    void *,size_t length,size_t capacity);
void bw_asset_bridge_close(struct bw_asset_bridge *);
#endif
