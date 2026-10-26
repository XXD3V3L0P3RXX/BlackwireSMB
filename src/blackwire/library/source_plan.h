#ifndef BW_SOURCE_PLAN_H
#define BW_SOURCE_PLAN_H
#include <stddef.h>
#include <stdint.h>
#include "../streamreader/manifest.h"

/* Metadata-only planner. No network, source writes, console writes, title
 * registration or gameplay claims. One caller owns each plan until destroy. */
struct bw_source_entry;
enum bw_source_plan_policy {BW_SOURCE_PLAN_LEGACY=0,BW_SOURCE_PLAN_COMPANIONS_V1=1};
struct bw_source_plan {
    char game[241];
    struct bw_source_entry *entries;
    char *paths;
    size_t entries_capacity,paths_capacity,paths_size;
    uint32_t count;
    unsigned sealed;
    enum bw_source_plan_policy policy;
    const char *error;
};
struct bw_source_map {
    uint8_t *bytes;
    size_t size;
    uint64_t local_bytes,remote_bytes,bootstrap_bytes;
    uint32_t local_count,stream_count;
};
int bw_source_plan_init(struct bw_source_plan *,const char *game);
/* Explicit NEW-generation planning only. Does not rewrite an existing map,
 * receipt or provider identity. The caller must use the separate projection
 * policy before any prepared-generation publication. */
int bw_source_plan_init_policy(struct bw_source_plan *,const char *game,enum bw_source_plan_policy);
/* Under COMPANIONS_V1 validate every source node BEFORE file hashing, including
 * empty directories. Exact roots are fakelib/fakelib2; aliases, file roots and
 * reserved Blackwire material names refuse. Legacy node checks are unchanged. */
int bw_source_plan_node(struct bw_source_plan *,const char *,int directory);
/* 0=unrelated, 1/2=canonical companion namespace, -1=case/token alias. */
int bw_source_companion_path_namespace(const char *);
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
