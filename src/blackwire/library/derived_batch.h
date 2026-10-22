#ifndef BW_DERIVED_BATCH_H
#define BW_DERIVED_BATCH_H
#include "derived_material.h"
#include "packed_bootstrap.h"
#define BW_BATCH_POLICY 2u
#define BW_BATCH_PLAN_DOMAIN "native-packed-startup-plan-v2"
#define BW_BATCH_MATERIAL_DOMAIN "native-packed-startup-material-v2"
#define BW_BATCH_MAX_BASE 4096u
#define BW_BATCH_MAX_RECORDS 8193u
#define BW_BATCH_BOOTSTRAP_LIMIT UINT64_C(268435456)
enum bw_batch_origin_kind {BW_BATCH_ORIGINAL=1,BW_BATCH_PROJECT=2,BW_BATCH_ICU=3};
struct bw_batch_origin {uint32_t file_id,kind,original_id,archive_index,entry_index;};

/* Immutable historical metadata, never a provider or prepared capability.
 * Flat archives are shared by ICU entries and the optional project descriptor.
 * bootstrap.epoch and project observation counters are zero in this type.
 * Recorded SHA facts do not re-prove original bodies/JSON or stored SHA1 links. */
struct bw_batch_facts {
    struct bw_provider_binding original;
    struct bw_derived_prepared_identity prepared;
    struct bw_pbb_view bootstrap;
    uint32_t has_project;struct bw_pp_plan project;
    const uint8_t *base_map,*base_digests,*map,*digests;
    size_t base_map_size,base_digest_size,map_size,digest_size;
    const struct bw_batch_origin *origins;
    uint32_t count,local_count,stream_count;
    uint64_t base_bootstrap_bytes,local_bytes,remote_bytes,bootstrap_bytes;
    uint8_t base_map_sha256[32],base_digests_sha256[32];
    uint8_t facts_sha256[32],mapping_rule_sha256[32],material_policy_sha256[32];
    uint8_t map_sha256[32],digests_sha256[32],origins_sha256[32],material_sha256[32];
};
struct bw_batch_destination_claim {
    const struct bw_source_provider *original;
    const struct bw_packed_bootstrap *bootstrap;
    const struct bw_derived_local *project_local;
    int (*current_job)(void *);void *job;uint64_t epoch;
    uint8_t binding_sha256[32],snapshot_sha256[32],base_map_sha256[32],base_digests_sha256[32];
    uint8_t facts_sha256[32],mapping_rule_sha256[32];
};
struct bw_batch_destination_grant {
    uint32_t version;const void *owner;uint64_t epoch;
    uint8_t mapping_rule_sha256[32];
    int (*check)(const struct bw_batch_destination_grant *,const struct bw_batch_destination_claim *);
};
struct bw_batch_plan_request {
    uint32_t policy;
    struct bw_source_provider *original;
    struct bw_packed_bootstrap *bootstrap;
    const struct bw_pbb_request *bootstrap_request;
    /* Either both NULL, or an actual coherent already-opened V1 LOCAL owner
     * plus its exact original request. No synthetic descriptor is invented. */
    struct bw_derived_local *project_local;
    const struct bw_derived_plan_request *project_request;
    const struct bw_batch_destination_grant *destination;
    uint64_t startup_budget;
};
struct bw_batch_plan {void *state;const char *error;unsigned busy,failed;};
/* Pure bounded facts/rule hashing for precomputed full-claim admission. Does
 * not grant ownership, source freshness, destination/CWD visibility or Ready. */
int bw_batch_facts_hash(const struct bw_pbb_view *,const struct bw_pp_plan *,uint8_t[32],uint8_t[32]);
/* Exclusive coherent native provider/cache/source/snapshot/job lifetimes are
 * caller preconditions, not attestations obtainable from public structs.
 * Requires real provider-current outside callbacks. No body emission/finish,
 * copier, receipt, reader, publication or registration operation occurs.
 * Pure current/grant callbacks may not reenter DATA/source/SDK/controller I/O. */
int bw_batch_plan_open(struct bw_batch_plan *,const struct bw_batch_plan_request *);
const struct bw_batch_facts *bw_batch_plan_view(struct bw_batch_plan *);
/* Outer orchestration only: exact captured live handles/request and current
 * metadata view. No source/body I/O. Never call from DATA consume callbacks. */
int bw_batch_plan_request_matches(struct bw_batch_plan *,const struct bw_batch_plan_request *);
/* Fresh original provider-current plus optional same project parser checks,
 * then exact in-memory reconstruction; never follows a changed generation.
 * Cache finish remains a separate caller obligation after all cache emissions. */
int bw_batch_plan_revalidate(struct bw_batch_plan *);
int bw_batch_plan_destroy(struct bw_batch_plan *);

struct bw_batch_material_request {
    struct bw_batch_plan *plan;
    const struct bw_native_prepare_selection *accepted;
    struct bw_native_jobs *jobs;const struct bw_native_job_token *token;
};
struct bw_batch_material_view {const struct bw_batch_facts *facts;char run[33];uint64_t job_generation;};
struct bw_batch_material {void *state;const char *error;unsigned busy,failed;};
/* Actual accepted controller selection/cache/token and unique RUNNING snapshot
 * required. Does not create jobs, grant destination access, rescan source,
 * emit cache bytes or turn historical facts into receipt/readiness authority. */
int bw_batch_material_open(struct bw_batch_material *,const struct bw_batch_material_request *);
const struct bw_batch_material_view *bw_batch_material_view(struct bw_batch_material *);
int bw_batch_material_destroy(struct bw_batch_material *);
#endif
