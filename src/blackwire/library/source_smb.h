#ifndef BW_SOURCE_SMB_H
#define BW_SOURCE_SMB_H
#include "source_plan.h"
#include "source_snapshot.h"
struct smb2_context;
enum bw_source_phase {
    BW_SOURCE_IDLE=0,BW_SOURCE_SCANNING,BW_SOURCE_METADATA,BW_SOURCE_HASHING,
    BW_SOURCE_RECHECKING,BW_SOURCE_VERIFIED,BW_SOURCE_PREFLIGHT,BW_SOURCE_COPYING,
    BW_SOURCE_SPARSE,BW_SOURCE_INDEXING,BW_SOURCE_INDEX_VERIFY,BW_SOURCE_MEASURING,
    BW_SOURCE_PREPARED,BW_SOURCE_ERROR,BW_SOURCE_PHASE_COUNT
};
/* Numeric progress only: no source paths, credentials, SDK objects or borrowed
 * asset buffers. Totals are zero while unknown. File/byte counters describe
 * this phase; request/entry/read counters cover the whole owned operation. */
struct bw_source_progress {
    enum bw_source_phase phase;
    uint32_t files_done,files_total,requests,entries,pages,phase_entries;
    uint64_t bytes_done,bytes_total,source_bytes;
    double monotonic_seconds;
};
typedef void (*bw_source_observer_fn)(void *,const struct bw_source_progress *);
struct bw_source_smb {
    struct smb2_context *smb;
    double deadline;
    const char *error;
    uint32_t last_status;
    uint32_t requests,pages,directories,entries,peak_held_pages;
    uint64_t metadata_bytes;
    uint64_t content_bytes;
    bw_source_observer_fn observe;
    void *observe_opaque;
    struct bw_source_progress progress;
    uint32_t phase_entry_base;
};
/* Optional synchronous observer, called on the worker outside SDK callbacks.
 * It must only copy/coalesce progress; do not reenter the connection, change
 * the operation or retain the snapshot pointer. Installing it grants no write
 * or cancellation capability. It may feed a separately synchronized UI cache. */
const char *bw_source_phase_name(enum bw_source_phase);
void bw_source_smb_progress(struct bw_source_smb *,enum bw_source_phase,uint32_t,uint32_t,uint64_t,uint64_t);
/* The caller owns a connected context, grants this scan exclusive use of it,
 * and sets a monotonic deadline. A timeout/transport failure destroys that
 * context and sets smb=NULL before stack callbacks can leave scope. */
int bw_source_smb_plan(struct bw_source_smb *,const char *game,struct bw_source_map *);
int bw_source_smb_snapshot(struct bw_source_smb *,const char *game,struct bw_source_map *,struct bw_source_snapshot *);
/* Full share-relative source -> independent safe console game component.
 * Holds each collection parent; both paths and parent generations are bound
 * into the sealed snapshot. This does not register or launch a title. */
int bw_source_smb_snapshot_bound(struct bw_source_smb *,const char *source,const char *game,struct bw_source_map *,struct bw_source_snapshot *);
/* Whole existing file, opened read-only with reparse points rejected. Holds and
 * verifies each parent. Expected metadata must come from a sealed snapshot.
 * The consumer is synchronous and borrows at most 64 KiB per call. No callback
 * is invoked until the held file matches that snapshot. */
typedef int (*bw_source_consume_fn)(void *,const void *,size_t);
int bw_source_smb_read(struct bw_source_smb *,const struct bw_source_snapshot *,const char *path,bw_source_consume_fn,void *);

#define BW_SOURCE_RANGE_LIMIT (16u*1024u*1024u)
#define BW_SOURCE_RANGE_MAX_READ_CALLS 4096u
/* Required, nonreentrant native job/cancellation guard: zero means current.
 * It must not call provider_current, reenter SMB or mutate the borrowed snapshot.
 * Callbacks are synchronous; deadline checks cannot preempt a blocking callback. */
typedef int (*bw_source_range_current_fn)(void *);
/* Exact protocol-sized uint64 range of an existing regular file in an owned,
 * collection-bound native snapshot. Zero length is checked (including EOF and
 * empty files); positive chunks are <=64 KiB. At most 16 MiB per call, with no
 * signed narrowing. Whole-file API semantics above are unchanged.
 * Consumer data is provisional until zero success; discard all data on -1.
 * All callbacks (current, consumer and any prior observer) must avoid source,
 * SDK and provider reentry and must not mutate source/snapshot/provider state. This API
 * does not secure hostile in-process callbacks or arbitrary invalid pointers.
 * This checks held file/parents, not siblings or the whole provider generation.
 * A future policy owner must check whole-source/job authority and cumulative
 * budgets before publication. A checksum or HOST witness is never a capability. */
int bw_source_smb_read_range(struct bw_source_smb *,const struct bw_source_snapshot *,const char *,
    uint64_t offset,uint64_t length,bw_source_range_current_fn,void *job,bw_source_consume_fn,void *consumer);
enum bw_source_lookup {BW_SOURCE_FOUND=0,BW_SOURCE_MISSING=1,BW_SOURCE_UNAVAILABLE=2,BW_SOURCE_TOO_LARGE=3};
#define BW_SOURCE_SMALL_FILE_LIMIT (4u*1024u*1024u)
/* Catalog-only operations. They do not grant a preparation capability or scan
 * asset trees. A missing/denied/linked item returns a positive result without
 * poisoning the connection; transport/protocol/timeout failures return -1.
 * Paths are safe share-relative UTF-8; only list() accepts the empty share root.
 * list callbacks run outside SDK callbacks and may synchronously use lookup.
 * Buffers returned by small_file() are owned by the caller and must be freed. */
int bw_source_smb_list(struct bw_source_smb *,const char *,bw_source_visit_fn,void *);
int bw_source_smb_file_info(struct bw_source_smb *,const char *,struct bw_source_info *);
int bw_source_smb_small_file(struct bw_source_smb *,const char *,size_t,void **,size_t *,struct bw_source_info *);
#endif
