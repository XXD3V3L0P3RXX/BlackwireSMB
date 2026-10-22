#include "derived_local.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct dl_state {
    struct bw_derived_plan plan;
    struct bw_derived_plan_request request;
    struct bw_derived_destination_grant grant;
    struct bw_derived_destination_claim claim;
    struct bw_derived_local_limits limits;
    struct bw_derived_local_stats stats;
    struct bw_derived_plan_view captured,visible;
    struct bw_pp_plan recipe;
    struct bw_source_smb *source;
    struct smb2_context *connection;
    double deadline,last_now;
};
_Static_assert(sizeof(struct dl_state)<=16384,"LOCAL reader state bound");
static int fail(struct bw_derived_local *p,const char *why){
    if(!p)return -1;p->failed=1;if(!p->error)p->error=why;
    if(p->state)memset(&((struct dl_state *)p->state)->visible,0,sizeof(struct bw_derived_plan_view));return -1;
}
static double now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int finite(double n){return n>=-DBL_MAX&&n<=DBL_MAX;}
static int same_grant(const struct bw_derived_destination_grant *a,const struct bw_derived_destination_grant *b){
    return a&&a->version==b->version&&a->owner==b->owner&&a->epoch==b->epoch&&a->check==b->check&&
        !memcmp(a->destination,b->destination,sizeof b->destination)&&!memcmp(a->mapping_rule_sha256,b->mapping_rule_sha256,32);
}
/* No provider/composer/parser calls here: actual provider_read is busy during
 * consume, and SMB metadata waits may also invoke this through an observer. */
static int clock_current(struct bw_derived_local *p){
    struct dl_state *s=p->state;struct bw_source_provider *a=s->request.original;double t=now();
    if(p->failed||s->plan.failed||!a||a->failed||a->error||a->source!=s->source||a->current!=s->request.current_job||a->opaque!=s->request.job||
       !s->source||!s->connection||s->source->smb!=s->connection||s->source->error||s->source->deadline!=s->deadline||
       !finite(t)||t<0||t<s->last_now||t>=s->deadline)return fail(p,"derived_local_current_or_deadline_refused");
    s->last_now=t;return 0;
}
static int current(void *raw){
    struct bw_derived_local *p=raw;struct dl_state *s=p->state;
    if(clock_current(p)||!same_grant(s->request.destination,&s->grant))return fail(p,"derived_local_grant_changed");
    if(s->request.current_job(s->request.job)||clock_current(p))return fail(p,"derived_local_job_refused");
    if(!same_grant(s->request.destination,&s->grant)||s->grant.check(s->request.destination,&s->claim)||
       clock_current(p)||!same_grant(s->request.destination,&s->grant))return fail(p,"derived_local_grant_refused");
    return 0;
}
static int same_recipe(const struct bw_pp_plan *a,const struct bw_pp_plan *b){
    return !strcmp(a->archive,b->archive)&&!strcmp(a->requested,b->requested)&&!strcmp(a->listed,b->listed)&&
       !strcmp(a->proposed_destination,b->proposed_destination)&&!strcmp(a->source,b->source)&&!strcmp(a->game,b->game)&&
       !memcmp(a->native_snapshot_sha256,b->native_snapshot_sha256,32)&&
       a->archive_info.size==b->archive_info.size&&a->archive_info.inode==b->archive_info.inode&&
       a->archive_info.mtime_ticks==b->archive_info.mtime_ticks&&a->archive_info.ctime_ticks==b->archive_info.ctime_ticks&&
       a->archive_info.attributes==b->archive_info.attributes&&a->entry_location==b->entry_location&&a->module_count==b->module_count&&
       !memcmp(a->spans,b->spans,sizeof a->spans)&&!memcmp(a->modules,b->modules,sizeof a->modules);
}
static int full(struct bw_derived_local *p){
    struct dl_state *s=p->state;
    if(current(p)||memcmp(&s->visible,&s->captured,sizeof s->visible)||bw_derived_plan_revalidate(&s->plan))return fail(p,"derived_local_full_validation_refused");
    const struct bw_derived_plan_view *v=bw_derived_plan_view(&s->plan);const struct bw_pp_plan *r=bw_packed_project_view(s->request.project);
    if(!v||!r||memcmp(v,&s->captured,sizeof *v)||!same_recipe(r,&s->recipe)||current(p))return fail(p,"derived_local_captured_plan_changed");
    return 0;
}
int bw_derived_local_open(struct bw_derived_local *p,const struct bw_derived_plan_request *r,const struct bw_derived_local_limits *limits){
    if(!p)return -1;
    if(p->state||p->busy||p->failed)return fail(p,"derived_local_open_state_refused");
    if(!r||!r->original||!r->original->source||!r->project||!r->current_job||!r->job||!r->destination||!limits||
       !limits->attempts||limits->attempts>BW_DERIVED_LOCAL_MAX_ATTEMPTS||!limits->consume_calls||limits->consume_calls>BW_DERIVED_LOCAL_MAX_CONSUME_CALLS||
       !limits->attempted_bytes||limits->attempted_bytes>BW_PROVIDER_MAX_LOCAL_BYTES||!limits->delivered_bytes||limits->delivered_bytes>BW_PROVIDER_MAX_LOCAL_BYTES||
       !finite(r->original->source->deadline))return fail(p,"derived_local_inputs_or_limits_refused");
    struct dl_state *s=calloc(1,sizeof *s);if(!s)return fail(p,"derived_local_allocation_refused");
    p->state=s;p->busy=1;s->request=*r;s->limits=*limits;s->grant=*r->destination;s->source=r->original->source;
    s->connection=s->source->smb;s->deadline=s->source->deadline;s->last_now=now();
    int bad=!finite(s->last_now)||s->last_now<0||s->last_now>=s->deadline||bw_derived_plan_open(&s->plan,r);
    const struct bw_derived_plan_view *v=bad?NULL:bw_derived_plan_view(&s->plan);
    const struct bw_pp_plan *recipe=bad?NULL:bw_packed_project_view(r->project);
    if(!v||!recipe)bad=1;
    if(!bad){s->captured=*v;s->visible=*v;s->recipe=*recipe;s->claim.provider=r->original;s->claim.project=r->project;
        s->claim.current_job=r->current_job;s->claim.job=r->job;
        memcpy(s->claim.binding_sha256,r->original->identity.binding.sha256,32);memcpy(s->claim.snapshot_sha256,v->base_snapshot_sha256,32);
        memcpy(s->claim.base_map_sha256,v->base_map_sha256,32);memcpy(s->claim.base_digests_sha256,v->base_digests_sha256,32);
        memcpy(s->claim.recipe_sha256,v->recipe_sha256,32);memcpy(s->claim.destination,s->grant.destination,sizeof s->claim.destination);
        bad=current(p);}
    p->busy=0;return bad?fail(p,"derived_local_open_refused"):0;
}
const struct bw_derived_plan_view *bw_derived_local_view(struct bw_derived_local *p){
    if(!p||!p->state)return NULL;if(p->busy||p->failed){fail(p,"derived_local_view_state_refused");return NULL;}
    struct dl_state *s=p->state;p->busy=1;const struct bw_derived_plan_view *v=NULL;
    if(!current(p)&&!memcmp(&s->visible,&s->captured,sizeof s->visible))v=bw_derived_plan_view(&s->plan);
    int bad=!v||memcmp(v,&s->captured,sizeof *v)||current(p);p->busy=0;
    return bad?(fail(p,"derived_local_view_refused"),(const struct bw_derived_plan_view *)NULL):&s->visible;
}
struct dl_read {struct bw_derived_local *reader;bw_source_consume_fn consume;void *opaque;struct cp_sha hash;uint64_t expected,size;};
static int consume(void *raw,const void *bytes,size_t size){
    struct dl_read *r=raw;struct bw_derived_local *p=r->reader;struct dl_state *s=p->state;
    if(current(p)||!bytes||!size||size>65536u||r->size>r->expected||size>r->expected-r->size||
       s->stats.consume_calls>=s->limits.consume_calls||s->stats.delivered_bytes>s->limits.delivered_bytes||
       size>s->limits.delivered_bytes-s->stats.delivered_bytes)return fail(p,"derived_local_chunk_or_delivery_limit_refused");
    s->stats.consume_calls++;s->stats.delivered_bytes+=size;r->size+=size;cp_sha_update(&r->hash,bytes,size);
    if(r->consume(r->opaque,bytes,size)||current(p))return fail(p,"derived_local_consumer_refused");return 0;
}
struct dl_watch {struct bw_derived_local *reader;bw_source_observer_fn prior;void *opaque;double deadline;int inside,latched;};
static void latch(struct dl_watch *w){
    struct dl_state *s=w->reader->state;w->latched=1;
    if(!s->source->error)s->source->error="derived_local_original_guard_refused";
    s->source->deadline=-1;
}
static void observe(void *raw,const struct bw_source_progress *progress){
    struct dl_watch *w=raw;
    if(w->inside){fail(w->reader,"derived_local_observer_reentry_refused");latch(w);return;}
    w->inside=1;if(current(w->reader))latch(w);
    if(!w->latched&&w->prior)w->prior(w->opaque,progress);
    if(current(w->reader))latch(w);w->inside=0;
}
static int original(struct bw_derived_local *p,const char *path,struct dl_read *reader){
    struct dl_state *s=p->state;struct dl_watch w={p,s->source->observe,s->source->observe_opaque,s->source->deadline,0,0};
    if(s->source->observe==observe)return fail(p,"derived_local_nested_original_refused");
    s->source->observe=observe;s->source->observe_opaque=&w;
    int result=bw_source_provider_read(s->request.original,path,consume,reader);
    /* All queued callbacks have completed or the context has been destroyed by
     * the existing synchronous transport before this scoped state can leave. */
    s->source->observe=w.prior;s->source->observe_opaque=w.opaque;
    if(w.latched)s->source->deadline=w.deadline;
    return result||w.latched?fail(p,"derived_local_original_read_refused"):0;
}
static int origin(struct bw_derived_local *p,uint32_t id,const struct bw_manifest_record *r){
    struct dl_state *s=p->state;const struct bw_derived_plan_view *v=&s->captured;const struct bw_derived_origin *o=v->origins+id;
    if(o->file_id!=id)return fail(p,"derived_local_origin_id_refused");
    struct bw_manifest old;struct bw_manifest_record a;
    if(bw_manifest_open(&old,s->request.original->bundle.map.bytes,s->request.original->bundle.map.size))return fail(p,"derived_local_original_map_refused");
    const uint8_t *expected=v->digests+56u+32u*id;
    if(o->kind==BW_DERIVED_ORIGINAL){
        if(id==v->derived_id||o->original_id>=old.count||o->archive_original_id!=UINT32_MAX||o->recipe_index!=UINT32_MAX||
           bw_manifest_get(&old,o->original_id,&a)||a.flags!=BW_MANIFEST_LOCAL||strcmp(a.path,r->path)||a.size!=r->size||a.mtime_sec!=r->mtime_sec||
           memcmp(expected,s->request.original->bundle.digests+56u+32u*o->original_id,32))return fail(p,"derived_local_original_origin_refused");
    }else if(o->kind==BW_DERIVED_PACKED_PROJECT){
        const struct bw_pp_span *body=&s->recipe.spans[BW_PP_BODY];
        if(id!=v->derived_id||o->original_id!=UINT32_MAX||o->archive_original_id!=v->archive_original_id||o->recipe_index||
           bw_manifest_get(&old,o->archive_original_id,&a)||a.flags!=BW_MANIFEST_STREAM||strcmp(a.path,s->recipe.archive)||a.size!=s->recipe.archive_info.size||
           strcmp(r->path,s->recipe.proposed_destination)||r->mtime_sec||!body->size||body->size>BW_PP_MAX_BODY||r->size!=body->size||
           body->offset>s->recipe.archive_info.size||body->size>s->recipe.archive_info.size-body->offset||memcmp(expected,body->sha256,32))return fail(p,"derived_local_packed_origin_refused");
    }else return fail(p,"derived_local_unknown_origin_refused");return 0;
}
int bw_derived_local_read(struct bw_derived_local *p,uint32_t id,bw_source_consume_fn fn,void *opaque){
    if(!p||!p->state)return -1;if(p->busy||p->failed)return fail(p,"derived_local_read_state_refused");
    struct dl_state *s=p->state;p->busy=1;int bad=1;struct bw_manifest m;struct bw_manifest_record r;
    if(s->stats.attempts>=s->limits.attempts){fail(p,"derived_local_attempt_limit_refused");goto done;}s->stats.attempts++;
    if(!fn||bw_manifest_open(&m,s->captured.map,s->captured.map_size)||id>=m.count||bw_manifest_get(&m,id,&r)||r.flags!=BW_MANIFEST_LOCAL){fail(p,"derived_local_id_or_class_refused");goto done;}
    if(s->stats.attempted_bytes>s->limits.attempted_bytes||r.size>s->limits.attempted_bytes-s->stats.attempted_bytes){fail(p,"derived_local_attempt_byte_limit_refused");goto done;}
    s->stats.attempted_bytes+=r.size;
    if(full(p)||origin(p,id,&r))goto done;
    struct dl_read reader={.reader=p,.consume=fn,.opaque=opaque,.expected=r.size};cp_sha_init(&reader.hash);
    const struct bw_derived_origin *o=s->captured.origins+id;
    if(o->kind==BW_DERIVED_ORIGINAL)bad=original(p,r.path,&reader);
    else {const struct bw_pp_span *body=&s->recipe.spans[BW_PP_BODY];
        bad=bw_source_smb_read_range(s->source,&s->request.original->folder.snapshot,s->recipe.archive,body->offset,body->size,current,p,consume,&reader);}
    if(bad||p->failed||reader.size!=r.size){fail(p,"derived_local_body_incomplete");bad=1;goto done;}
    uint8_t actual[32];cp_sha_final(&reader.hash,actual);
    if(memcmp(actual,s->captured.digests+56u+32u*id,32)||full(p)||current(p)){fail(p,"derived_local_digest_or_terminal_validation_refused");bad=1;goto done;}
    s->stats.completed++;bad=0;
done:p->busy=0;return bad?fail(p,"derived_local_read_refused"):0;
}
int bw_derived_local_stats(struct bw_derived_local *p,struct bw_derived_local_stats *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!p||!p->state)return -1;
    if(p->busy)return fail(p,"derived_local_stats_busy_refused");*out=((struct dl_state *)p->state)->stats;return 0;
}
int bw_derived_local_destroy(struct bw_derived_local *p){
    if(!p)return -1;if(p->busy)return fail(p,"derived_local_destroy_busy_refused");
    if(p->state){struct dl_state *s=p->state;if(bw_derived_plan_destroy(&s->plan))return fail(p,"derived_local_plan_destroy_refused");free(s);}
    memset(p,0,sizeof *p);return 0;
}
