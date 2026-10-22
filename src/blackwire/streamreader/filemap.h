#ifndef BW_FILEMAP_H
#define BW_FILEMAP_H

#include "manifest.h"

#define BW_FILES_HANDLE_LIMIT 16u
#define BW_FILES_PATH_CAPACITY 1024u
#define BW_FILES_MAX_MANIFEST_BYTES (40u * 1024u * 1024u)

struct smb2_context;
struct smb2fh;
struct bw_file_identity;

struct bw_files_remote_stat {
    uint64_t size, mtime_sec, inode;
    int regular;
};
/* Host-test/provider seam. close() consumes its handle even when it reports an
 * error. Operations and handles are strictly synchronous, with no outstanding
 * read when requesting another handle, closing, or destroying the map. */
struct bw_files_remote_ops {
    int (*stat)(void *, const char *, struct bw_files_remote_stat *);
    void *(*open_readonly)(void *, const char *);
    int (*fstat)(void *, void *, struct bw_files_remote_stat *);
    int (*close)(void *, void *);
};
struct bw_files_handle { void *handle; uint64_t age; uint32_t file_id; };
struct bw_files {
    struct bw_manifest manifest;
    uint32_t stream_count, handle_count;
    uint64_t handle_hits, handle_opens, handle_evictions;
    size_t allocation_bytes;
    /* Internal fields: do not modify. Zero-initialize before first load. */
    void *_bytes;
    struct bw_file_identity *_identities;
    char _root[BW_FILES_PATH_CAPACITY], _prefix[256];
    uint64_t _local_device, _game_device, _clock;
    int _latched, _remote_bound, _remote_failed;
    void *_remote_context;
    struct bw_files_remote_ops _ops;
    struct bw_files_handle _handles[BW_FILES_HANDLE_LIMIT];
    const char *_error;
};

enum bw_files_match { BW_FILES_ERROR = -1, BW_FILES_LOCAL = 0, BW_FILES_MATCH = 1 };

/* Root must be an existing exact absolute canonical directory, without a final
 * slash. manifest_path is either its basename or an exact canonical full path
 * beneath root. Validates all local record paths/sizes, indexes STREAM objects
 * only, requires LOCAL loader files to have execute permissions, and closes
 * local descriptors before returning. Existing bytes are never
 * modified. A failed load frees allocations; destroy before loading again. */
int bw_files_load(struct bw_files *files, const char *root, const char *manifest_path);
void bw_files_destroy(struct bw_files *files);
uint32_t bw_files_count(const struct bw_files *files);
uint64_t bw_files_checksum(const struct bw_files *files);
/* Immutable borrowed manifest bytes; valid until destroy, no allocation. */
const void *bw_files_manifest_bytes(const struct bw_files *files, size_t *size);
int bw_files_get(const struct bw_files *files, uint32_t file_id, struct bw_manifest_record *record);
const char *bw_files_path(const struct bw_files *files, uint32_t file_id);
int bw_files_first_stream(const struct bw_files *files, uint32_t *file_id);

/* Caller verifies S_ISREG on game-thread fstat output. Latch from a known
 * STREAM path opened in the game's namespace, then close that probe FD.
 * Repeated latches must agree. Unknown objects return LOCAL; a known inode with
 * wrong size/device or without a verified latch returns ERROR. No FD is cached. */
int bw_files_latch_device(struct bw_files *files, uint64_t device, uint64_t inode,
                          uint64_t size, uint32_t expected_file_id);
enum bw_files_match bw_files_lookup(struct bw_files *files, uint64_t device,
                                    uint64_t inode, uint64_t size, uint32_t *file_id);

/* Borrowed handle; valid only until the next remote_get/destroy call. Connect
 * the SMB context first. This map binds once to context+prefix; it cannot be
 * reused with another share/game. Destroy the map BEFORE destroying SMB.
 * A cache hit reuses a validated immutable-source handle. Each new open checks
 * stat and opened-file fstat for exact size and Unix mtime seconds. */
struct smb2fh *bw_files_remote_get(struct bw_files *files, struct smb2_context *smb,
                                  uint32_t file_id, const char *game_prefix);
void *bw_files_remote_get_ops(struct bw_files *files, const struct bw_files_remote_ops *ops,
                              void *context, uint32_t file_id, const char *game_prefix);
const char *bw_files_error(const struct bw_files *files);

#endif
