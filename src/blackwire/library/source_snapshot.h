#ifndef BW_SOURCE_SNAPSHOT_H
#define BW_SOURCE_SNAPSHOT_H
#include "source_directory.h"
#include "../streamreader/manifest.h"
#define BW_SOURCE_SNAPSHOT_MAX_ENTRIES 262144u
#define BW_SOURCE_SNAPSHOT_MAX_PATH_BYTES (64u*1024u*1024u)
#define BW_SOURCE_SNAPSHOT_MAX_PARENTS 49u
struct bw_source_snapshot_entry;
struct bw_source_snapshot {
    char game[241];
    char source[BW_MANIFEST_MAX_PATH+1];
    struct bw_source_info parents[BW_SOURCE_SNAPSHOT_MAX_PARENTS];
    unsigned parent_count;
    struct bw_source_snapshot_entry *entries;
    char *paths;
    size_t count,capacity,path_size,path_capacity;
    uint8_t sha256[32];
    unsigned sealed;
    const char *error;
};
int bw_source_snapshot_init(struct bw_source_snapshot *,const char *game);
/* A bounded full share path and a separate safe console component. The SMB
 * owner records/holds every collection parent before sealing this snapshot. */
int bw_source_snapshot_init_bound(struct bw_source_snapshot *,const char *source,const char *game);
/* Includes directories and empty directories. Only the selected root directory
 * may have an empty path. Metadata is copied, never borrowed from an SMB page. */
int bw_source_snapshot_add(struct bw_source_snapshot *,const char *,const struct bw_source_info *);
int bw_source_snapshot_finish(struct bw_source_snapshot *);
const struct bw_source_info *bw_source_snapshot_find(const struct bw_source_snapshot *,const char *);
int bw_source_snapshot_equal(const struct bw_source_snapshot *,const struct bw_source_snapshot *);
void bw_source_snapshot_destroy(struct bw_source_snapshot *);
/* Const integrity check of the existing seal and bounded owned representation.
 * Caller must own valid addressable native allocations throughout this call;
 * arbitrary invalid C pointers cannot be validated. A matching hash grants no
 * job/source capability and cannot admit a HOST stat witness as native memory. */
int bw_source_snapshot_validate(const struct bw_source_snapshot *);
/* Borrowed sealed metadata, INCLUDING directories/root and empty directories.
 * Validates the complete seal before and after callbacks. Callback must not
 * mutate/reenter; this data-only visit never establishes live SMB authority. */
int bw_source_snapshot_visit(const struct bw_source_snapshot *,bw_source_visit_fn,void *);
#endif
