#include "derived_material.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void u32(struct cp_sha *h,uint32_t n){uint8_t b[4];for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(n>>(8*i));cp_sha_update(h,b,4);}
static void lp(struct cp_sha *h,const char *s){size_t n=strlen(s);u32(h,(uint32_t)n);cp_sha_update(h,s,n);}
static int text(const char *s,size_t cap){return s&&memchr(s,0,cap)!=NULL;}
static int path(const char *s,size_t cap){if(!text(s,cap))return -1;size_t n=strlen(s);if(!n||n>240||bw_manifest_validate_path(s,n))return -1;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]>=127)return -1;return 0;}
int bw_derived_material_rule_sha256(const struct bw_pp_plan *p,uint8_t out[32]){
    if(!out)return -1;memset(out,0,32);
    if(!p||path(p->archive,sizeof p->archive)||path(p->proposed_destination,sizeof p->proposed_destination)||
       !text(p->requested,sizeof p->requested)||!text(p->listed,sizeof p->listed)||strcmp(p->requested,p->listed)||
       strlen(p->requested)!=9u+strlen(p->proposed_destination)||memcmp(p->requested,"../../../",9)||strcmp(p->requested+9,p->proposed_destination))return -1;
    struct cp_sha h;cp_sha_init(&h);lp(&h,BW_DERIVED_MATERIAL_RULE_DOMAIN);u32(&h,1);u32(&h,1);u32(&h,BW_DERIVED_PLAN_POLICY);
    lp(&h,BW_DERIVED_MATERIAL_RULE_SEMANTICS);lp(&h,p->archive);lp(&h,p->requested);lp(&h,p->listed);lp(&h,p->proposed_destination);cp_sha_final(&h,out);return 0;
}
void bw_derived_material_policy_sha256(uint8_t out[32]){if(!out)return;struct cp_sha h;cp_sha_init(&h);lp(&h,BW_DERIVED_MATERIAL_DOMAIN);u32(&h,BW_DERIVED_MATERIAL_POLICY);lp(&h,BW_DERIVED_MATERIAL_RULE_DOMAIN);lp(&h,BW_DERIVED_MATERIAL_RULE_SEMANTICS);u32(&h,1);u32(&h,1);u32(&h,BW_DERIVED_PLAN_POLICY);cp_sha_final(&h,out);}
struct dm_state {
    struct bw_derived_material_request request;
    struct bw_derived_plan_request plan;
    struct bw_native_prepare_selection accepted;
    struct bw_native_job_token token;
    struct bw_derived_destination_grant grant;
    struct bw_derived_plan_view captured_plan;
    struct bw_derived_material_view visible,captured;
    struct bw_native_job_view *jobs;
    const void *local_state,*parser_state;
    struct bw_source_smb *source;struct smb2_context *connection;double deadline;
    const void *base_map,*base_digests;size_t map_size,digest_size;
};
_Static_assert(sizeof(struct dm_state)<=8192,"material owned state bound");
_Static_assert(sizeof(struct bw_native_job_view)<=32768,"material job snapshot scratch bound");
static int fail(struct bw_derived_material *p,const char *reason){if(p){p->failed=1;p->error=reason;if(p->state)memset(&((struct dm_state *)p->state)->visible,0,sizeof(struct bw_derived_material_view));}return -1;}
static int identity(const struct bw_provider_binding *a,const struct bw_provider_binding *b){return a->kind==b->kind&&a->policy==b->policy&&!memcmp(&a->binding,&b->binding,sizeof a->binding);}
/* Catalog QUERY_INFO is microsecond precision; preserve the admitted normal
 * comparison here only. The sealed snapshot/commitments remain byte exact. */
static int info(const struct bw_source_info *a,const struct bw_source_info *b){return a&&b&&a->size==b->size&&a->inode==b->inode&&a->mtime_ticks/10u==b->mtime_ticks/10u&&a->ctime_ticks/10u==b->ctime_ticks/10u&&a->attributes==b->attributes;}
static int running(struct dm_state *s){
    const struct bw_derived_material_request *r=&s->request;
    if(!r->jobs||r->token->owner!=r->jobs||s->token.owner!=r->jobs||memcmp(r->token,&s->token,sizeof s->token)||!s->token.generation||
       !text(s->token.id,sizeof s->token.id)||strlen(s->token.id)!=32||strcmp(s->token.id,s->accepted.run)||bw_native_jobs_snapshot(r->jobs,s->jobs)||s->jobs->count>BW_NATIVE_JOB_LIMIT)return -1;
    unsigned found=0;for(size_t i=0;i<s->jobs->count;i++){const struct bw_native_job_record *j=&s->jobs->jobs[i];
        if(j->state!=BW_JOB_QUEUED&&j->state!=BW_JOB_RUNNING)continue;
        if(j->state!=BW_JOB_RUNNING||!text(j->id,sizeof j->id)||!text(j->source,sizeof j->source)||strcmp(j->id,s->token.id)||j->generation!=s->token.generation||strcmp(j->source,s->accepted.identity.binding.source))return -1;found++;
    }return found==1?0:-1;
}
static int base(struct dm_state *s,const struct bw_derived_plan_view *v){
    struct bw_source_provider *p=s->plan.original;const struct bw_native_prepare_selection *a=s->request.accepted;
    if(memcmp(a,&s->accepted,sizeof *a)||memcmp(s->request.plan,&s->plan,sizeof s->plan)||memcmp(s->plan.destination,&s->grant,sizeof s->grant)||
       p->failed||p->busy||!p->bundle.ready||!p->folder.verified||p->identity.kind!=BW_PROVIDER_FOLDER||p->identity.policy!=1||
       !identity(&a->identity,&p->identity)||p->current!=s->plan.current_job||p->opaque!=s->plan.job||p->source!=s->source||
       !s->source||s->source->smb!=s->connection||!s->connection||s->source->error||s->source->deadline!=s->deadline||
       s->request.local->state!=s->local_state||s->plan.project->state!=s->parser_state||s->request.local->busy||s->request.local->failed||
       a->container_kind||a->logical_boot_size||bw_source_profile_name(a->identity.binding.title)!=NULL||
       p->bundle.map.bytes!=s->base_map||p->bundle.digests!=s->base_digests||p->bundle.map.size!=s->map_size||p->bundle.digest_size!=s->digest_size||
       bw_source_snapshot_validate(&p->folder.snapshot)||memcmp(p->bundle.generation_sha256,p->folder.snapshot.sha256,32))return -1;
    if(!text(p->bundle.source,sizeof p->bundle.source)||!text(p->bundle.game,sizeof p->bundle.game)||!text(p->bundle.title,sizeof p->bundle.title)||
       strcmp(p->bundle.source,a->identity.binding.source)||strcmp(p->bundle.game,a->identity.binding.game)||strcmp(p->bundle.title,a->identity.binding.title))return -1;
    uint8_t profile[32],h[32];if(bw_source_profile_identity(a->identity.binding.title,profile)||memcmp(profile,p->folder.profile_sha256,32))return -1;
    const struct bw_source_info *boot=bw_source_snapshot_find(&p->folder.snapshot,"eboot.bin"),*param=bw_source_snapshot_find(&p->folder.snapshot,"sce_sys/param.json");
    if(!info(boot,&a->boot)||!info(param,&a->param)||!boot->size)return -1;
    struct bw_manifest m;if(bw_manifest_open(&m,p->bundle.map.bytes,s->map_size)||!m.count||m.count>BW_PP_MAX_INVENTORY||s->digest_size!=56u+32u*m.count)return -1;
    char selection[2048];int n=snprintf(selection,sizeof selection,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%016"PRIx64"\n",a->run,a->identity.binding.title,a->identity.binding.game,boot->size,m.count,m.checksum);
    if(n<0||(size_t)n>=sizeof selection||!text(p->bundle.selection,sizeof p->bundle.selection)||!text(p->folder.selection,sizeof p->folder.selection)||strcmp(p->bundle.selection,selection)||strcmp(p->folder.selection,selection))return -1;
    cp_sha_bytes(s->base_map,s->map_size,h);if(memcmp(h,v->base_map_sha256,32))return -1;cp_sha_bytes(s->base_digests,s->digest_size,h);
    return memcmp(h,v->base_digests_sha256,32)||memcmp(p->folder.snapshot.sha256,v->base_snapshot_sha256,32)?-1:0;
}
static int inspect(struct dm_state *s,const struct bw_derived_plan_view **out){
    if(running(s)||s->plan.current_job(s->plan.job))return -1;
    const struct bw_derived_plan_view *v=bw_derived_local_view(s->request.local);const struct bw_pp_plan *p=bw_packed_project_view(s->plan.project);uint8_t rule[32];
    if(!v||!p||base(s,v)||bw_derived_material_rule_sha256(p,rule)||memcmp(rule,s->grant.mapping_rule_sha256,32)||
       s->grant.version!=1||!s->grant.owner||!s->grant.check||s->grant.epoch!=s->token.generation||strcmp(s->grant.destination,p->proposed_destination)||
       strcmp(p->source,s->accepted.identity.binding.source)||strcmp(p->game,s->accepted.identity.binding.game)||memcmp(p->native_snapshot_sha256,v->base_snapshot_sha256,32)||
       v->identity.policy!=BW_DERIVED_PLAN_POLICY||strcmp(v->identity.domain,BW_DERIVED_PLAN_DOMAIN)||v->count>BW_DERIVED_MAX_RECORDS||v->derived_id>=v->count||
       v->count!=s->plan.original->bundle.map.local_count+s->plan.original->bundle.map.stream_count+1u||
       v->local_count!=s->plan.original->bundle.map.local_count+1u||v->stream_count!=s->plan.original->bundle.map.stream_count||
       v->local_bytes>s->plan.startup_budget||s->plan.policy!=BW_DERIVED_PLAN_POLICY)return -1;
    struct bw_derived_destination_claim claim={.provider=s->plan.original,.project=s->plan.project,.current_job=s->plan.current_job,.job=s->plan.job};
    memcpy(claim.binding_sha256,s->accepted.identity.binding.sha256,32);memcpy(claim.snapshot_sha256,v->base_snapshot_sha256,32);memcpy(claim.base_map_sha256,v->base_map_sha256,32);
    memcpy(claim.base_digests_sha256,v->base_digests_sha256,32);memcpy(claim.recipe_sha256,v->recipe_sha256,32);memcpy(claim.destination,p->proposed_destination,strlen(p->proposed_destination)+1);
    if(s->grant.check(s->plan.destination,&claim)||s->plan.current_job(s->plan.job)||running(s))return -1;*out=v;return 0;
}
static void material_hash(const struct bw_derived_material_view *v,uint8_t out[32]){struct cp_sha h;cp_sha_init(&h);lp(&h,"Blackwire native prepared material refinement v1");
    cp_sha_update(&h,v->policy_sha256,32);cp_sha_update(&h,v->mapping_rule_sha256,32);u32(&h,(uint32_t)v->original.kind);u32(&h,v->original.policy);
    lp(&h,v->original.binding.source);lp(&h,v->original.binding.game);lp(&h,v->original.binding.title);cp_sha_update(&h,v->original.binding.sha256,32);
    cp_sha_update(&h,v->plan->base_snapshot_sha256,32);cp_sha_update(&h,v->plan->base_map_sha256,32);cp_sha_update(&h,v->plan->base_digests_sha256,32);
    cp_sha_update(&h,v->prepared.namespace_sha256,32);cp_sha_update(&h,v->prepared.commitment_sha256,32);cp_sha_final(&h,out);
}
int bw_derived_material_open(struct bw_derived_material *p,const struct bw_derived_material_request *r){
    if(!p)return -1;if(p->state||p->busy||p->failed)return fail(p,"material_open_reused");
    if(!r||!r->accepted||!r->jobs||!r->token||!r->plan||!r->local||!r->plan->original||!r->plan->project||!r->plan->destination||!r->plan->current_job||!r->plan->job)return fail(p,"material_arguments");
    if(!text(r->accepted->run,sizeof r->accepted->run)||strlen(r->accepted->run)!=32||!text(r->accepted->identity.binding.source,sizeof r->accepted->identity.binding.source)||
       !text(r->accepted->identity.binding.game,sizeof r->accepted->identity.binding.game)||!text(r->accepted->identity.binding.title,sizeof r->accepted->identity.binding.title)||
       r->accepted->identity.kind!=BW_PROVIDER_FOLDER||r->accepted->identity.policy!=1||!r->plan->original->source||!r->local->state||!r->plan->project->state)return fail(p,"material_unsupported_or_closed");
    struct bw_source_provider *b=r->plan->original;
    if(!b->bundle.map.bytes||!b->bundle.digests||b->bundle.map.size>40u+BW_PP_MAX_INVENTORY*(32u+241u)||!b->bundle.map.size||
       b->bundle.digest_size<56u||b->bundle.digest_size>56u+32u*BW_PP_MAX_INVENTORY||!(b->source->deadline>=0&&b->source->deadline<=DBL_MAX))return fail(p,"material_bounds");
    struct dm_state *s=calloc(1,sizeof *s);if(!s)return fail(p,"material_allocation");s->jobs=calloc(1,sizeof *s->jobs);if(!s->jobs){free(s);return fail(p,"material_allocation");}p->state=s;p->busy=1;
    s->request=*r;memcpy(&s->accepted,r->accepted,sizeof s->accepted);memcpy(&s->token,r->token,sizeof s->token);memcpy(&s->plan,r->plan,sizeof s->plan);memcpy(&s->grant,r->plan->destination,sizeof s->grant);
    s->source=b->source;s->connection=b->source->smb;s->deadline=b->source->deadline;s->local_state=r->local->state;s->parser_state=r->plan->project->state;
    s->base_map=b->bundle.map.bytes;s->base_digests=b->bundle.digests;s->map_size=b->bundle.map.size;s->digest_size=b->bundle.digest_size;
    const struct bw_derived_plan_view *v=NULL;if(inspect(s,&v)||p->failed){p->busy=0;return fail(p,"material_admission_refused");}
    memcpy(&s->captured_plan,v,sizeof *v);s->captured.policy=BW_DERIVED_MATERIAL_POLICY;s->captured.original=b->identity;s->captured.prepared=v->identity;s->captured.plan=v;
    bw_derived_material_policy_sha256(s->captured.policy_sha256);memcpy(s->captured.mapping_rule_sha256,s->grant.mapping_rule_sha256,32);material_hash(&s->captured,s->captured.material_sha256);
    memcpy(s->captured.run,s->accepted.run,33);s->captured.job_generation=s->token.generation;memcpy(&s->visible,&s->captured,sizeof s->visible);p->busy=0;return 0;
}
const struct bw_derived_material_view *bw_derived_material_view(struct bw_derived_material *p){
    if(!p||!p->state||p->failed)return NULL;if(p->busy){fail(p,"material_reentry");return NULL;}p->busy=1;struct dm_state *s=p->state;const struct bw_derived_plan_view *v=NULL;
    int bad=memcmp(&s->visible,&s->captured,sizeof s->visible)||inspect(s,&v)||p->failed||v!=s->captured.plan||memcmp(v,&s->captured_plan,sizeof *v);p->busy=0;
    if(bad){fail(p,"material_changed");return NULL;}return &s->visible;
}
int bw_derived_material_destroy(struct bw_derived_material *p){if(!p)return -1;if(p->busy)return fail(p,"material_destroy_busy");if(p->state){struct dm_state *s=p->state;free(s->jobs);free(s);}memset(p,0,sizeof *p);return 0;}
