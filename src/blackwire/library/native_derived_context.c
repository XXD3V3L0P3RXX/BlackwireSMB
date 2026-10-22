#include "native_derived_context.h"
#include "../streamreader/copy_sha256.h"
#include <string.h>

static int fail(struct bw_native_derived_context *p,const char *why){if(p&&!p->error)p->error=why;return -1;}
static int claim_check(const struct bw_derived_destination_grant *grant,const struct bw_derived_destination_claim *claim){
    if(!grant||!claim||!grant->owner)return -1;
    const struct bw_native_derived_context *p=grant->owner;
    return grant!=&p->grant||!p->epoch||grant->epoch!=p->epoch||grant->version!=BW_DERIVED_DESTINATION_GRANT_V1||
        grant->owner!=p||claim->provider!=p->claim.provider||claim->project!=p->claim.project||
        claim->current_job!=p->claim.current_job||claim->job!=p->claim.job||
        memcmp(claim->binding_sha256,p->claim.binding_sha256,32)||memcmp(claim->snapshot_sha256,p->claim.snapshot_sha256,32)||
        memcmp(claim->base_map_sha256,p->claim.base_map_sha256,32)||memcmp(claim->base_digests_sha256,p->claim.base_digests_sha256,32)||
        memcmp(claim->recipe_sha256,p->claim.recipe_sha256,32)||strcmp(claim->destination,p->claim.destination)||
        strcmp(grant->destination,p->claim.destination)||!p->request.current_job||p->request.current_job(p->request.job)?-1:0;
}
static int open_local(struct bw_native_derived_context *p,struct bw_source_provider *original,
                      struct bw_packed_project *project,int (*current)(void *),void *job,uint64_t epoch){
    if(!epoch||!current||!original||original->current!=current||original->opaque!=job||current(job))return fail(p,"derived_controller_lease_refused");
    const struct bw_pp_plan *recipe=bw_packed_project_view(project);if(!recipe)return fail(p,"derived_recipe_view_refused");
    p->original=original;p->project=project;p->epoch=epoch;
    p->claim.provider=original;p->claim.project=project;p->claim.current_job=current;p->claim.job=job;
    memcpy(p->claim.binding_sha256,original->identity.binding.sha256,32);
    memcpy(p->claim.snapshot_sha256,original->folder.snapshot.sha256,32);
    cp_sha_bytes(original->bundle.map.bytes,original->bundle.map.size,p->claim.base_map_sha256);
    cp_sha_bytes(original->bundle.digests,original->bundle.digest_size,p->claim.base_digests_sha256);
    /* The exact recipe hash is produced by the real composer. Its canonical
     * definition is shared with the frozen packed-project DATA helper. */
    if(bw_derived_plan_recipe_sha256(recipe,p->claim.recipe_sha256))return fail(p,"derived_recipe_claim_refused");
    strcpy(p->claim.destination,recipe->proposed_destination);
    p->grant.version=BW_DERIVED_DESTINATION_GRANT_V1;p->grant.owner=p;p->grant.epoch=epoch;
    strcpy(p->grant.destination,recipe->proposed_destination);p->grant.check=claim_check;
    if(bw_derived_material_rule_sha256(recipe,p->grant.mapping_rule_sha256))return fail(p,"derived_mapping_rule_refused");
    p->request=(struct bw_derived_plan_request){.original=original,.project=project,.current_job=current,.job=job,
        .destination=&p->grant,.policy=BW_DERIVED_PLAN_POLICY,.startup_budget=original->budget};
    const struct bw_derived_local_limits limits={.attempts=BW_DERIVED_LOCAL_MAX_ATTEMPTS,
        .consume_calls=BW_DERIVED_LOCAL_MAX_CONSUME_CALLS,.attempted_bytes=BW_PROVIDER_MAX_LOCAL_BYTES,.delivered_bytes=BW_PROVIDER_MAX_LOCAL_BYTES};
    if(bw_derived_local_open(&p->local,&p->request,&limits))return fail(p,p->local.error?p->local.error:"derived_local_open_refused");
    return 0;
}
int bw_native_derived_context_prepare(struct bw_native_derived_context *p,struct bw_source_provider *original,
    int (*current)(void *),void *job,const struct bw_native_prepare_selection *accepted,struct bw_native_jobs *jobs,
    const struct bw_native_job_token *token){
    if(!p||p->original||p->error||!accepted||!jobs||!token||token->owner!=jobs||!token->generation||
       !original||!current||original->current!=current||original->opaque!=job||current(job))return -1;
    int found=bw_native_project_recipe_open(&p->discovery,original,current,job);
    if(found<0)return fail(p,p->discovery.error?p->discovery.error:"derived_discovery_refused");if(!found)return 0;
    if(open_local(p,original,&p->discovery.project,current,job,token->generation))return -1;
    const struct bw_derived_material_request request={.accepted=accepted,.jobs=jobs,.token=token,.plan=&p->request,.local=&p->local};
    if(bw_derived_material_open(&p->material,&request))return fail(p,p->material.error?p->material.error:"derived_material_open_refused");
    return 1;
}
int bw_native_derived_context_reopen(struct bw_native_derived_context *p,struct bw_source_provider *original,
    int (*current)(void *),void *job,uint64_t epoch,const struct bw_derived_provenance_facts *history){
    if(!p||p->original||p->error||!history||!original||!current||!epoch||original->current!=current||original->opaque!=job||current(job))return -1;
    const struct bw_provider_binding *a=&original->identity,*b=&history->original;
    if(a->kind!=b->kind||a->policy!=b->policy||strcmp(a->binding.source,b->binding.source)||strcmp(a->binding.game,b->binding.game)||
       strcmp(a->binding.title,b->binding.title)||memcmp(a->binding.sha256,b->binding.sha256,32)||
       memcmp(original->folder.snapshot.sha256,history->plan.base_snapshot_sha256,32)||
       original->bundle.map.size!=history->base_map_size||original->bundle.digest_size!=history->base_digest_size||
       memcmp(original->bundle.map.bytes,history->base_map,history->base_map_size)||memcmp(original->bundle.digests,history->base_digests,history->base_digest_size))
        return fail(p,"derived_original_generation_changed");
    const struct bw_pp_plan *recipe=&history->recipe;
    struct bw_pp_request request={.source=original->source,.snapshot=&original->folder.snapshot,.current=current,.job=job,
        .source_kind=BW_PROVIDER_FOLDER,.source_policy=BW_PROVIDER_POLICY_V1,.profile="none",.archive=recipe->archive,
        .requested=recipe->requested,.listed=recipe->listed,.proposed_destination=recipe->proposed_destination,
        .base_local_bytes=original->bundle.map.local_bytes,.startup_budget=original->budget};
    if(bw_packed_project_open(&p->reopened,&request))return fail(p,p->reopened.error?p->reopened.error:"derived_historical_recipe_changed");
    if(open_local(p,original,&p->reopened,current,job,epoch))return -1;
    const struct bw_derived_plan_view *view=bw_derived_local_view(&p->local);
    if(!view||strcmp(view->identity.component,history->plan.identity.component)||
       memcmp(view->identity.commitment_sha256,history->plan.identity.commitment_sha256,32)||
       memcmp(view->map_sha256,history->plan.map_sha256,32)||memcmp(view->digests_sha256,history->plan.digests_sha256,32)||
       memcmp(view->recipe_sha256,history->plan.recipe_sha256,32)||memcmp(view->origins_sha256,history->plan.origins_sha256,32))
        return fail(p,"derived_historical_plan_changed");
    return 0;
}
int bw_native_derived_context_destroy(struct bw_native_derived_context *p){
    if(!p)return -1;
    if(bw_derived_material_destroy(&p->material)||bw_derived_local_destroy(&p->local)||
       bw_packed_project_destroy(&p->reopened)||bw_native_project_recipe_destroy(&p->discovery))return -1;
    memset(p,0,sizeof *p);return 0;
}
