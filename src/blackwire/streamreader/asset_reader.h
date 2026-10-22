#ifndef BW_ASSET_READER_H
#define BW_ASSET_READER_H
#include "asset_volume_smb.h"
#include "asset_volume_bridge.h"

#define BW_ASSET_SELECTION_BYTES 312u
#define BW_ASSET_READER_METADATA (128u*1024u*1024u)
#define BW_ASSET_SELECTED_MAP "blackwire-pack.files"
#define BW_ASSET_SELECTION_NAME "blackwire-pack.selection"

struct bw_asset_reader_selection {
    const char *run_id, *title_id, *game;
    uint64_t eboot_size, manifest_hash;
    uint32_t manifest_count;
};

/* Source/cache owner for the existing reader. Borrows one connected SMB
 * context and the selected file-map bytes. No game-memory, mount, launch,
 * allocation-in-another-process, or staging-file operations. Zero-initialize,
 * keep at a stable address, serialize calls, and close BEFORE the map/context.
 * The planner must fully validate the canonical RAW pack before publication.
 */
struct bw_asset_reader {
    struct bw_asset_smb source;
    struct bw_asset_bridge bridge;
    void *source_map, *volume_map;
    size_t metadata_bytes;
    uint64_t read_bytes, hit_bytes;
    int ready;
    const char *error;
};

/* root is the exact canonical prepared directory (without trailing slash).
 * Independent hashes come from a fixed-size, run-bound selection sidecar.
 * All local inputs are opened read-only with named/held identity checks.
 * Errors free allocations and handles; an explicit close is required before
 * reopening. The retained cache is exactly the existing 64 MiB cache.
 */
int bw_asset_reader_open(struct bw_asset_reader *, struct smb2_context *, const char *root,
    const struct bw_asset_reader_selection *, const void *selected_map, size_t map_size);
int bw_asset_reader_read(struct bw_asset_reader *, uint32_t file_id, uint64_t offset,
    void *output, size_t length, size_t capacity);
int bw_asset_reader_close(struct bw_asset_reader *);
/* Private fixed-file staging only. A successful read inside begin/commit is
 * provisional: commit must pass before forwarding a native request. Abort
 * poisons the source on any intervening read/write/ownership failure. */
int bw_asset_reader_stage_begin(struct bw_asset_reader *);
int bw_asset_reader_stage_commit(struct bw_asset_reader *);
void bw_asset_reader_stage_abort(struct bw_asset_reader *);
const char *bw_asset_reader_error(const struct bw_asset_reader *);
#endif
