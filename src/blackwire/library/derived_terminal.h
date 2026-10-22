#ifndef BW_DERIVED_TERMINAL_H
#define BW_DERIVED_TERMINAL_H
#include "derived_provenance.h"
#define BW_DERIVED_TERMINAL_VERSION 1u
#define BW_DERIVED_TERMINAL_LIMIT 4096u
#define BW_DERIVED_TERMINAL_INDEX_LIMIT (2u*1024u*1024u)
#define BW_DERIVED_TERMINAL_IDENTITY_LIMIT 4096u
struct bw_derived_artifact_ref {uint64_t size;uint8_t sha256[32];};
struct bw_derived_object_identity {uint64_t device,inode;};
/* Historical recorded observations ONLY. No live provider/material/job/grant,
 * ready/body-verified/Play flag, filesystem ownership or durability authority.
 * original is the unchanged source binding; prepared is NEVER a provider. */
struct bw_derived_terminal_record {
    struct bw_provider_binding original;
    struct bw_derived_prepared_identity prepared;
    uint32_t material_policy;
    uint8_t material_policy_sha256[32],mapping_rule_sha256[32],material_sha256[32];
    uint8_t native_snapshot_sha256[32],base_map_sha256[32],base_digests_sha256[32];
    uint8_t recipe_sha256[32],origins_sha256[32];
    char run[33],title[10]; /* preparation run; not a later reader launch run */
    struct bw_derived_artifact_ref provenance,map,digests,index,marker,identity;
    uint32_t count,local_count,stream_count,entries;
    uint64_t local_bytes,remote_bytes,bootstrap_bytes,allocated_bytes;
    struct bw_derived_object_identity game_root,state_root,provenance_object;
};
/* Immutable metadata snapshots, not open handles/authority. identity may be the
 * adapter's domain+LP canonical state-source/root-owner metadata combination;
 * only its exact byte binding is checked here. Its schema and both physical
 * named-object readbacks/ownership remain adapter gates. It must not include
 * this terminal record's hash (that would create a commitment cycle).
 * map/digests are the exact final blobs embedded in decoded provenance. */
struct bw_derived_terminal_artifacts {
    const void *provenance,*index,*marker,*identity;
    size_t provenance_size,index_size,marker_size,identity_size;
};
/* BWNDR001 is a NEW record; all legacy formats remain unchanged. Canonical LE
 * record-only codec, no artifacts fetched. Decode clears output on refusal.
 * Input/output allocations must be addressable, immutable and nonoverlapping.
 * Output is bounded caller-owned memory, with size=0 on encode refusal. */
int bw_derived_terminal_encode(const struct bw_derived_terminal_record *,void *,size_t,size_t *,const char **);
int bw_derived_terminal_decode(struct bw_derived_terminal_record *,const void *,size_t,const char **);
/* Independent pure cross-artifact validation: actual frozen provenance decode,
 * exact semantic fields/map+BWDIG/counts, AMPRIDX3 DATA rows/hash table, and
 * BWGEN001(run/title/prepared.component/map SHA). No body/filesystem/current/
 * callback/network calls. Index mtimes are recorded metadata, NOT filesystem
 * evidence. Root dev+ino are opaque recorded observations for adapter checks.
 * Success remains historical consistency, never publication/readiness/Play. */
int bw_derived_terminal_validate(const struct bw_derived_terminal_record *,const struct bw_derived_terminal_artifacts *,const char **);
#endif
