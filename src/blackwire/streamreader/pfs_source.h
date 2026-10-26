#ifndef BW_PFS_SOURCE_H
#define BW_PFS_SOURCE_H
#include <stddef.h>
#include <stdint.h>
#include "manifest.h"

#define BW_PFS_INODE_BYTES 168u
#define BW_PFS_MAX_INODES 131072u
#define BW_PFS_MAX_DEPTH 32u
#define BW_PFS_MAX_DIRECTORY (1024u * 1024u)
#define BW_PFS_MAX_VOLUME (8ULL << 40)

typedef int (*bw_pfs_pread_fn)(void *, uint64_t, void *, size_t);
enum bw_pfs_result {
    BW_PFS_OK=0, BW_PFS_ARGUMENT=-1, BW_PFS_FORMAT=-2, BW_PFS_IO=-3,
    BW_PFS_UNSUPPORTED=-4, BW_PFS_RANGE=-5, BW_PFS_LIMIT=-6,
    BW_PFS_MEMORY=-7, BW_PFS_PATH=-8, BW_PFS_NOT_FOUND=-9,
    BW_PFS_CALLBACK=-10
};
struct bw_pfs_inode {
    uint64_t base, stored_size, logical_size;
    uint32_t id, blocks, flags;
    uint16_t mode, links;
};
struct bw_pfs {
    bw_pfs_pread_fn pread;
    void *source;
    uint64_t size, blocks, inode_blocks, cached_page;
    uint32_t block_size, inode_count, root, flat_table, collisions;
    int ready, namespace_validated, cache_valid, case_insensitive;
    size_t cached_bytes;
    uint8_t cache[4096];
};
/* Immutable source generation and serialized calls are owned by the caller.
 * No extraction, writes, mounts or process operations. D32 unsigned contiguous
 * PFS v2 only; signed/encrypted/64-bit/scattered variants fail explicitly.
 * open validates all inode extents, including overlap. walk validates names,
 * aliases, cycles, types, dot entries and case-folded sibling collisions.
 * Path callbacks are borrowed only for that call; discard partial results if
 * walk fails. Up to 4 MiB temporary name slots plus bounded traversal metadata.
 */
typedef int (*bw_pfs_visit_fn)(void *, const char *, const struct bw_pfs_inode *);
int bw_pfs_open(struct bw_pfs *,bw_pfs_pread_fn,void *,uint64_t);
int bw_pfs_inode_get(struct bw_pfs *,uint32_t,struct bw_pfs_inode *);
int bw_pfs_walk(struct bw_pfs *,bw_pfs_visit_fn,void *,uint32_t *);
/* Separate typed namespace observation: emits directories, including empty,
 * as well as files. Existing file-only walk/count behavior is unchanged. */
int bw_pfs_walk_nodes(struct bw_pfs *,bw_pfs_visit_fn,void *,uint32_t *);
int bw_pfs_find(struct bw_pfs *,const char *,struct bw_pfs_inode *);
int bw_pfs_read_file(struct bw_pfs *,uint32_t,uint64_t,void *,size_t);
const char *bw_pfs_error(int);
#endif
