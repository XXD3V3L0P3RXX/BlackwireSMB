/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_PRELOAD_SOURCE_H
#define BW_PRELOAD_SOURCE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct bp_source;
enum bp_connect_stage {
    BP_CONNECT_NONE=0,BP_CONNECT_STATE=1,BP_CONNECT_INITIAL_CURRENT=2,
    BP_CONNECT_CACHE=3,BP_CONNECT_CONTEXT=4,BP_CONNECT_SHARE=5,BP_CONNECT_FINAL_CURRENT=6,
    BP_CONNECT_SCRATCH=7
};
/* Host-only controlled candidate: same-request cold-run cap. Negotiated
 * server maximum constrains each transport call; cache entries remain64KiB. */
#define BP_COALESCE_BYTES (1024u*1024u)
struct bp_connect_diagnostic {
    unsigned stage,options_returned;
    int raw,error;
    uint32_t ntstatus,ntstatus_valid;
};
struct bp_read_witness {
    uint64_t offset,requested,returned,digest_offset,fetched_payload_bytes;
    uint32_t map_id,digest_size,valid;
    uint8_t digest_sha256[32];
};
struct bp_io_performance {uint64_t calls,nanoseconds;};
/* Fixed-size worker-owned aggregates only; no paths, credentials or samples.
 * Read timing is inclusive of all metadata/payload/copy work. Paired metadata
 * operation times overlap and must not be summed as elapsed wall time. Counts
 * include failed attempts.
 * completed/cache-hit bytes count only reads passing their final identity and
 * cancellation fences; payload/cache counters include provisional failed work.
 * Additional counters saturate rather than changing source behavior. Clock faults only
 * increment clock_failures and omit that sample; no timing is authority. */
struct bp_source_performance {
    uint64_t reads,completed_reads,failed_reads,canceled_reads,requested_bytes,completed_bytes,cache_hit_bytes;
    uint64_t payload_bytes,cache_hits,cache_misses,cache_evictions,cache_resident;
    uint64_t coalesced_runs,coalesced_blocks,scratch_bytes,largest_pread_request,clock_failures;
    struct bp_io_performance named_stat,held_stat,open,close,pread,read;
};
enum bp_source_stage {
    BP_SOURCE_NONE=0,BP_SOURCE_ENTRY_NAMED=1,BP_SOURCE_OPEN=2,BP_SOURCE_ENTRY_HELD=3,
    BP_SOURCE_PREAD=4,BP_SOURCE_FINAL_NAMED=5,BP_SOURCE_FINAL_HELD=6,
    BP_SOURCE_EVICT_CLOSE=7,BP_SOURCE_DISCONNECT_CLOSE=8,
    BP_SOURCE_ENTRY_METADATA=9,BP_SOURCE_ENTRY_IDENTITY=10,
    BP_SOURCE_FINAL_METADATA=11,BP_SOURCE_FINAL_IDENTITY=12,
    BP_SOURCE_SCOPE=13,BP_SOURCE_MAX_READ=14,BP_SOURCE_COUNTER=15,BP_SOURCE_FINAL_PATH=16,
    BP_SOURCE_CONTAINER=17
};
struct bp_source_failure {
    uint64_t sequence;int64_t raw;int error;
    unsigned valid,stage,error_observed,ntstatus_observed;uint32_t ntstatus;
};
struct bp_logical_failure {
    uint64_t offset,requested;int64_t result;uint32_t map_id;
    unsigned valid,poisoned;
};
struct bp_source_progress {
    uint64_t sequence;unsigned stage,in_flight,poisoned;
    int64_t raw;int error;unsigned error_observed;
    struct bp_source_failure first_poison;
    struct bp_logical_failure first_failed_read;
};
/* Trusted same-DATA-worker observer only. Snapshots contain fixed numeric
 * scalars, no paths/credentials. Beginning a call is not completion evidence.
 * errno is captured immediately but SMB may leave it stale. NTSTATUS is the
 * last value retained by the current context, not guaranteed newly supplied
 * by this failing operation. Observation flags mean a value was sampled.
 * No observer may mutate/reenter the source or retain these borrowed views. */
typedef void (*bp_source_progress_fn)(void *,const struct bp_source_progress *,const struct bp_source_performance *);
void bp_source_observe(struct bp_source *,bp_source_progress_fn,void *);
void bp_source_progress(const struct bp_source *,struct bp_source_progress *);
typedef int (*bp_current_fn)(void *); /* Pure; zero means current. */
/* Bounded metadata-only admission. No cache, SMB context, network or thread
 * creation. Immutable map/selection are copied; explicit credentials are kept
 * privately only until connect/close and never exposed in a view. Folder and
 * PFS/PFSC container selections bind distinct explicit provider identities;
 * a container's map retains its preparation-time image mtime. */
int bp_source_admit(struct bp_source **,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,
    bp_current_fn,void *);
/* Single worker only: create its actual SMB context/16 handles/64MiB cache.
 * This candidate adds one bounded1MiB scratch allocation outside the cache.
 * No retry: failure poisons the admitted session. Credentials are cleared on
 * every result. This is a DATA session, not a receipt or launcher authority. */
int bp_source_connect(struct bp_source *);
/* Same one-shot worker connection using caller-owned exact64MiB cache payload.
 * Caller keeps it exclusive/alive through disconnect and all pending SMB work.
 * Cache stage raw-1 means invalid backing, raw-2 entry calloc failure.
 * Normal connect/open keeps its original malloc-backed path. */
int bp_source_connect_with_storage(struct bp_source *,void *,size_t);
/* CPU-decoded containers must not outrank the PS5 dialog/UI threads. This
 * worker-only choice observes admitted immutable state and performs no I/O.
 * Folder sources retain the upstream I/O worker priority. */
int bp_source_worker_priority(const struct bp_source *,int upstream);
/* Single-worker scalar snapshot only: first failed existing operation survives
 * cleanup. Context NULL is raw0; state is a predicate bit mask with errno0.
 * Options are void setters: count returned calls, never checked success.
 * Immediate errno can be stale; no error strings or credential bytes. */
void bp_source_connect_diagnostic(const struct bp_source *,struct bp_connect_diagnostic *);
/* Convenience composition used by focused host transport tests. */
int bp_source_open(struct bp_source **,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,
    bp_current_fn,void *);
/* Immutable lookup, safe after publication and before joined destruction.
 * Exact /app0/ path and size required. LOCAL/missing returns0, STREAM1,
 * recognized size/path conflict returns-1. IDs are BWMAP record indices. */
int bp_source_match(const struct bp_source *,const char *,size_t,uint64_t,uint32_t *);
/* Worker-only. EOF clamps; caller buffers are provisional on failure. This
 * returns exact logical bytes or negative POSIX error, never physical bytes.
 * Consecutive cold blocks in this request are staged in at most1MiB runs.
 * Every payload call is clamped to the negotiated server limit. Positive short
 * replies complete the run before any block publication. No cross-request
 * speculation is active. Cache validity remains64KiB. STRICT keeps remote
 * entry/final identity checks even on cache hits. Independent named/held checks
 * are queued as a pair; both complete before comparison and publication.
 * Explicit experimental ON_OPEN
 * checks new/reopened handles, assumes immutable files, and uses current/exact-slot
 * RAM checks for admitted reads. It does not retain per-read mutation detection.
 * Transport failures
 * destroy the context before any provisional cache storage can be reused. */
int64_t bp_source_read(struct bp_source *,uint32_t,uint64_t,void *,size_t);
/* Worker-only staged diagnostics. Digest consumes at most64KiB of owned cache
 * bytes BEFORE copying. Entire witness stays invalid on zero/error/cancel or
 * failed final source fence. No caller-buffer read or publication authority. */
int64_t bp_source_read_observed(struct bp_source *,uint32_t,uint64_t,void *,size_t,struct bp_read_witness *);
int bp_source_disconnect(struct bp_source *); /* worker: drain transport/cache, retain immutable map */
int bp_source_close(struct bp_source *); /* only after all worker reads drain */
uint64_t bp_source_network_bytes(const struct bp_source *);
uint64_t bp_source_cache_hits(const struct bp_source *);
/* Same worker only; snapshot before disconnect destroys cache counters. */
void bp_source_performance(const struct bp_source *,struct bp_source_performance *);
#ifdef __cplusplus
}
#endif
#endif
