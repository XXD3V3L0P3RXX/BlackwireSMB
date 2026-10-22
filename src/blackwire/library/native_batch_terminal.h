#ifndef BW_NATIVE_BATCH_TERMINAL_H
#define BW_NATIVE_BATCH_TERMINAL_H
#include "derived_batch_provenance.h"
#include "derived_terminal.h" /* unchanged common artifact/object value types */
#define BW_BATCH_TERMINAL_VERSION 2u
#define BW_BATCH_TERMINAL_LIMIT 4096u
#define BW_BATCH_TERMINAL_INDEX_LIMIT (4u*1024u*1024u)
#define BW_BATCH_TERMINAL_IDENTITY_LIMIT 4096u
struct bw_batch_terminal_record {
    struct bw_provider_binding original;
    struct bw_derived_prepared_identity prepared;
    uint32_t material_policy;
    uint8_t material_policy_sha256[32],mapping_rule_sha256[32],material_sha256[32];
    uint8_t native_snapshot_sha256[32],base_map_sha256[32],base_digests_sha256[32];
    uint8_t facts_sha256[32],origins_sha256[32];
    char run[33],title[10];
    struct bw_derived_artifact_ref provenance,map,digests,index,marker,identity;
    uint32_t count,local_count,stream_count,entries;
    /* allocated_bytes measures the game-root tree only, including its normal
     * map/index/owner/marker artifacts. State/provenance allocation is separate. */
    uint64_t local_bytes,remote_bytes,bootstrap_bytes,allocated_bytes;
    struct bw_derived_object_identity game_root,state_root,provenance_object;
};
struct bw_batch_terminal_artifacts {
    const void *provenance,*index,*marker,*identity;
    size_t provenance_size,index_size,marker_size,identity_size;
};
/* New BWNDR002 only. V1 wire/codecs remain unchanged. Record decode proves
 * bounded historical structure; validate independently binds exact BWDPR002,
 * AMPRIDX3, BWGEN001 and identity bytes. No live/readiness authority. */
int bw_batch_terminal_encode(const struct bw_batch_terminal_record *,void *,size_t,size_t *,const char **);
int bw_batch_terminal_decode(struct bw_batch_terminal_record *,const void *,size_t,const char **);
int bw_batch_terminal_validate(const struct bw_batch_terminal_record *,const struct bw_batch_terminal_artifacts *,const char **);
#endif
