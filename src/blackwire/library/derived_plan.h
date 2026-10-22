#ifndef BW_DERIVED_PLAN_H
#define BW_DERIVED_PLAN_H
#include "source_provider.h"
#include "packed_project.h"

#define BW_DERIVED_PLAN_POLICY 1u
#define BW_DERIVED_PLAN_DOMAIN "native-derived-plain-project-plan-v1"
#define BW_DERIVED_DESTINATION_GRANT_V1 1u
#define BW_DERIVED_MAX_RECORDS (BW_PP_MAX_INVENTORY+1u)

/* A live caller-owned experimental destination permission. It is NOT a
 * persisted recipe flag, native CWD/visibility proof or publication grant.
 * The caller supplies this exact object and a pure nonreentrant check; zero
 * means the same owner/epoch/rule/destination is still permitted for the claim.
 * All objects/allocations remain addressable and immutable during callbacks.
 * The composer never creates a grant, strips ../../../, or follows a new grant.
 */
struct bw_derived_destination_grant;
struct bw_derived_destination_claim {
    const struct bw_source_provider *provider;
    const struct bw_packed_project *project;
    int (*current_job)(void *);
    void *job;
    uint8_t binding_sha256[32],snapshot_sha256[32];
    uint8_t base_map_sha256[32],base_digests_sha256[32],recipe_sha256[32];
    char destination[BW_PP_MAX_PATH+1];
};
struct bw_derived_destination_grant {
    uint32_t version;
    const void *owner;
    uint64_t epoch;
    char destination[BW_PP_MAX_PATH+1];
    uint8_t mapping_rule_sha256[32];
    int (*check)(const struct bw_derived_destination_grant *,
                 const struct bw_derived_destination_claim *);
};
struct bw_derived_plan_request {
    struct bw_source_provider *original;
    struct bw_packed_project *project;
    int (*current_job)(void *);
    void *job;
    const struct bw_derived_destination_grant *destination;
    uint32_t policy; /* exactly BW_DERIVED_PLAN_POLICY; original stays policy1 */
    uint64_t startup_budget; /* 1..original budget, at most 5 GiB */
};
enum bw_derived_origin_kind {BW_DERIVED_ORIGINAL=1,BW_DERIVED_PACKED_PROJECT=2};
struct bw_derived_origin {
    uint32_t file_id,kind,original_id,archive_original_id,recipe_index;
};
struct bw_derived_prepared_identity {
    /* Separate proposed prepared identity, NEVER a bw_provider_binding.
     * No original game/identity/snapshot is rewritten. */
    uint32_t policy;
    char domain[48],component[75];
    uint8_t policy_sha256[32],namespace_sha256[32],commitment_sha256[32];
};
struct bw_derived_plan_view {
    struct bw_derived_prepared_identity identity;
    const uint8_t *map,*digests;
    size_t map_size,digest_size;
    const struct bw_derived_origin *origins;
    uint32_t count,derived_id,archive_original_id;
    uint64_t local_bytes,remote_bytes,bootstrap_bytes;
    uint32_t local_count,stream_count;
    uint8_t map_sha256[32],digests_sha256[32],origins_sha256[32],recipe_sha256[32];
    uint8_t base_map_sha256[32],base_digests_sha256[32],base_snapshot_sha256[32];
};
struct bw_derived_plan {void *state;const char *error;unsigned busy,failed;};
/* Canonical immutable recipe-facts hash shared by the controller's explicit
 * destination claim. The recipe must be an actual bounded parser view; this
 * pure hash conveys no live ownership, destination permission or freshness. */
int bw_derived_plan_recipe_sha256(const struct bw_pp_plan *,uint8_t[32]);
/* Zero initialize. Exclusive caller-owned native provider/parser/source/job;
 * no concurrent calls, callback reentry, provider destruction or mutations.
 * The parser MUST have been opened for the coherent original source/job; its
 * public API reveals matching seal/facts, not private connection/job pointers.
 * Ready/verified bits are sanity checks only: actual provider_current and
 * parser_revalidate are required outside SMB callbacks on open/revalidate.
 * No source reads through a HOST adapter, destination writes, copying,
 * receipts, registration, selection, reader or app policy changes.
 */
int bw_derived_plan_open(struct bw_derived_plan *,const struct bw_derived_plan_request *);
/* Checks the current job, captured in-memory base/parser/grant and commitments.
 * Does NOT rescan the source or reread archive bytes. Returned immutable borrowed
 * facts are historical after return, never publication/readiness authority.
 * Revalidate before future use; that also cannot replace a guarded copier.
 */
const struct bw_derived_plan_view *bw_derived_plan_view(struct bw_derived_plan *);
/* Actual original provider-current -> same parser revalidate -> provider-current,
 * then deterministic reconstruction/equality. Refuse changes, never rebase. */
int bw_derived_plan_revalidate(struct bw_derived_plan *);
/* Failure zeroes the retained view, without freeing buffers during callbacks.
 * Destroy releases only owned derived memory; never the borrowed live inputs. */
int bw_derived_plan_destroy(struct bw_derived_plan *);
#endif
