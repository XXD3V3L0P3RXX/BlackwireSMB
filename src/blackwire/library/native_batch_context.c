#define _POSIX_C_SOURCE 200809L
#include "native_batch_context.h"
#include "packed_project_json.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct nbc_loose {char path[241];uint64_t size;uint8_t sha256[32];};
struct nbc_state {
    struct bw_native_batch_context *owner;
    struct bw_source_provider *original;struct bw_source_smb *source;struct smb2_context *connection;
    struct bw_provider_binding binding;const uint8_t *map,*digests;size_t map_size,digest_size;
    uint8_t seal[32],map_sha256[32],digests_sha256[32];
    int (*current)(void *);void *job;uint64_t epoch;double deadline;
    struct bw_native_derived_context project;uint8_t project_rule[32];
    struct nbc_loose loose[8];unsigned loose_count,inside_callback,prepared,finished,applicable;
    char archive_names[BW_PBB_MAX_ARCHIVES][241];const char *archives[BW_PBB_MAX_ARCHIVES];
    struct bw_pbb_request cache_request;struct bw_packed_bootstrap cache;
    struct bw_batch_destination_claim claim;struct bw_batch_destination_grant grant;
    struct bw_batch_plan_request request;struct bw_batch_plan plan;struct bw_batch_material material;
};
static int fail(struct bw_native_batch_context *p,const char *why){if(p){p->failed=1;if(!p->error)p->error=why;}return -1;}
static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int suffix(const char *p,const char *end){size_t n=strlen(p),m=strlen(end);if(n<m)return 0;for(size_t i=0;i<m;i++)if(fold((unsigned char)p[n-m+i])!=fold((unsigned char)end[i]))return 0;return 1;}
static int names_compare(const void *a,const void *b){return strcmp((const char *)a,(const char *)b);}
static int same_source(struct nbc_state *s){
    struct bw_source_provider *a=s->original;double at=now();
    return s->owner->failed||!a||a->failed||a->error||a->source!=s->source||!s->source->smb||s->source->smb!=s->connection||
        a->current!=s->current||a->opaque!=s->job||memcmp(&a->identity,&s->binding,sizeof s->binding)||
        a->bundle.map.bytes!=s->map||a->bundle.map.size!=s->map_size||a->bundle.digests!=s->digests||a->bundle.digest_size!=s->digest_size||
        memcmp(a->folder.snapshot.sha256,s->seal,32)||at<0||!(s->source->deadline>=-DBL_MAX&&s->source->deadline<=DBL_MAX)||at>=s->deadline||at>=s->source->deadline?-1:0;
}
static int current(struct nbc_state *s){
    if(s->inside_callback)return fail(s->owner,"batch_context_callback_reentry");
    s->inside_callback=1;int bad=same_source(s)||s->current(s->job)||same_source(s);s->inside_callback=0;
    return bad?fail(s->owner,"batch_context_worker_or_source_changed"):0;
}
struct nbc_bytes {uint8_t data[BW_PP_MAX_BODY];size_t used,limit;};
static int collect(void *opaque,const void *raw,size_t n){struct nbc_bytes *b=opaque;if(!raw||!n||n>b->limit-b->used)return -1;memcpy(b->data+b->used,raw,n);b->used+=n;return 0;}
static int loose_verify(struct nbc_state *s,const struct nbc_loose *f){
    struct nbc_bytes *b=calloc(1,sizeof *b);if(!b)return fail(s->owner,"batch_context_loose_allocation");
    b->limit=(size_t)f->size;uint8_t hash[32];char modules[32][65];unsigned count=0;
    int bad=current(s)||bw_source_provider_read(s->original,f->path,collect,b)||b->used!=f->size;
    if(!bad){cp_sha_bytes(b->data,b->used,hash);bad=memcmp(hash,f->sha256,32)||bw_pp_json(b->data,b->used,modules,&count,s->current,s->job)||current(s);}
    free(b);return bad?fail(s->owner,"batch_context_loose_project_changed_or_invalid"):0;
}
static int fence(void *opaque){
    struct nbc_state *s=opaque;
    if(current(s)||s->original->busy||bw_source_provider_current(s->original)||current(s))return fail(s->owner,"batch_context_provider_fence_refused");
    if(s->project.project&&(bw_packed_project_revalidate(s->project.project)||current(s)))return fail(s->owner,"batch_context_project_fence_refused");
    for(unsigned i=0;i<s->loose_count;i++)if(loose_verify(s,&s->loose[i]))return -1;
    return current(s);
}
static int project_granted(const struct bw_derived_destination_grant *g,const struct bw_derived_destination_claim *c){
    if(!g||!c||!g->owner)return -1;struct nbc_state *s=(struct nbc_state *)g->owner;const struct bw_derived_destination_claim *e=&s->project.claim;
    return g!=&s->project.grant||g->version!=1||g->epoch!=s->epoch||memcmp(g->mapping_rule_sha256,s->project_rule,32)||strcmp(g->destination,e->destination)||
        c->provider!=e->provider||c->project!=e->project||c->current_job!=e->current_job||c->job!=e->job||
        memcmp(c->binding_sha256,e->binding_sha256,32)||memcmp(c->snapshot_sha256,e->snapshot_sha256,32)||
        memcmp(c->base_map_sha256,e->base_map_sha256,32)||memcmp(c->base_digests_sha256,e->base_digests_sha256,32)||
        memcmp(c->recipe_sha256,e->recipe_sha256,32)||strcmp(c->destination,e->destination)||current(s)?-1:0;
}
static int project_local(struct nbc_state *s){
    struct bw_native_derived_context *p=&s->project;const struct bw_pp_plan *recipe=bw_packed_project_view(&p->discovery.project);
    if(!recipe)return fail(s->owner,"batch_context_project_view_refused");p->original=s->original;p->project=&p->discovery.project;p->epoch=s->epoch;
    p->claim.provider=s->original;p->claim.project=p->project;p->claim.current_job=s->current;p->claim.job=s->job;
    memcpy(p->claim.binding_sha256,s->binding.binding.sha256,32);memcpy(p->claim.snapshot_sha256,s->seal,32);
    memcpy(p->claim.base_map_sha256,s->map_sha256,32);memcpy(p->claim.base_digests_sha256,s->digests_sha256,32);
    if(bw_derived_plan_recipe_sha256(recipe,p->claim.recipe_sha256)||bw_derived_material_rule_sha256(recipe,s->project_rule))return fail(s->owner,"batch_context_project_rule_refused");
    strcpy(p->claim.destination,recipe->proposed_destination);p->grant.version=1;p->grant.owner=s;p->grant.epoch=s->epoch;p->grant.check=project_granted;
    strcpy(p->grant.destination,recipe->proposed_destination);memcpy(p->grant.mapping_rule_sha256,s->project_rule,32);
    p->request=(struct bw_derived_plan_request){.original=s->original,.project=p->project,.current_job=s->current,.job=s->job,.destination=&p->grant,.policy=1,.startup_budget=s->original->budget};
    const struct bw_derived_local_limits limits={.attempts=BW_DERIVED_LOCAL_MAX_ATTEMPTS,.consume_calls=BW_DERIVED_LOCAL_MAX_CONSUME_CALLS,.attempted_bytes=BW_PROVIDER_MAX_LOCAL_BYTES,.delivered_bytes=BW_PROVIDER_MAX_LOCAL_BYTES};
    return bw_derived_local_open(&p->local,&p->request,&limits)?fail(s->owner,p->local.error?p->local.error:"batch_context_project_local_refused"):0;
}
/* Eligibility precedes Pak enumeration. A .pak extension alone is no engine
 * evidence. Existing V1 discovery validates actual command/request/body facts;
 * an original loose descriptor must be LOCAL, digest-matched and strict JSON. */
static int eligible(struct nbc_state *s){
    int found=bw_native_project_recipe_open(&s->project.discovery,s->original,s->current,s->job);
    if(found<0)return fail(s->owner,s->project.discovery.error?s->project.discovery.error:"batch_context_project_discovery_refused");
    if(found)return project_local(s)?-1:1;
    struct bw_manifest map;if(bw_manifest_open(&map,s->map,s->map_size))return fail(s->owner,"batch_context_original_map_refused");
    for(uint32_t id=0;id<map.count;id++){
        struct bw_manifest_record r;if(bw_manifest_get(&map,id,&r)||current(s))return -1;if(!suffix(r.path,".uproject"))continue;
        if(s->loose_count==8||r.flags!=BW_MANIFEST_LOCAL||!r.size||r.size>BW_PP_MAX_BODY||r.path_length>240)return fail(s->owner,"batch_context_loose_project_limit");
        struct nbc_loose *f=&s->loose[s->loose_count++];memcpy(f->path,r.path,r.path_length+1);f->size=r.size;memcpy(f->sha256,s->digests+56u+32u*id,32);
        if(loose_verify(s,f))return -1;
    }
    return s->loose_count?1:0;
}
static int archives(struct nbc_state *s){
    struct bw_manifest map;if(bw_manifest_open(&map,s->map,s->map_size)||map.count>BW_BATCH_MAX_BASE)return fail(s->owner,"batch_context_inventory_limit");
    uint32_t count=0;
    for(uint32_t id=0;id<map.count;id++){
        struct bw_manifest_record r;if(bw_manifest_get(&map,id,&r)||current(s))return -1;if(!suffix(r.path,".pak"))continue;
        if(count==BW_PBB_MAX_ARCHIVES||r.flags!=BW_MANIFEST_STREAM||r.path_length>240)return fail(s->owner,"batch_context_archive_limit_or_origin");
        const struct bw_source_info *i=bw_source_snapshot_find(&s->original->folder.snapshot,r.path);
        if(!i||i->size!=r.size||(i->attributes&(0x10u|0x40u|0x400u)))return fail(s->owner,"batch_context_archive_generation");
        memcpy(s->archive_names[count++],r.path,r.path_length+1);
    }
    qsort(s->archive_names,count,sizeof s->archive_names[0],names_compare);for(uint32_t i=0;i<count;i++)s->archives[i]=s->archive_names[i];
    s->cache_request=(struct bw_pbb_request){.version=BW_PBB_VERSION,.epoch=s->epoch,.source=s->source,.snapshot=&s->original->folder.snapshot,
        .current=s->current,.job=s->job,.fence=fence,.fence_opaque=s,.source_kind=1,.source_policy=1,.profile="none",.prefix=BW_PBB_CURRENT_PREFIX,
        .archives=s->archives,.archive_count=count,.base_local_bytes=s->original->bundle.map.local_bytes,.startup_budget=s->original->budget,
        .read_byte_limit=BW_PBB_MAX_READ_BYTES,.request_limit=BW_PBB_MAX_REQUESTS};return 0;
}
static int batch_granted(const struct bw_batch_destination_grant *g,const struct bw_batch_destination_claim *c){
    if(!g||!c||!g->owner)return -1;struct nbc_state *s=(struct nbc_state *)g->owner;const struct bw_batch_destination_claim *e=&s->claim;
    return g!=&s->grant||g->version!=2||g->epoch!=s->epoch||memcmp(g->mapping_rule_sha256,e->mapping_rule_sha256,32)||
        c->original!=e->original||c->bootstrap!=e->bootstrap||c->project_local!=e->project_local||c->current_job!=e->current_job||c->job!=e->job||c->epoch!=e->epoch||
        memcmp(c->binding_sha256,e->binding_sha256,32)||memcmp(c->snapshot_sha256,e->snapshot_sha256,32)||
        memcmp(c->base_map_sha256,e->base_map_sha256,32)||memcmp(c->base_digests_sha256,e->base_digests_sha256,32)||
        memcmp(c->facts_sha256,e->facts_sha256,32)||memcmp(c->mapping_rule_sha256,e->mapping_rule_sha256,32)||current(s)?-1:0;
}
static int open_batch(struct nbc_state *s){
    int engine=eligible(s);if(engine<=0)return engine;
    if(archives(s))return -1;if(!s->cache_request.archive_count)return fence(s)?-1:0;
    int rc=bw_packed_bootstrap_open(&s->cache,&s->cache_request);
    if(rc==BW_PBB_NOT_APPLICABLE){if(bw_packed_bootstrap_destroy(&s->cache))return fail(s->owner,"batch_context_empty_cache_destroy_refused");return 0;}
    if(rc)return fail(s->owner,s->cache.error?s->cache.error:"batch_context_cache_refused");
    const struct bw_pbb_view *v=bw_packed_bootstrap_view(&s->cache);const struct bw_pp_plan *p=s->project.project?bw_packed_project_view(s->project.project):NULL;
    if(!v||(s->project.project&&!p))return fail(s->owner,"batch_context_live_facts_refused");
    s->claim=(struct bw_batch_destination_claim){.original=s->original,.bootstrap=&s->cache,.project_local=p?&s->project.local:NULL,.current_job=s->current,.job=s->job,.epoch=s->epoch};
    memcpy(s->claim.binding_sha256,s->binding.binding.sha256,32);memcpy(s->claim.snapshot_sha256,s->seal,32);
    memcpy(s->claim.base_map_sha256,s->map_sha256,32);memcpy(s->claim.base_digests_sha256,s->digests_sha256,32);
    if(bw_batch_facts_hash(v,p,s->claim.facts_sha256,s->claim.mapping_rule_sha256))return fail(s->owner,"batch_context_full_claim_refused");
    s->grant.version=2;s->grant.owner=s;s->grant.epoch=s->epoch;s->grant.check=batch_granted;memcpy(s->grant.mapping_rule_sha256,s->claim.mapping_rule_sha256,32);
    s->request=(struct bw_batch_plan_request){.policy=2,.original=s->original,.bootstrap=&s->cache,.bootstrap_request=&s->cache_request,
        .project_local=p?&s->project.local:NULL,.project_request=p?&s->project.request:NULL,.destination=&s->grant,.startup_budget=s->original->budget};
    if(bw_batch_plan_open(&s->plan,&s->request)||current(s))return fail(s->owner,s->plan.error?s->plan.error:"batch_context_plan_refused");s->applicable=1;return 1;
}
static int start(struct bw_native_batch_context *p,struct bw_source_provider *a,int (*guard)(void *),void *job,uint64_t epoch){
    if(!p)return -1;if(p->busy||p->state||p->failed)return fail(p,"batch_context_new_zeroed_owner_required");
    p->busy=1;
    if(!a||!guard||!epoch||a->current!=guard||a->opaque!=job||!a->source||!a->source->smb||guard(job)||p->failed){p->busy=0;return fail(p,"batch_context_actual_worker_required");}
    if(a->identity.kind!=BW_PROVIDER_FOLDER||a->identity.policy!=1||bw_source_profile_name(a->identity.binding.title)){p->busy=0;return 0;}
    struct nbc_state *s=calloc(1,sizeof *s);if(!s){p->busy=0;return fail(p,"batch_context_allocation");}p->state=s;s->owner=p;s->original=a;s->source=a->source;s->connection=a->source->smb;
    s->current=guard;s->job=job;s->epoch=epoch;s->deadline=a->source->deadline;s->binding=a->identity;s->map=a->bundle.map.bytes;s->map_size=a->bundle.map.size;s->digests=a->bundle.digests;s->digest_size=a->bundle.digest_size;
    memcpy(s->seal,a->folder.snapshot.sha256,32);
    if(!a->bundle.ready||!s->map||!s->digests||s->map_size<40||s->digest_size<56||a->failed||a->error||a->busy||bw_source_snapshot_validate(&a->folder.snapshot)||current(s)||bw_source_provider_current(a)||current(s)){p->busy=0;return fail(p,"batch_context_original_provider_refused");}
    cp_sha_bytes(s->map,s->map_size,s->map_sha256);cp_sha_bytes(s->digests,s->digest_size,s->digests_sha256);return 1;
}
static int prepare_running(const struct bw_native_prepare_selection *a,struct bw_native_jobs *jobs,const struct bw_native_job_token *token,const struct bw_source_provider *p){
    if(!a||!jobs||!token||token->owner!=jobs||!token->generation||memcmp(&a->identity,&p->identity,sizeof a->identity)||memcmp(token->id,a->run,33))return -1;
    unsigned nz=0;for(unsigned i=0;i<32;i++){if(!a->run[i]||!strchr("0123456789abcdef",a->run[i]))return -1;nz|=a->run[i]!='0';}if(a->run[32]||!nz)return -1;
    struct bw_native_job_view *v=calloc(1,sizeof *v);if(!v)return -1;int bad=bw_native_jobs_snapshot(jobs,v);unsigned matches=0;
    if(!bad&&v->count<=BW_NATIVE_JOB_LIMIT)for(size_t i=0;i<v->count;i++){const struct bw_native_job_record *j=&v->jobs[i];if(j->state!=BW_JOB_RUNNING&&j->state!=BW_JOB_QUEUED)continue;
        if(j->state!=BW_JOB_RUNNING||j->generation!=token->generation||memcmp(j->id,token->id,33)||!memchr(j->source,0,sizeof j->source)||strcmp(j->source,a->identity.binding.source)){bad=1;break;}matches++;}
    else bad=1;free(v);return bad||matches!=1?-1:0;
}
int bw_native_batch_context_prepare(struct bw_native_batch_context *p,struct bw_source_provider *a,int (*guard)(void *),void *job,
    const struct bw_native_prepare_selection *accepted,struct bw_native_jobs *jobs,const struct bw_native_job_token *token){
    if(!p||!a||prepare_running(accepted,jobs,token,a))return fail(p,"batch_context_actual_running_preparation_required");
    int rc=start(p,a,guard,job,token->generation);if(rc<=0)return rc;struct nbc_state *s=p->state;
    rc=open_batch(s);if(rc==1){const struct bw_batch_material_request request={.plan=&s->plan,.accepted=accepted,.jobs=jobs,.token=token};
        if(bw_batch_material_open(&s->material,&request)||current(s)){fail(p,s->material.error?s->material.error:"batch_context_material_refused");rc=-1;}else s->prepared=1;}
    if(!rc&&(bw_batch_material_destroy(&s->material)||bw_batch_plan_destroy(&s->plan)||bw_packed_bootstrap_destroy(&s->cache)||bw_native_derived_context_destroy(&s->project)))rc=fail(p,"batch_context_inapplicable_cleanup_refused");
    p->busy=0;if(rc<0)fail(p,p->error?p->error:"batch_context_prepare_refused");return rc;
}
int bw_native_batch_context_reopen(struct bw_native_batch_context *p,struct bw_source_provider *a,int (*guard)(void *),void *job,uint64_t epoch,const struct bw_batch_facts *history){
    uint8_t *expected=NULL,*actual=NULL;size_t en=0,an=0;const char *error=NULL;
    if(!p||!a||!history||bw_batch_provenance_encode(history,&expected,&en,&error))return fail(p,error?error:"batch_context_historical_facts_refused");
    int rc=-1;
    if(memcmp(&a->identity,&history->original,sizeof a->identity)||memcmp(a->folder.snapshot.sha256,history->bootstrap.native_snapshot_sha256,32)||a->bundle.map.size!=history->base_map_size||a->bundle.digest_size!=history->base_digest_size||
        !a->bundle.map.bytes||!a->bundle.digests||memcmp(a->bundle.map.bytes,history->base_map,history->base_map_size)||memcmp(a->bundle.digests,history->base_digests,history->base_digest_size)){fail(p,"batch_context_historical_original_changed");goto done;}
    int begun=start(p,a,guard,job,epoch);if(begun<=0){fail(p,"batch_context_historical_worker_refused");goto done;}struct nbc_state *s=p->state;
    if(open_batch(s)!=1){fail(p,"batch_context_historical_batch_changed");p->busy=0;goto done;}
    const struct bw_batch_facts *f=bw_batch_plan_view(&s->plan);
    if(!f||bw_batch_provenance_encode(f,&actual,&an,&error)||an!=en||memcmp(actual,expected,en)||current(s))fail(p,error?error:"batch_context_historical_commitment_changed");else rc=0;
    p->busy=0;
done:free(expected);free(actual);return rc;
}
static struct nbc_state *access(struct bw_native_batch_context *p){
    if(!p||!p->state||p->failed)return NULL;struct nbc_state *s=p->state;
    if(p->busy||s->inside_callback||s->cache.busy||s->plan.busy||s->material.busy||s->project.local.busy||(s->project.project&&s->project.project->busy)||s->original->busy){fail(p,"batch_context_outer_access_reentry");return NULL;}
    if(!s->applicable)return NULL;p->busy=1;int bad=current(s)||bw_batch_plan_request_matches(&s->plan,&s->request);p->busy=0;return bad?NULL:s;
}
const struct bw_batch_plan_request *bw_native_batch_context_request(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s?&s->request:NULL;}
struct bw_batch_plan *bw_native_batch_context_plan(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s?&s->plan:NULL;}
struct bw_batch_material *bw_native_batch_context_material(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s&&s->prepared&&bw_batch_material_view(&s->material)?&s->material:NULL;}
struct bw_packed_bootstrap *bw_native_batch_context_cache(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s?&s->cache:NULL;}
struct bw_native_derived_context *bw_native_batch_context_project(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s&&s->project.project?&s->project:NULL;}
const struct bw_batch_facts *bw_native_batch_context_view(struct bw_native_batch_context *p){struct nbc_state *s=access(p);return s?bw_batch_plan_view(&s->plan):NULL;}
int bw_native_batch_context_revalidate(struct bw_native_batch_context *p){struct nbc_state *s=access(p);if(!s)return -1;p->busy=1;int bad=bw_batch_plan_revalidate(&s->plan)||current(s);p->busy=0;return bad?fail(p,"batch_context_revalidation_refused"):0;}
int bw_native_batch_context_finish(struct bw_native_batch_context *p){struct nbc_state *s=access(p);if(!s)return -1;if(s->finished)return fail(p,"batch_context_finish_already_consumed");p->busy=1;
    int bad=bw_batch_plan_revalidate(&s->plan)||bw_packed_bootstrap_finish(&s->cache)||bw_batch_plan_revalidate(&s->plan)||current(s);if(!bad)s->finished=1;p->busy=0;return bad?fail(p,"batch_context_finish_refused"):0;}
int bw_native_batch_context_destroy(struct bw_native_batch_context *p){
    if(!p)return -1;if(p->busy)return fail(p,"batch_context_destroy_reentry");struct nbc_state *s=p->state;
    if(s){if(s->inside_callback||s->cache.busy||s->plan.busy||s->material.busy||s->project.local.busy||(s->project.project&&s->project.project->busy)||s->original->busy)return fail(p,"batch_context_child_still_active");
        if(bw_batch_material_destroy(&s->material)||bw_batch_plan_destroy(&s->plan)||bw_packed_bootstrap_destroy(&s->cache)||bw_native_derived_context_destroy(&s->project))return fail(p,"batch_context_destroy_unconfirmed");
        memset(s,0,sizeof *s);free(s);}memset(p,0,sizeof *p);return 0;
}
