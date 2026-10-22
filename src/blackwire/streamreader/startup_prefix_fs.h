#ifndef BW_STARTUP_PREFIX_FS_H
#define BW_STARTUP_PREFIX_FS_H
#include "startup_prefix.h"
#include <sys/stat.h>

struct bw_prefix_file_ops {
    /* Must confirm the selected generation, exclusive preparation ownership,
     * deadline and no active game/reader. Unknown state must return nonzero. */
    int (*live)(void *);
    /* Durable journal entry bound to plan generation + file ID, including the
     * pinned inode/device, exact full size and source-prefix digest. */
    int (*note)(void *,enum bw_prefix_note,const struct stat *,uint64_t,uint64_t);
};
struct bw_prefix_file_result {
    struct bw_prefix_result copy;
    uint64_t allocated_before,allocated_after;
    int opened,allocation_after_known;
};
/* Read-only preflight, preserving the selected inode generation for apply. */
int bw_prefix_file_inspect(const char *root_path,int root_fd,
    const struct bw_manifest_record *,struct stat *selected);
/* Ordinary filesystem adapter, no network, game launch, memory hooks, resize,
 * delete or rename. Root is an already-held canonical selected game directory.
 * Validate complete manifest/plan and global allocation budget before calling.
 * Acquires an exclusive nonblocking lock on the selected target and verifies
 * canonical parents, single link, same filesystem, identity, size and mode
 * before every write/readback. Existing allocated content is refused.
 * Production roots are restricted to /data/blackwire/games/<single component>.
 */
struct bw_prefix_file_result bw_prefix_file_apply(const char *root_path,int root_fd,
    const struct bw_manifest_record *,const struct stat *selected,
    const void *source,size_t length,
    const uint8_t expected_sha[32],const struct bw_prefix_file_ops *,void *opaque);
#endif
