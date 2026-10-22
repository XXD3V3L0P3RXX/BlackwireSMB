#ifndef BW_NATIVE_DERIVED_CONTEXT_H
#define BW_NATIVE_DERIVED_CONTEXT_H
#include "native_project_recipe.h"
#include "derived_material.h"
#include "derived_provenance.h"

/* One actual controller worker owns this stable-address context. It borrows
 * the original live provider and pure cancellation lease; it never rewrites
 * provider identity/map/snapshot. The destination grant is created only after
 * the controller accepts the exact supported command-line placement rule. */
struct bw_native_derived_context {
    struct bw_native_project_recipe discovery;
    struct bw_packed_project reopened;
    struct bw_packed_project *project;
    struct bw_source_provider *original;
    struct bw_derived_destination_grant grant;
    struct bw_derived_destination_claim claim;
    struct bw_derived_plan_request request;
    struct bw_derived_local local;
    struct bw_derived_material material;
    uint64_t epoch;
    const char *error;
};
/* 1 actual narrow derived plan, 0 ordinary source without a project request,
 * -1 refusal. Only an actual accepted preparation/cache/token may materialize. */
int bw_native_derived_context_prepare(struct bw_native_derived_context *,
    struct bw_source_provider *,int (*)(void *),void *,
    const struct bw_native_prepare_selection *,struct bw_native_jobs *,
    const struct bw_native_job_token *);
/* Fresh real parser/plan/LOCAL under actual Play lease. Historical provenance
 * is a hint and exact expected commitment, never a provider or live grant.
 * No material object or fake RUNNING preparation job is constructed. */
int bw_native_derived_context_reopen(struct bw_native_derived_context *,
    struct bw_source_provider *,int (*)(void *),void *,uint64_t,
    const struct bw_derived_provenance_facts *);
int bw_native_derived_context_destroy(struct bw_native_derived_context *);
#endif
