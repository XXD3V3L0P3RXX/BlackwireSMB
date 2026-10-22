/* Isolated native source-bound asset owner. No game launch or hooks.
 * Retains the existing Ampr physical-volume bridge and single cache. */
#ifndef BW_READER_ASSET_H
#define BW_READER_ASSET_H
#include "reader_asset_smb.h"
#include "../streamreader/asset_volume_bridge.h"
#include "source_binding.h"
#include "reader_selection.h"
#include "native_send.h"

#define BW_ASSET_SELECTION_BYTES 312u
#define BW_ASSET_READER_METADATA (128u*1024u*1024u)
#define BW_ASSET_SELECTED_MAP "blackwire-pack.files"
#define BW_ASSET_SELECTION_NAME "blackwire-pack.selection"

struct bw_reader_asset_selection {
    const char *run_id, *title_id, *game;
    uint64_t eboot_size, manifest_hash;
    uint32_t manifest_count;
    const struct bw_share_config *connection;
    const struct bw_source_binding *binding;
    uint32_t source_count;
    uint64_t source_hash;
    uint8_t source_manifest_sha256[32];
    int (*current)(void *);
    void *current_opaque;
};
/* The source bytes must be the fresh native-preflight bundle, not an arbitrary
 * reread of local metadata. prepared is that preflight's fresh-byte result.
 * Output borrows selection/config/binding/guard until open has completed.
 * A valid output binds data only; it never grants launch or first-read safety. */
int bw_reader_asset_select(struct bw_reader_asset_selection *,
    const struct bw_reader_selection *,const struct bw_share_config *,
    const struct bw_source_binding *,const struct bw_native_prepared *,
    const void *source,size_t source_size,const void *selected,size_t selected_size,
    int (*current)(void *),void *);

/* Source/cache owner for the existing reader. Borrows one connected SMB
 * context and the selected file-map bytes. No game-memory, mount, launch,
 * allocation-in-another-process, or staging-file operations. Zero-initialize,
 * keep at a stable address, serialize calls, and close BEFORE the map/context.
 * The planner must fully validate the canonical RAW pack before publication.
 */
struct bw_reader_asset {
    struct bw_reader_asset_smb source;
    struct bw_asset_bridge bridge;
    void *source_map, *volume_map;
    size_t metadata_bytes;
    uint64_t read_bytes, hit_bytes;
    int ready;
    int (*_current)(void *);
    void *_current_opaque;
    const char *error;
};

/* root is the exact canonical prepared directory (without trailing slash).
 * Independent hashes come from a fixed-size, run-bound selection sidecar.
 * All local inputs are opened read-only with named/held identity checks.
 * Errors free allocations and handles; an explicit close is required before
 * reopening. The retained cache is exactly the existing 64 MiB cache.
 */
int bw_reader_asset_open(struct bw_reader_asset *, struct smb2_context *, const char *root,
    const struct bw_reader_asset_selection *, const void *selected_map, size_t map_size);
int bw_reader_asset_read(struct bw_reader_asset *, uint32_t file_id, uint64_t offset,
    void *output, size_t length, size_t capacity);
int bw_reader_asset_close(struct bw_reader_asset *);
/* Private fixed-file staging only. A successful read inside begin/commit is
 * provisional: commit must pass before forwarding a native request. Abort
 * poisons the source on any intervening read/write/ownership failure. */
int bw_reader_asset_stage_begin(struct bw_reader_asset *);
int bw_reader_asset_stage_commit(struct bw_reader_asset *);
void bw_reader_asset_stage_abort(struct bw_reader_asset *);
const char *bw_reader_asset_error(const struct bw_reader_asset *);
#endif
