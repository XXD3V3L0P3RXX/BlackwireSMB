#ifndef BW_BATCH_METADATA_H
#define BW_BATCH_METADATA_H
#include "derived_batch.h"

#define BW_BATCH_METADATA_PLAN_VERSION 1u
#define BW_BATCH_METADATA_MAX_SPANS (BW_PBB_MAX_ARCHIVES*BW_PBB_SPANS)
#define BW_BATCH_METADATA_MIN_UNIT 512u
#define BW_BATCH_METADATA_MAX_UNIT 65536u

struct bw_batch_metadata_archive {
    uint32_t original_file_id;
    char path[241];
    struct bw_source_info info;
    uint64_t declared_bytes,unique_bytes,allocation_bytes,blocks512;
};
struct bw_batch_metadata_span {
    uint32_t archive_index,kind;
    struct bw_pbb_span span;
};
struct bw_batch_metadata_range {
    uint32_t archive_index;
    uint64_t offset,size;
};
/* Owned-by-value historical output; allocate this bounded object on the heap
 * when the caller cannot afford its size on a native worker stack. All arrays
 * have fixed capacities, archive indices refer to archives[], and span rows
 * retain original kind/hash identity even when their transport ranges overlap.
 * ranges[] merges overlapping/adjacent bytes in one archive, never gaps.
 * Global and per-archive allocation_bytes are the union after rounding ranges outward to the explicit
 * allocation_unit; blocks512 uses the st_blocks accounting unit, not st_blksize.
 * The unit is a caller-admitted conservative bound, NOT measured filesystem
 * geometry. Native consumers must check held/named identities, all span hashes
 * and actual allocation themselves. No zero-outside-span claim is made. */
struct bw_batch_metadata_plan {
    uint32_t version,allocation_unit,archive_count,span_count,range_count;
    struct bw_provider_binding original;
    struct bw_derived_prepared_identity prepared;
    uint8_t native_snapshot_sha256[32],facts_sha256[32],base_map_sha256[32];
    uint8_t map_sha256[32],seed_sha256[32];
    uint64_t declared_bytes,unique_bytes,allocation_bytes,blocks512;
    uint64_t startup_bytes,bootstrap_bytes;
    struct bw_batch_metadata_archive archives[BW_PBB_MAX_ARCHIVES];
    struct bw_batch_metadata_span spans[BW_BATCH_METADATA_MAX_SPANS];
    struct bw_batch_metadata_range ranges[BW_BATCH_METADATA_MAX_SPANS];
};

/* Pure, synchronous metadata validation/reconstruction; no source/SDK/file I/O,
 * callback, body read, live authority, receipt, copy or Ready operation.
 * Only explicit mode5 metadata semantics are accepted. Validates full existing
 * facts/map/digests/origins through db_check, exact original STREAM archive rows
 * and all declared span bounds/hashes. Declared (overlap-counted) bytes <=16MiB
 * and fit remaining inherited LOCAL/startup and bootstrap budgets. Output is
 * cleared on refusal. allocation_unit must be power-of-two, 512..65536.
 * Caller supplies addressable immutable input memory for this call; out must
 * not alias facts or its backing arrays. Arbitrary C pointers cannot be proven
 * valid, and a historical decoded document grants no native ownership. */
int bw_batch_metadata_build(const struct bw_batch_facts *,uint32_t allocation_unit,
                            struct bw_batch_metadata_plan *,const char **error);
#endif
