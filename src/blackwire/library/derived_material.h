#ifndef BW_DERIVED_MATERIAL_H
#define BW_DERIVED_MATERIAL_H
#include "derived_local.h"
#include "native_backend.h"

#define BW_DERIVED_MATERIAL_POLICY 1u
#define BW_DERIVED_MATERIAL_DOMAIN "native-prepared-plain-project-material-v1"
#define BW_DERIVED_MATERIAL_RULE_DOMAIN "Blackwire native prepared-root plain-project placement rule v1"
#define BW_DERIVED_MATERIAL_RULE_SEMANTICS "profile-none;literal-request-index-equality;explicit-destination;collision-refusal;no-visibility-proof"

struct bw_derived_material_request {
    const struct bw_native_prepare_selection *accepted;
    struct bw_native_jobs *jobs; /* explicitly captured accepted controller cache */
    const struct bw_native_job_token *token;
    const struct bw_derived_plan_request *plan;
    struct bw_derived_local *local; /* opened from this exact coherent plan */
};
struct bw_derived_material_view {
    uint32_t policy;
    struct bw_provider_binding original; /* unchanged source identity */
    struct bw_derived_prepared_identity prepared; /* distinct, never a provider */
    const struct bw_derived_plan_view *plan;
    uint8_t policy_sha256[32],mapping_rule_sha256[32],material_sha256[32];
    char run[33];uint64_t job_generation; /* ephemeral, excluded from hashes */
};
struct bw_derived_material {void *state;const char *error;unsigned busy,failed;};

/* Pure bounded metadata codec only, no authority. Literal supplied destination
 * must match requested==listed=="../../../"+destination, exact case. This
 * validates a proposed mapping; it never derives permission or proves CWD. */
int bw_derived_material_rule_sha256(const struct bw_pp_plan *,uint8_t[32]);
void bw_derived_material_policy_sha256(uint8_t[32]);
/* Zero initialize. Borrows ACTUAL accepted selection/cache/token and coherent
 * exclusive provider/parser/source/job/grant/LOCAL lifetimes. LOCAL must have
 * been opened from this exact request; public fields cannot attest private
 * pointer provenance, HTTP authentication, catalog/settings or idle fences.
 * Selection/token/plan and DATA metadata/lengths stay valid and immutable;
 * live job cache progress/state may change and is observed through snapshots.
 * Its owning cache lifetime remains retained until destroy. Required
 * current/grant callbacks are pure, synchronous and nonreentrant: no source,
 * provider/parser/LOCAL/material/SDK APIs, settings or progress calls.
 * Calls the unchanged bounded in-memory jobs snapshot before/after inspection;
 * no source rescan, archive reread, body hash, destination or other I/O occurs.
 * grant epoch must equal the actual running token generation (not hashed).
 */
int bw_derived_material_open(struct bw_derived_material *,const struct bw_derived_material_request *);
/* Rechecks captured actual job/selection and live in-memory DATA views. Returns
 * borrowed historical material only; no ready/PREPARED, copying, receipt,
 * publication, registration, alias, normal reader or launch authority. Later
 * LOCAL reads and controller outer fences still provide freshness. Sticky
 * failure clears retained public view, never silently rebases or repairs. */
const struct bw_derived_material_view *bw_derived_material_view(struct bw_derived_material *);
/* Owned metadata only; never finishes jobs or destroys borrowed DATA objects. */
int bw_derived_material_destroy(struct bw_derived_material *);
#endif
