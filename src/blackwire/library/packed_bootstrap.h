#ifndef BW_PACKED_BOOTSTRAP_H
#define BW_PACKED_BOOTSTRAP_H
#include "source_smb.h"
#define BW_PBB_VERSION 2u
/* Transport-accounting revision; canonical facts/wire remain version 2. */
#define BW_PBB_METERING_VERSION 4u
/* Typed non-success after all admitted indexes + final fresh source fence. */
#define BW_PBB_NOT_APPLICABLE 1
#define BW_PBB_POLICY "plain-pak11-bootstrap-cache-v2"
#define BW_PBB_PREFIX "Engine/Content/Internationalization/"
#include "packed_bootstrap_rules.h"
#define BW_PBB_MAX_PATH 240u
#define BW_PBB_MAX_ARCHIVES 64u
#define BW_PBB_MAX_FILES 4096u
#define BW_PBB_MAX_INDEX (8u*1024u*1024u)
#define BW_PBB_MAX_ARCHIVE_ENTRIES 262144u
#define BW_PBB_MAX_BOOTSTRAP UINT64_C(67108864)
#define BW_PBB_MAX_STARTUP UINT64_C(5368709120)
#define BW_PBB_MAX_READ_BYTES UINT64_C(268435456)
#define BW_PBB_MAX_REQUESTS 1000000u
#define BW_PBB_MAX_BODY_WORK (3u*BW_PBB_MAX_BOOTSTRAP)
#define BW_PBB_MAX_METADATA_SEED UINT64_C(16777216)
#define BW_PBB_LOCAL 1u
enum bw_pbb_span_kind { BW_PBB_FOOTER, BW_PBB_PRIMARY, BW_PBB_PATH_HASH, BW_PBB_FILENAMES, BW_PBB_SPANS };
struct bw_pbb_span {uint64_t offset,size;uint8_t sha256[32],sha1[20];};
struct bw_pbb_archive {
    char path[241];struct bw_source_info info;
    struct bw_pbb_span spans[BW_PBB_SPANS];
    uint32_t filename_count,directory_count;uint64_t hash_seed;
};
struct bw_pbb_entry {
    char destination[241];uint32_t archive_index,entry_location,flags;
    uint64_t header_offset,body_offset,size;
    uint8_t header_sha256[32],body_sha1[20],body_sha256[32];
};
struct bw_pbb_view {
    uint32_t version;uint64_t epoch; /* captured caller epoch, not authority */
    char source[BW_MANIFEST_MAX_PATH+1],game[241],prefix[sizeof BW_PBB_PREFIX];
    uint8_t native_snapshot_sha256[32];
    const struct bw_pbb_archive *archives;const struct bw_pbb_entry *entries;
    uint32_t archive_count,entry_count;uint64_t bootstrap_bytes,base_local_bytes;
};
struct bw_pbb_stats {
    uint64_t accepted_source_bytes,range_bytes,body_bytes; /* lifetime / own ranges / body work */
    uint32_t source_requests,range_chunks,current_checks,fences,delivered_files;
    uint64_t emitted_bytes;uint32_t emitted_chunks,range_calls,capture_range_calls,final_range_calls;
    int finished;
    uint64_t cache_owned_source_bytes,external_source_bytes;
    uint32_t cache_owned_requests,external_requests;
};
/* Whole-provider/source-generation fence; zero=current. Called only outside
 * pending SMB operations, at capture/final boundaries. It may synchronously
 * call the actual provider-current API on THIS owned source. It must use its
 * established deadline/observer discipline, not replace source/snapshot/context.
 * It grants no authority: the caller must own the real native job/provider. */
typedef int (*bw_pbb_fence_fn)(void *);
struct bw_pbb_request {
    uint32_t version;uint64_t epoch; /* exactly 2, nonzero real caller epoch */
    struct bw_source_smb *source;const struct bw_source_snapshot *snapshot;
    bw_source_range_current_fn current;void *job; /* pure, nonreentrant */
    bw_pbb_fence_fn fence;void *fence_opaque;
    uint32_t source_kind,source_policy;const char *profile; /* FOLDER/1/none */
    const char *prefix; /* explicit mode1..5 declaration; never inferred */
    const char *const *archives;uint32_t archive_count; /* sorted literal paths */
    uint64_t base_local_bytes,startup_budget; /* <=5 GiB, not allocation proof */
    /* Cache-owned transport while the scoped observer is held, including its
     * open/finish outer fences. External original-copy/provider traffic is
     * reported separately and must be bounded by the actual native controller.
     * No lifetime source counter is reset or given an arbitrary allowance. */
    uint64_t read_byte_limit;uint32_t request_limit; /* positive, <=fixed own caps */
};
struct bw_packed_bootstrap {void *state;const char *error;unsigned busy,failed;};
/* Zero initialize. Native source/context/snapshot/job are exclusively owned by
 * caller and remain alive/immutable throughout; all callbacks obey those same
 * captured lifetimes. current/consume/prior observer must not do source/SDK/
 * provider reentry or mutation. The fence alone may perform established source
 * validation, never while a range is outstanding. No hostile-pointer safety.
 * Original snapshot/map/STREAM flags are never rewritten. All facts are
 * historical; LOCAL flags propose future composition, not native visibility,
 * copying, receipt, preparation, body persistence or launch authority. */
/* 0 opens a live candidate; BW_PBB_NOT_APPLICABLE only means all admitted
 * indexes were valid and selected no prefix entry, with fresh outer fences.
 * That owner is failed/has no view and MUST be destroyed; it is no permission
 * to reinterpret malformed, unsupported, conflicting or changed input. */
int bw_packed_bootstrap_open(struct bw_packed_bootstrap *,const struct bw_pbb_request *);
const struct bw_pbb_view *bw_packed_bootstrap_view(struct bw_packed_bootstrap *);
/* Exact entry id in the canonical returned table, at most once per instance.
 * This version emits ONLY scratch copies of the private verified capture;
 * it performs no SMB/provider/body reread at emission. Captured hashes are
 * checked again; current/job guard is pure. Consumer bytes are provisional.
 * Any failure requires discarding ALL
 * emitted results from this instance, even earlier successful calls. Bodies
 * are binary, SHA1/SHA256 checked; no project JSON parser is invoked. */
int bw_packed_bootstrap_read(struct bw_packed_bootstrap *,uint32_t,bw_source_consume_fn,void *);
/* Mode5 only. One successful call per admitted NONZERO (archive,span) before
 * finish. Absent/zero spans refuse without a callback. Each callback borrows a
 * scratch copy of the captured archive descriptor and <=64KiB binary bytes at
 * the exact absolute offset. The descriptor includes the expected full-span
 * offset/size/SHA1/SHA256. Neither descriptor nor bytes may be modified or kept;
 * the callback must not reenter cache/source/provider/SDK APIs. A native writer
 * may only write/hash its already-held provisional STREAM descriptor here.
 * Original spans can overlap: they are independently verified and emitted;
 * overlap does NOT grant authority to write elsewhere or skip a span/hash.
 * All nonzero span sizes are conservatively summed (including overlap), capped
 * at16MiB and charged together with original LOCAL/selected bodies against the
 * captured startup budget. Reads use the existing cache-owned transport caps.
 * Zero return verifies THIS emission; all outputs remain provisional until one
 * successful finish plus the owner's existing source/material/publication
 * fences and local range readback. Any error poisons the complete candidate. */
typedef int (*bw_pbb_metadata_consume_fn)(void *,uint32_t,enum bw_pbb_span_kind,
    const struct bw_pbb_archive *,uint64_t,const void *,size_t);
int bw_packed_bootstrap_read_metadata(struct bw_packed_bootstrap *,uint32_t,
    enum bw_pbb_span_kind,bw_pbb_metadata_consume_fn,void *);
/* Requires every entry read successfully. Rereads footer/index facts and
 * coalesced selected headers AND BODIES once at the final boundary, with
 * whole-source fences. Every final hash must match the private capture. Same
 * metadata is not cryptographic freshness; native owner must enforce its
 * existing source/publication fences. Mode5 additionally requires every
 * nonzero metadata span emitted exactly once. No rescans per emitted entry. */
int bw_packed_bootstrap_finish(struct bw_packed_bootstrap *);
/* Historical counters remain available after sticky failure, not liveness.
 * Lifetime = cache-owned + external, with checked monotonic deltas through
 * post-finish views. Rewind/overflow refuses; no whole-process work cap is
 * implied by the cache-owned caps. A counter-integrity failure returns -1
 * with the last successfully accounted historical counters in out. */
int bw_packed_bootstrap_stats(struct bw_packed_bootstrap *,struct bw_pbb_stats *);
int bw_packed_bootstrap_destroy(struct bw_packed_bootstrap *);
#endif
