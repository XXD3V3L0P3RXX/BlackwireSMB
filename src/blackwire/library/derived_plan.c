#include "derived_plan.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DP_MAP_LIMIT (BW_MANIFEST_HEADER_SIZE+BW_DERIVED_MAX_RECORDS*(BW_MANIFEST_RECORD_SIZE+241u))
#define DP_DIGEST_LIMIT (56u+32u*BW_DERIVED_MAX_RECORDS)
struct dp_state {
    struct bw_derived_plan_request request;
    struct bw_derived_destination_grant grant;
    struct bw_derived_destination_claim claim;
    struct bw_provider_binding binding;
    struct bw_source_smb *source;
    struct smb2_context *connection;
    struct bw_source_snapshot snapshot;
    struct bw_source_map base;
    const uint8_t *base_digests;
    size_t base_digest_size;
    uint64_t provider_budget;
    struct bw_pp_plan recipe;
    struct bw_derived_plan_view view;
    struct bw_source_map map;
    uint8_t *digests;
    struct bw_derived_origin *origins;
    size_t digest_allocated;
    uint32_t output_count;
};
_Static_assert(sizeof(struct dp_state)<16384,"derived state bound");
_Static_assert(sizeof(struct bw_derived_origin)==20,"origin in-memory bound only; hash fields explicitly");
static int fail(struct bw_derived_plan *p,const char *why){
    if(!p)return -1;p->failed=1;if(!p->error)p->error=why;
    if(p->state)memset(&((struct dp_state *)p->state)->view,0,sizeof(struct bw_derived_plan_view));return -1;
}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t le64(const uint8_t *p){return le32(p)|(uint64_t)le32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static void h32(struct cp_sha *s,uint32_t n){uint8_t p[4];put32(p,n);cp_sha_update(s,p,4);}
static void h64(struct cp_sha *s,uint64_t n){uint8_t p[8];put64(p,n);cp_sha_update(s,p,8);}
static void text(struct cp_sha *s,const char *p){size_t n=strlen(p);h32(s,(uint32_t)n);cp_sha_update(s,p,n);}
static void begin(struct cp_sha *s,const char *domain){cp_sha_init(s);text(s,domain);}
static int zero(const void *raw,size_t n){const uint8_t *p=raw;for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static int string(const char *p,size_t cap,int empty){
    if(!p)return -1;const char *end=memchr(p,0,cap);if(!end||(!empty&&end==p))return -1;
    for(const char *at=p;at<end;at++)if((unsigned char)*at<32||(unsigned char)*at>=127)return -1;return 0;
}
/* Same deliberately narrower printable-ASCII path policy as packed_project.
 * General Unicode HOST paths and their case aliases are unsupported. */
static int path(const char *p){return string(p,241,0)||bw_manifest_validate_path(p,strlen(p))||strpbrk(p,"~%")?-1:0;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int compare(const char *a,const char *b){
    while(*a&&fold((unsigned char)*a)==fold((unsigned char)*b)){a++;b++;}
    unsigned x=fold((unsigned char)*a),y=fold((unsigned char)*b);return x<y?-1:x>y;
}
static int same_info(const struct bw_source_info *a,const struct bw_source_info *b){
    return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;
}
static int binding_ok(const struct bw_provider_binding *b){
    const struct bw_source_binding *a=&b->binding;if(b->kind!=BW_PROVIDER_FOLDER||b->policy!=BW_PROVIDER_POLICY_V1||
        string(a->source,sizeof a->source,0)||bw_manifest_validate_path(a->source,strlen(a->source))||
        string(a->game,sizeof a->game,0)||string(a->title,sizeof a->title,0)||strlen(a->title)!=9||memcmp(a->title,"PPSA",4)||
        strlen(a->game)!=74||memcmp(a->game,a->title,9)||a->game[9]!='_'||zero(a->sha256,32))return -1;
    for(unsigned i=4;i<9;i++)if(a->title[i]<'0'||a->title[i]>'9')return -1;
    static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++)if(a->game[10+2*i]!=hex[a->sha256[i]>>4]||a->game[11+2*i]!=hex[a->sha256[i]&15])return -1;return 0;
}
static int same_binding(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
    return a->kind==b->kind&&a->policy==b->policy&&!strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&
        !strcmp(a->binding.title,b->binding.title)&&!memcmp(a->binding.sha256,b->binding.sha256,32);
}
static int guard(struct bw_derived_plan *p){
    struct dp_state *s=p->state;struct bw_source_provider *a=s->request.original;struct timespec at;
    if(p->failed||!s->request.current_job||!a||a->failed||a->error||a->busy||a->source!=s->source||!s->source||s->source->smb!=s->connection||
       !s->connection||s->source->error||a->current!=s->request.current_job||a->opaque!=s->request.job||
       !(s->source->deadline>=-DBL_MAX&&s->source->deadline<=DBL_MAX)||clock_gettime(CLOCK_MONOTONIC,&at)||
       s->source->deadline<=(double)at.tv_sec+(double)at.tv_nsec/1e9)return fail(p,"derived_live_job_or_deadline_refused");
    int bad=s->request.current_job(s->request.job);
    if(bad||p->failed)return fail(p,"derived_current_job_refused");
    if(clock_gettime(CLOCK_MONOTONIC,&at)||s->source->deadline<=(double)at.tv_sec+(double)at.tv_nsec/1e9)return fail(p,"derived_deadline_refused");return 0;
}
static int hashed(struct bw_derived_plan *p,const void *raw,size_t n,uint8_t out[32]){
    struct cp_sha sha;cp_sha_init(&sha);const uint8_t *b=raw;
    for(size_t at=0;at<n;){size_t part=n-at;if(part>65536)part=65536;if(guard(p))return -1;cp_sha_update(&sha,b+at,part);at+=part;}
    cp_sha_final(&sha,out);return guard(p);
}
static int same_map(const struct bw_source_map *a,const struct bw_source_map *b){return a->size==b->size&&a->local_bytes==b->local_bytes&&
    a->remote_bytes==b->remote_bytes&&a->bootstrap_bytes==b->bootstrap_bytes&&a->local_count==b->local_count&&a->stream_count==b->stream_count;}
static int inventory(struct bw_derived_plan *p,const struct bw_source_snapshot *snap,const char *dest){
    size_t at=0,rows=0,dn=strlen(dest);
    while(at<snap->path_size){
        const char *name=snap->paths+at,*end=memchr(name,0,snap->path_size-at);if(!end)return fail(p,"derived_inventory_storage_refused");
        size_t n=(size_t)(end-name);at+=n+1;rows++;const struct bw_source_info *info=bw_source_snapshot_find(snap,name);
        if(!info)return fail(p,"derived_inventory_lookup_refused");if(!n)continue;if(path(name))return fail(p,"derived_inventory_policy_refused");
        for(size_t i=0;i<n;i++)if(name[i]=='/'){char parent[241];memcpy(parent,name,i);parent[i]=0;
            const struct bw_source_info *value=bw_source_snapshot_find(snap,parent);if(!value||!(value->attributes&0x10u))return fail(p,"derived_inventory_parent_refused");}
        for(size_t i=0;i<=dn;i++)if(!dest[i]||dest[i]=='/'){char part[241];memcpy(part,dest,i);part[i]=0;
            /* Preserve every original spelling; only directory ancestors may
             * coexist at the exact new literal destination's different case. */
            if(!compare(name,part)&&!(i<dn&&(info->attributes&0x10u)))return fail(p,"derived_destination_collision");}
        /* A pre-existing child of a proposed file is also a conflict. */
        if(n>dn&&name[dn]=='/'){char part[241];memcpy(part,name,dn);part[dn]=0;if(!compare(part,dest))return fail(p,"derived_destination_ancestor_collision");}
        if((rows&31u)==0&&guard(p))return -1;
    }
    return at==snap->path_size&&rows==snap->count?guard(p):fail(p,"derived_inventory_coverage_refused");
}
/* Validate ordinary typed native structures before invoking APIs which expect
 * those structures. These checks are not arbitrary C-pointer validation. */
static int base(struct bw_derived_plan *p,int capture){
    struct dp_state *s=p->state;struct bw_source_provider *a=s->request.original;struct bw_source_preparation *f=&a->folder;
    const struct bw_source_binding *b=&a->identity.binding;struct bw_source_snapshot *snap=&f->snapshot;
    if(guard(p)||binding_ok(&a->identity)||!a->bundle.ready||!f->verified||a->budget!=s->provider_budget||!a->budget||a->budget>BW_PROVIDER_MAX_LOCAL_BYTES||
       s->request.startup_budget>a->budget||!s->request.startup_budget||bw_source_profile_name(b->title)||!zero(f->profile_sha256,32)||
       string(f->title,sizeof f->title,0)||strcmp(f->title,b->title)||string(a->bundle.source,sizeof a->bundle.source,0)||
       string(a->bundle.game,sizeof a->bundle.game,0)||string(a->bundle.title,sizeof a->bundle.title,0)||
       strcmp(a->bundle.source,b->source)||strcmp(a->bundle.game,b->game)||strcmp(a->bundle.title,b->title)||
       a->bundle.map.bytes!=f->map.bytes||!same_map(&a->bundle.map,&f->map)||a->bundle.digests!=f->digests||a->bundle.digest_size!=f->digest_size||
       !f->map.bytes||f->map.size>DP_MAP_LIMIT||!f->digests||f->digest_size>DP_DIGEST_LIMIT||
       !snap->parent_count||snap->parent_count>BW_SOURCE_SNAPSHOT_MAX_PARENTS||!snap->count||snap->count>BW_PP_MAX_INVENTORY+1u||
       !snap->paths||!snap->entries||snap->path_size>1u+BW_PP_MAX_INVENTORY*241u||
       string(snap->source,sizeof snap->source,0)||string(snap->game,sizeof snap->game,0)||strcmp(snap->source,b->source)||strcmp(snap->game,b->game)||
       memcmp(a->bundle.generation_sha256,snap->sha256,32))return fail(p,"derived_original_structure_refused");
    if(!capture&&(f->map.bytes!=s->base.bytes||!same_map(&f->map,&s->base)||f->digests!=s->base_digests||f->digest_size!=s->base_digest_size||
       snap->paths!=s->snapshot.paths||snap->entries!=s->snapshot.entries||snap->count!=s->snapshot.count||snap->path_size!=s->snapshot.path_size||
       !same_binding(&a->identity,&s->binding)||memcmp(snap->sha256,s->snapshot.sha256,32)))return fail(p,"derived_original_identity_changed");
    if(bw_source_snapshot_validate(snap)||inventory(p,snap,s->grant.destination))return fail(p,"derived_original_seal_or_inventory_refused");
    struct bw_manifest m;if(bw_manifest_open(&m,f->map.bytes,f->map.size)||!m.count||m.count>BW_PP_MAX_INVENTORY||
       f->digest_size!=56u+32u*m.count||memcmp(f->digests,"BWDIG001",8)||le32(f->digests+8)!=1||le32(f->digests+12)!=m.count||le64(f->digests+16)!=m.checksum)return fail(p,"derived_original_wire_refused");
    uint8_t mh[32],dh[32];if(hashed(p,f->map.bytes,f->map.size,mh)||hashed(p,f->digests,f->digest_size,dh)||memcmp(mh,f->digests+24,32))return fail(p,"derived_original_digest_refused");
    if(!capture&&(memcmp(mh,s->claim.base_map_sha256,32)||memcmp(dh,s->claim.base_digests_sha256,32)))return fail(p,"derived_original_bytes_changed");
    struct bw_source_plan plan={0};struct bw_source_map canonical={0};int bad=bw_source_plan_init(&plan,b->game);
    for(uint32_t i=0;!bad&&i<m.count;i++){
        struct bw_manifest_record r;if(guard(p)||bw_manifest_get(&m,i,&r)||path(r.path)){bad=1;break;}
        const struct bw_source_info *info=bw_source_snapshot_find(snap,r.path);
        if(!info||(info->attributes&(0x10u|0x400u))||info->size!=r.size||bw_source_unix_mtime(info->mtime_ticks)!=r.mtime_sec||
           (r.flags==BW_MANIFEST_STREAM&&!zero(f->digests+56u+32u*i,32))||bw_source_plan_add(&plan,r.path,r.size,r.mtime_sec)){bad=1;break;}
    }
    if(!bad)bad=bw_source_plan_finish(&plan,&canonical);
    if(!bad)bad=!same_map(&f->map,&canonical)||memcmp(f->map.bytes,canonical.bytes,canonical.size)||f->map.local_bytes>s->request.startup_budget;
    bw_source_plan_destroy(&plan);bw_source_map_destroy(&canonical);if(bad)return fail(p,"derived_original_canonical_map_refused");
    if(capture){s->binding=a->identity;s->base=f->map;s->base_digests=f->digests;s->base_digest_size=f->digest_size;s->snapshot=*snap;
        memcpy(s->claim.binding_sha256,b->sha256,32);memcpy(s->claim.snapshot_sha256,snap->sha256,32);
        memcpy(s->claim.base_map_sha256,mh,32);memcpy(s->claim.base_digests_sha256,dh,32);}
    return guard(p);
}
static void recipe_hash(const struct bw_pp_plan *r,uint8_t out[32]){
    struct cp_sha h;begin(&h,"Blackwire native plain-project recipe facts v1");
    text(&h,r->archive);text(&h,r->requested);text(&h,r->listed);text(&h,r->proposed_destination);text(&h,r->source);text(&h,r->game);
    cp_sha_update(&h,r->native_snapshot_sha256,32);h64(&h,r->archive_info.size);h64(&h,r->archive_info.inode);
    h64(&h,r->archive_info.mtime_ticks);h64(&h,r->archive_info.ctime_ticks);h32(&h,r->archive_info.attributes);h32(&h,r->entry_location);
    for(unsigned i=0;i<BW_PP_SPAN_COUNT;i++){h64(&h,r->spans[i].offset);h64(&h,r->spans[i].size);cp_sha_update(&h,r->spans[i].sha256,32);cp_sha_update(&h,r->spans[i].sha1,20);}
    h32(&h,r->module_count);for(uint32_t i=0;i<r->module_count;i++)text(&h,r->modules[i]);cp_sha_final(&h,out);
}
int bw_derived_plan_recipe_sha256(const struct bw_pp_plan *r,uint8_t out[32]){
    if(!out)return -1;memset(out,0,32);
    if(!r||r->module_count>BW_PP_MAX_MODULES||path(r->archive)||path(r->proposed_destination)||
       string(r->requested,sizeof r->requested,0)||string(r->listed,sizeof r->listed,0)||
       string(r->source,sizeof r->source,0)||string(r->game,sizeof r->game,0))return -1;
    for(uint32_t i=0;i<r->module_count;i++)if(string(r->modules[i],sizeof r->modules[i],0))return -1;
    recipe_hash(r,out);return 0;
}
static int recipe(struct bw_derived_plan *p,int capture){
    struct dp_state *s=p->state;if(guard(p))return -1;const struct bw_pp_plan *r=bw_packed_project_view(s->request.project);
    if(!r||p->failed||path(r->archive)||path(r->proposed_destination)||string(r->requested,sizeof r->requested,0)||string(r->listed,sizeof r->listed,0)||
       strcmp(r->requested,r->listed)||string(r->source,sizeof r->source,0)||string(r->game,sizeof r->game,0)||
       strcmp(r->source,s->binding.binding.source)||strcmp(r->game,s->binding.binding.game)||strcmp(r->proposed_destination,s->grant.destination)||
       memcmp(r->native_snapshot_sha256,s->snapshot.sha256,32)||r->module_count>BW_PP_MAX_MODULES||
       !r->spans[BW_PP_BODY].size||r->spans[BW_PP_BODY].size>BW_PP_MAX_BODY||r->spans[BW_PP_BODY].size>s->request.startup_budget-s->base.local_bytes)return fail(p,"derived_live_recipe_refused");
    const struct bw_source_info *info=bw_source_snapshot_find(&s->request.original->folder.snapshot,r->archive);
    if(!info||!same_info(info,&r->archive_info))return fail(p,"derived_recipe_archive_changed");
    for(uint32_t i=0;i<r->module_count;i++)if(string(r->modules[i],sizeof r->modules[i],0))return fail(p,"derived_recipe_module_refused");
    for(unsigned i=0;i<BW_PP_SPAN_COUNT;i++)if(!r->spans[i].size||r->spans[i].offset>info->size||r->spans[i].size>info->size-r->spans[i].offset)return fail(p,"derived_recipe_span_refused");
    uint8_t sha[32];recipe_hash(r,sha);
    if(capture){s->recipe=*r;memcpy(s->claim.recipe_sha256,sha,32);}
    else if(memcmp(sha,s->claim.recipe_sha256,32))return fail(p,"derived_recipe_facts_changed");return guard(p);
}
static int grant(struct bw_derived_plan *p){
    struct dp_state *s=p->state;const struct bw_derived_destination_grant *a=s->request.destination,*b=&s->grant;
    if(guard(p)||!a||a->version!=b->version||a->owner!=b->owner||a->epoch!=b->epoch||a->check!=b->check||
       memcmp(a->destination,b->destination,sizeof b->destination)||memcmp(a->mapping_rule_sha256,b->mapping_rule_sha256,32))return fail(p,"derived_destination_grant_changed");
    int bad=a->check(a,&s->claim);
    if(bad||p->failed)return fail(p,"derived_destination_grant_refused");
    if(a->version!=b->version||a->owner!=b->owner||a->epoch!=b->epoch||a->check!=b->check||
       memcmp(a->destination,b->destination,sizeof b->destination)||memcmp(a->mapping_rule_sha256,b->mapping_rule_sha256,32))return fail(p,"derived_destination_grant_changed");return guard(p);
}
static void policy_hash(uint8_t out[32]){
    struct cp_sha h;begin(&h,BW_DERIVED_PLAN_DOMAIN);h32(&h,BW_DERIVED_PLAN_POLICY);h32(&h,BW_PROVIDER_FOLDER);h32(&h,BW_PROVIDER_POLICY_V1);
    text(&h,"profile-none;original-records-preserved;derived-mtime-zero;literal-destination-grant;no-publication");
    h32(&h,BW_PP_MAX_INVENTORY);h32(&h,BW_PP_MAX_PATH);h32(&h,BW_PP_MAX_BODY);h64(&h,BW_PROVIDER_MAX_LOCAL_BYTES);cp_sha_final(&h,out);
}
static void identity(struct dp_state *s,struct bw_derived_prepared_identity *id){
    id->policy=BW_DERIVED_PLAN_POLICY;memcpy(id->domain,BW_DERIVED_PLAN_DOMAIN,sizeof BW_DERIVED_PLAN_DOMAIN);policy_hash(id->policy_sha256);
    struct cp_sha h;begin(&h,"Blackwire native derived prepared namespace v1");cp_sha_update(&h,id->policy_sha256,32);
    text(&h,s->binding.binding.source);text(&h,s->binding.binding.game);text(&h,s->binding.binding.title);
    cp_sha_update(&h,s->claim.binding_sha256,32);cp_sha_update(&h,s->claim.snapshot_sha256,32);cp_sha_update(&h,s->claim.base_map_sha256,32);
    cp_sha_update(&h,s->claim.base_digests_sha256,32);cp_sha_update(&h,s->claim.recipe_sha256,32);cp_sha_update(&h,s->grant.mapping_rule_sha256,32);text(&h,s->grant.destination);
    cp_sha_final(&h,id->namespace_sha256);memcpy(id->component,s->binding.binding.title,9);id->component[9]='_';
    static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++){id->component[10+2*i]=hex[id->namespace_sha256[i]>>4];id->component[11+2*i]=hex[id->namespace_sha256[i]&15];}id->component[74]=0;
}
struct composed {struct bw_source_map map;uint8_t *digests;struct bw_derived_origin *origins;struct bw_derived_plan_view view;};
static void release(struct composed *c){bw_source_map_destroy(&c->map);free(c->digests);free(c->origins);memset(c,0,sizeof *c);}
static int compose(struct bw_derived_plan *p,struct composed *c){
    struct dp_state *s=p->state;struct bw_manifest old,new;struct bw_source_plan plan={0};int bad=1;
    memset(c,0,sizeof *c);identity(s,&c->view.identity);
    if(bw_manifest_open(&old,s->base.bytes,s->base.size)||bw_source_plan_init(&plan,c->view.identity.component))goto done;
    uint32_t archive=UINT32_MAX;
    for(uint32_t i=0;i<old.count;i++){struct bw_manifest_record r;if(guard(p)||bw_manifest_get(&old,i,&r)||bw_source_plan_add(&plan,r.path,r.size,r.mtime_sec))goto done;
        if(!strcmp(r.path,s->recipe.archive)){if(r.flags!=BW_MANIFEST_STREAM||r.size!=s->recipe.archive_info.size)goto done;archive=i;}}
    if(archive==UINT32_MAX||bw_source_plan_add(&plan,s->grant.destination,s->recipe.spans[BW_PP_BODY].size,0)||bw_source_plan_finish(&plan,&c->map)||
       c->map.local_bytes>s->request.startup_budget||c->map.remote_bytes!=s->base.remote_bytes||c->map.stream_count!=s->base.stream_count||
       c->map.local_count!=s->base.local_count+1u||c->map.bootstrap_bytes!=s->base.bootstrap_bytes+s->recipe.spans[BW_PP_BODY].size||
       bw_manifest_open(&new,c->map.bytes,c->map.size)||new.count!=old.count+1u)goto done;
    c->view.count=new.count;c->view.archive_original_id=archive;c->view.derived_id=UINT32_MAX;c->view.digest_size=56u+32u*new.count;
    c->digests=calloc(c->view.digest_size,1);c->origins=calloc(new.count,sizeof *c->origins);if(!c->digests||!c->origins)goto done;
    memcpy(c->digests,"BWDIG001",8);put32(c->digests+8,1);put32(c->digests+12,new.count);put64(c->digests+16,new.checksum);
    if(hashed(p,c->map.bytes,c->map.size,c->view.map_sha256))goto done;memcpy(c->digests+24,c->view.map_sha256,32);
    uint32_t oi=0;
    for(uint32_t i=0;i<new.count;i++){
        if(guard(p))goto done;struct bw_manifest_record r;if(bw_manifest_get(&new,i,&r))goto done;
        struct bw_derived_origin *origin=c->origins+i;origin->file_id=i;origin->original_id=origin->archive_original_id=origin->recipe_index=UINT32_MAX;
        if(!strcmp(r.path,s->grant.destination)){
            if(c->view.derived_id!=UINT32_MAX||r.flags!=BW_MANIFEST_LOCAL||r.mtime_sec||r.size!=s->recipe.spans[BW_PP_BODY].size)goto done;
            c->view.derived_id=i;origin->kind=BW_DERIVED_PACKED_PROJECT;origin->archive_original_id=archive;origin->recipe_index=0;memcpy(c->digests+56u+32u*i,s->recipe.spans[BW_PP_BODY].sha256,32);
        }else{
            struct bw_manifest_record o;if(oi>=old.count||bw_manifest_get(&old,oi,&o)||strcmp(o.path,r.path)||o.flags!=r.flags||o.size!=r.size||o.mtime_sec!=r.mtime_sec)goto done;
            origin->kind=BW_DERIVED_ORIGINAL;origin->original_id=oi;memcpy(c->digests+56u+32u*i,s->base_digests+56u+32u*oi,32);oi++;
        }
    }
    if(oi!=old.count||c->view.derived_id==UINT32_MAX||hashed(p,c->digests,c->view.digest_size,c->view.digests_sha256))goto done;
    struct cp_sha h;begin(&h,"Blackwire native derived origin table v1");cp_sha_update(&h,c->view.identity.namespace_sha256,32);cp_sha_update(&h,s->claim.base_map_sha256,32);
    cp_sha_update(&h,s->claim.base_digests_sha256,32);cp_sha_update(&h,s->claim.recipe_sha256,32);h32(&h,new.count);
    for(uint32_t i=0;i<new.count;i++){if((i&31u)==0&&guard(p))goto done;const struct bw_derived_origin *o=c->origins+i;
        h32(&h,o->file_id);h32(&h,o->kind);h32(&h,o->original_id);h32(&h,o->archive_original_id);h32(&h,o->recipe_index);}
    cp_sha_final(&h,c->view.origins_sha256);begin(&h,"Blackwire native derived plan commitment v1");cp_sha_update(&h,c->view.identity.namespace_sha256,32);
    cp_sha_update(&h,c->view.map_sha256,32);cp_sha_update(&h,c->view.digests_sha256,32);cp_sha_update(&h,c->view.origins_sha256,32);cp_sha_final(&h,c->view.identity.commitment_sha256);
    c->view.map=c->map.bytes;c->view.map_size=c->map.size;c->view.digests=c->digests;c->view.origins=c->origins;
    c->view.local_bytes=c->map.local_bytes;c->view.remote_bytes=c->map.remote_bytes;c->view.bootstrap_bytes=c->map.bootstrap_bytes;c->view.local_count=c->map.local_count;c->view.stream_count=c->map.stream_count;
    memcpy(c->view.recipe_sha256,s->claim.recipe_sha256,32);memcpy(c->view.base_map_sha256,s->claim.base_map_sha256,32);
    memcpy(c->view.base_digests_sha256,s->claim.base_digests_sha256,32);memcpy(c->view.base_snapshot_sha256,s->claim.snapshot_sha256,32);bad=guard(p);
done:
    bw_source_plan_destroy(&plan);if(bad){release(c);return fail(p,"derived_composition_refused");}return 0;
}
static int live(struct bw_derived_plan *p){
    struct dp_state *s=p->state;if(base(p,0)||recipe(p,0)||grant(p))return -1;
    if(bw_source_provider_current(s->request.original)||guard(p)||base(p,0))return fail(p,"derived_provider_current_refused");
    if(bw_packed_project_revalidate(s->request.project)||guard(p)||recipe(p,0)||grant(p))return fail(p,"derived_parser_revalidation_refused");
    if(bw_source_provider_current(s->request.original)||guard(p)||base(p,0)||recipe(p,0)||grant(p))return fail(p,"derived_provider_after_parser_refused");return 0;
}
int bw_derived_plan_open(struct bw_derived_plan *p,const struct bw_derived_plan_request *r){
    if(!p)return -1;if(p->state||p->busy||p->failed)return fail(p,"derived_new_zeroed_session_required");
    if(!r||!r->original||!r->original->source||!r->project||!r->current_job||r->policy!=BW_DERIVED_PLAN_POLICY||!r->destination||
       r->destination->version!=BW_DERIVED_DESTINATION_GRANT_V1||!r->destination->owner||!r->destination->epoch||!r->destination->check||
       path(r->destination->destination)||zero(r->destination->mapping_rule_sha256,32)||!r->startup_budget||r->startup_budget>BW_PROVIDER_MAX_LOCAL_BYTES)return fail(p,"derived_live_inputs_or_grant_required");
    struct dp_state *s=calloc(1,sizeof *s);if(!s)return fail(p,"derived_allocation_refused");p->state=s;p->busy=1;s->request=*r;s->grant=*r->destination;
    s->source=r->original->source;s->connection=s->source->smb;s->provider_budget=r->original->budget;s->claim.provider=r->original;s->claim.project=r->project;
    s->claim.current_job=r->current_job;s->claim.job=r->job;memcpy(s->claim.destination,s->grant.destination,sizeof s->claim.destination);
    struct composed c={0};int bad=base(p,1)||recipe(p,1)||grant(p)||live(p)||compose(p,&c)||base(p,0)||recipe(p,0)||grant(p);
    if(!bad){s->map=c.map;s->digests=c.digests;s->origins=c.origins;s->view=c.view;
        s->digest_allocated=c.view.digest_size;s->output_count=c.view.count;memset(&c,0,sizeof c);}
    release(&c);p->busy=0;return bad?fail(p,"derived_open_refused"):0;
}
static int output(struct bw_derived_plan *p){
    struct dp_state *s=p->state;struct bw_derived_plan_view *v=&s->view;uint8_t mh[32],dh[32];
    if(!v->map||v->map!=s->map.bytes||v->digests!=s->digests||v->origins!=s->origins||!v->count||v->count>BW_DERIVED_MAX_RECORDS||
       v->count!=s->output_count||v->digest_size!=s->digest_allocated||
       v->map_size!=s->map.size||v->map_size>DP_MAP_LIMIT||v->digest_size!=56u+32u*v->count||
       hashed(p,v->map,v->map_size,mh)||hashed(p,v->digests,v->digest_size,dh)||memcmp(mh,v->map_sha256,32)||memcmp(dh,v->digests_sha256,32))return fail(p,"derived_owned_output_changed");
    /* Reconstruct also checks origin rows/namespace/all facts, not only bytes. */
    struct composed c={0};int bad=compose(p,&c);
    if(!bad){struct bw_derived_plan_view expected=c.view;expected.map=v->map;expected.digests=v->digests;expected.origins=v->origins;
        bad=memcmp(&expected,v,sizeof expected)||memcmp(c.origins,s->origins,(size_t)v->count*sizeof *s->origins);}
    release(&c);return bad?fail(p,"derived_owned_commitment_changed"):guard(p);
}
const struct bw_derived_plan_view *bw_derived_plan_view(struct bw_derived_plan *p){
    if(!p||!p->state)return NULL;if(p->busy||p->failed){fail(p,"derived_view_state_refused");return NULL;}
    p->busy=1;int bad=base(p,0)||recipe(p,0)||grant(p)||output(p)||base(p,0)||recipe(p,0)||grant(p);p->busy=0;
    return bad?NULL:&((struct dp_state *)p->state)->view;
}
int bw_derived_plan_revalidate(struct bw_derived_plan *p){
    if(!p||!p->state)return -1;if(p->busy||p->failed)return fail(p,"derived_revalidate_state_refused");
    p->busy=1;int bad=live(p)||output(p)||base(p,0)||recipe(p,0)||grant(p);p->busy=0;return bad?fail(p,"derived_revalidation_refused"):0;
}
int bw_derived_plan_destroy(struct bw_derived_plan *p){
    if(!p)return -1;if(p->busy)return fail(p,"derived_destroy_busy_refused");if(p->state){struct dp_state *s=p->state;
        bw_source_map_destroy(&s->map);free(s->digests);free(s->origins);free(s);}memset(p,0,sizeof *p);return 0;
}
