#ifndef BW_SOURCE_PLAN_H
#define BW_SOURCE_PLAN_H
#include <stddef.h>
#include <stdint.h>
#include "../streamreader/manifest.h"

/* Metadata-only planner. No network, source writes, console writes, title
 * registration or gameplay claims. One caller owns each plan until destroy. */
struct bw_source_entry;
struct bw_source_plan {
    char game[241];
    struct bw_source_entry *entries;
    char *paths;
    size_t entries_capacity,paths_capacity,paths_size;
    uint32_t count;
    unsigned sealed;
    const char *error;
};
struct bw_source_map {
    uint8_t *bytes;
    size_t size;
    uint64_t local_bytes,remote_bytes,bootstrap_bytes;
    uint32_t local_count,stream_count;
};
int bw_source_plan_init(struct bw_source_plan *,const char *game);
/* Return 0 for an admitted file, 1 for reserved/backup metadata excluded by the
 * existing planner, -1 for an error. Any error poisons this plan. */
int bw_source_plan_add(struct bw_source_plan *,const char *path,uint64_t size,uint64_t mtime);
int bw_source_plan_finish(struct bw_source_plan *,struct bw_source_map *);
void bw_source_plan_destroy(struct bw_source_plan *);
void bw_source_map_destroy(struct bw_source_map *);
/* Also used before descending directories. The input is NUL-terminated UTF-8;
 * callers must separately validate non-excluded filesystem components. */
int bw_source_excluded(const char *path);
/* Generic bounded startup metadata: case-insensitive ASCII initfs or
 * initfs_<platform> leaf ([A-Za-z0-9_-]+), or a nonempty .cat catalog leaf.
 * Invalid/excluded paths and files above the existing 64 MiB sidecar limit
 * return zero. This classification grants no source/copy/readiness authority. */
int bw_source_startup_metadata(const char *path,uint64_t size);
#endif
