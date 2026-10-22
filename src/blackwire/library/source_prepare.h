#ifndef BW_SOURCE_PREPARE_H
#define BW_SOURCE_PREPARE_H
#include "source_smb.h"
struct bw_source_preparation {
    struct bw_source_map map;
    struct bw_source_snapshot snapshot;
    uint8_t *digests;
    size_t digest_size;
    uint8_t profile_sha256[32];
    char title[10],selection[2048];
    uint32_t hashed_files;
    uint64_t hashed_bytes;
    unsigned verified;
};
/* Build the existing manifest + BWDIG001 startup digests directly from SMB.
 * The caller owns the connection and bundle. No source/console writes, hooks,
 * registration, process control or game launch are performed. */
int bw_source_prepare(struct bw_source_smb *,const char *game,const char *run,struct bw_source_preparation *);
/* Native-controller path: exact share key, separate console component and
 * expected validated catalog title. A changed title cannot yield a bundle. */
int bw_source_prepare_bound(struct bw_source_smb *,const char *source,const char *game,const char *title,const char *run,struct bw_source_preparation *);
/* Full namespace and generation comparison, not a per-file HTTP lease. */
int bw_source_prepare_current(struct bw_source_smb *,const struct bw_source_preparation *);
void bw_source_prepare_destroy(struct bw_source_preparation *);
#endif
