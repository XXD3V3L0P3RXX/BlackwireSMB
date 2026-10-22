#ifndef BW_ASSET_VOLUME_SMB_H
#define BW_ASSET_VOLUME_SMB_H
#include "asset_volume.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#define BW_ASSET_SMB_HANDLES 16u
#define BW_ASSET_SMB_PATH 1024u
#define BW_ASSET_SMB_MAX_IDENTITIES (32u*1024u*1024u)
#define BW_ASSET_SMB_STAGE_FILES 8192u
struct bw_asset_smb_handle { struct smb2fh *handle;uint32_t file_id;uint64_t age; };
/* Zero-initialize before open. Owns at most 16 read-only SMB handles and a
 * bounded per-file identity table; borrows the connected context and immutable
 * manifest/map input bytes. Keep at a stable address, serialize all calls and
 * close before rebinding. Named/held type, inode, size and precise mtime/ctime
 * are checked before/after reads, including generation guards for cache hits.
 * Metadata checks presume a coherent trusted SMB server, not malicious content
 * authentication. No source writes, mount, process or game-memory operations.
 */
struct bw_asset_smb {
    struct bw_asset_volume volume;
    struct bw_asset_ops ops;
    struct smb2_context *smb;
    struct smb2_stat_64 *_admitted;
    struct bw_asset_smb_handle handles[BW_ASSET_SMB_HANDLES];
    char prefix[256];
    uint64_t binding_id,clock,read_calls,read_bytes,generation_checks,opens,evictions;
    uint64_t read_limit; /* Optional total network-byte cap; zero disables it. */
    size_t identity_bytes;
    /* One bounded private staging transaction. Bytes read while active must
     * not reach the game until commit rechecks every touched source. Normal
     * reads outside a transaction retain their per-block generation checks. */
    uint8_t stage_seen[BW_MANIFEST_MAX_FILES/8];
    uint32_t stage_ids[BW_ASSET_SMB_STAGE_FILES],stage_count;
    uint64_t stage_binding;
    int stage_active;
    int initialized,failed,last_transport_error;
};
int bw_asset_smb_open(struct bw_asset_smb *,struct smb2_context *,const char *game_prefix,
    const void *map,size_t map_size,const uint8_t map_sha[32],
    const void *manifest,size_t manifest_size,const uint8_t apr_sha[32],const void *header,size_t header_size);
int bw_asset_smb_read(struct bw_asset_smb *,uint64_t offset,void *,size_t length,size_t capacity);
int bw_asset_smb_guard(struct bw_asset_smb *,uint64_t offset,size_t length);
int bw_asset_smb_close(struct bw_asset_smb *);
int bw_asset_smb_stage_begin(struct bw_asset_smb *);
int bw_asset_smb_stage_commit(struct bw_asset_smb *);
void bw_asset_smb_stage_abort(struct bw_asset_smb *);
#endif
