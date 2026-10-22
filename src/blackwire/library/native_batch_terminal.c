#include "native_batch_terminal.h"
#include "source_profile.h"
#include "../streamreader/preparation_receipt.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if !defined(BW_GTA_AUDIO_BOOTSTRAP) || BW_GTA_AUDIO_BOOTSTRAP != 1
#error "Terminal v2 requires the frozen material profile registry configuration"
#endif
#define DT_MAX_LOGICAL UINT64_C(9007199254740991)
#define DT_MAP_LIMIT (40u+BW_BATCH_MAX_RECORDS*(32u+241u))
struct dt_cursor {uint8_t *out;const uint8_t *in;size_t size,at;int bad;};
_Static_assert(sizeof(struct bw_batch_terminal_record)<=4096,"terminal metadata bound");
static uint32_t dt_r32(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint64_t dt_r64(const uint8_t *b){return dt_r32(b)|(uint64_t)dt_r32(b+4)<<32;}
static void dt_p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void dt_p64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static int dt_error(const char **error,const char *why){if(error)*error=why;return -1;}
static int dt_string(const char *p,size_t cap,int ascii){const char *end=memchr(p,0,cap);if(!end||end==p)return -1;if(ascii)for(const char *a=p;a<end;a++)if((unsigned char)*a<32||(unsigned char)*a>=127)return -1;return 0;}
static int dt_zero(const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static void dt_raw(struct dt_cursor *c,void *p,size_t n){if(c->bad||c->at>c->size||n>c->size-c->at){c->bad=1;return;}if(c->in)memcpy(p,c->in+c->at,n);else if(c->out)memcpy(c->out+c->at,p,n);c->at+=n;}
static void dt_u32(struct dt_cursor *c,uint32_t *n){uint8_t b[4]={0};if(!c->in)dt_p32(b,*n);dt_raw(c,b,4);if(c->in)*n=dt_r32(b);}
static void dt_u64(struct dt_cursor *c,uint64_t *n){uint8_t b[8]={0};if(!c->in)dt_p64(b,*n);dt_raw(c,b,8);if(c->in)*n=dt_r64(b);}
static void dt_text(struct dt_cursor *c,char *s,size_t cap){uint32_t n=0;if(!c->in){if(dt_string(s,cap,0)){c->bad=1;return;}n=(uint32_t)strlen(s);}dt_u32(c,&n);if(c->bad||!n||n>=cap){c->bad=1;return;}dt_raw(c,s,n);if(c->in&&!c->bad){if(memchr(s,0,n)){c->bad=1;return;}s[n]=0;}}
static void dt_h32(struct cp_sha *h,uint32_t n){uint8_t b[4];dt_p32(b,n);cp_sha_update(h,b,4);}
static void dt_h64(struct cp_sha *h,uint64_t n){uint8_t b[8];dt_p64(b,n);cp_sha_update(h,b,8);}
static void dt_lp(struct cp_sha *h,const char *s){size_t n=strlen(s);dt_h32(h,(uint32_t)n);cp_sha_update(h,s,n);}
static void dt_begin(struct cp_sha *h,const char *s){cp_sha_init(h);dt_lp(h,s);}
static int dt_component(const char *p,const char *title,const uint8_t h[32]){static const char hex[]="0123456789abcdef";if(dt_string(p,75,1)||strlen(p)!=74||memcmp(p,title,9)||p[9]!='_')return -1;for(unsigned i=0;i<32;i++)if(p[10+2*i]!=hex[h[i]>>4]||p[11+2*i]!=hex[h[i]&15])return -1;return 0;}
static int dt_ref(const struct bw_derived_artifact_ref *a,uint64_t min,uint64_t max){return a->size<min||a->size>max||dt_zero(a->sha256,32)?-1:0;}
static int dt_check(const struct bw_batch_terminal_record *r,const char **error){
    if(!r)return dt_error(error,"terminal_arguments");const struct bw_source_binding *b=&r->original.binding;
    if(r->original.kind!=BW_PROVIDER_FOLDER||r->original.policy!=1||r->material_policy!=BW_BATCH_POLICY||r->prepared.policy!=BW_BATCH_POLICY||dt_string(b->source,sizeof b->source,0)||bw_manifest_validate_path(b->source,strlen(b->source))||dt_string(b->title,sizeof b->title,1)||strlen(b->title)!=9||memcmp(b->title,"PPSA",4)||dt_string(r->title,sizeof r->title,1)||strcmp(r->title,b->title))return dt_error(error,"terminal_source_policy");
    for(unsigned i=4;i<9;i++)if(b->title[i]<'0'||b->title[i]>'9')return dt_error(error,"terminal_title");
    if(bw_source_profile_name(b->title)||dt_zero(b->sha256,32)||dt_component(b->game,b->title,b->sha256)||dt_string(r->prepared.domain,sizeof r->prepared.domain,1)||strcmp(r->prepared.domain,BW_BATCH_PLAN_DOMAIN)||dt_component(r->prepared.component,b->title,r->prepared.namespace_sha256))return dt_error(error,"terminal_identity");
    if(dt_string(r->run,sizeof r->run,1)||strlen(r->run)!=32)return dt_error(error,"terminal_run");unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",r->run[i]))return dt_error(error,"terminal_run");nonzero|=r->run[i]!='0';}if(!nonzero)return dt_error(error,"terminal_run");
    if(dt_zero(r->prepared.policy_sha256,32)||dt_zero(r->material_policy_sha256,32))return dt_error(error,"terminal_policy_commitment"); /* exact policy reconstruction is independently required by BWDPR002 validation */
    const uint8_t *hashes[]={r->prepared.namespace_sha256,r->prepared.commitment_sha256,r->mapping_rule_sha256,r->material_sha256,r->native_snapshot_sha256,r->base_map_sha256,r->base_digests_sha256,r->facts_sha256,r->origins_sha256};for(unsigned i=0;i<sizeof hashes/sizeof hashes[0];i++)if(dt_zero(hashes[i],32))return dt_error(error,"terminal_missing_commitment");
    if(r->count<2||r->count>BW_BATCH_MAX_RECORDS||!r->local_count||r->local_count>r->count||r->stream_count!=r->count-r->local_count||r->entries<r->count+1||r->entries>262144||!r->local_bytes||r->local_bytes>UINT64_C(5368709120)||r->remote_bytes>DT_MAX_LOGICAL-r->local_bytes||r->bootstrap_bytes>UINT64_C(268435456)||r->bootstrap_bytes>r->local_bytes||r->allocated_bytes>DT_MAX_LOGICAL)return dt_error(error,"terminal_accounting");
    if(dt_ref(&r->provenance,1,BW_BATCH_PROVENANCE_LIMIT)||dt_ref(&r->map,40,DT_MAP_LIMIT)||dt_ref(&r->digests,56u+32u*r->count,56u+32u*r->count)||dt_ref(&r->index,48,BW_BATCH_TERMINAL_INDEX_LIMIT)||dt_ref(&r->marker,BW_GENERATION_SIZE,BW_GENERATION_SIZE)||dt_ref(&r->identity,1,BW_BATCH_TERMINAL_IDENTITY_LIMIT))return dt_error(error,"terminal_artifact_bounds");return 0;
}
static void dt_fields(struct dt_cursor *c,struct bw_batch_terminal_record *r){uint32_t kind=(uint32_t)r->original.kind;dt_u32(c,&kind);if(c->in)r->original.kind=(enum bw_provider_kind)kind;dt_u32(c,&r->original.policy);dt_u32(c,&r->material_policy);dt_u32(c,&r->prepared.policy);dt_text(c,r->original.binding.source,sizeof r->original.binding.source);dt_text(c,r->original.binding.game,sizeof r->original.binding.game);dt_text(c,r->original.binding.title,sizeof r->original.binding.title);dt_raw(c,r->original.binding.sha256,32);dt_text(c,r->prepared.domain,sizeof r->prepared.domain);dt_text(c,r->prepared.component,sizeof r->prepared.component);dt_raw(c,r->prepared.policy_sha256,32);dt_raw(c,r->prepared.namespace_sha256,32);dt_raw(c,r->prepared.commitment_sha256,32);dt_raw(c,r->material_policy_sha256,32);dt_raw(c,r->mapping_rule_sha256,32);dt_raw(c,r->material_sha256,32);dt_raw(c,r->native_snapshot_sha256,32);dt_raw(c,r->base_map_sha256,32);dt_raw(c,r->base_digests_sha256,32);dt_raw(c,r->facts_sha256,32);dt_raw(c,r->origins_sha256,32);dt_raw(c,r->run,32);dt_raw(c,r->title,9);if(c->in){r->run[32]=0;r->title[9]=0;}
    struct bw_derived_artifact_ref *refs[]={&r->provenance,&r->map,&r->digests,&r->index,&r->marker,&r->identity};for(unsigned i=0;i<6;i++){dt_u64(c,&refs[i]->size);dt_raw(c,refs[i]->sha256,32);}dt_u32(c,&r->count);dt_u32(c,&r->local_count);dt_u32(c,&r->stream_count);dt_u32(c,&r->entries);dt_u64(c,&r->local_bytes);dt_u64(c,&r->remote_bytes);dt_u64(c,&r->bootstrap_bytes);dt_u64(c,&r->allocated_bytes);struct bw_derived_object_identity *objects[]={&r->game_root,&r->state_root,&r->provenance_object};for(unsigned i=0;i<3;i++){dt_u64(c,&objects[i]->device);dt_u64(c,&objects[i]->inode);}
}
static void dt_document_hash(const void *p,size_t n,uint8_t out[32]){struct cp_sha h;dt_begin(&h,"Blackwire native packed startup terminal record v2");cp_sha_update(&h,p,n);cp_sha_final(&h,out);}
int bw_batch_terminal_encode(const struct bw_batch_terminal_record *r,void *output,size_t cap,size_t *size,const char **error){
    if(size)*size=0;if(error)*error=NULL;if(!r||!output||!size)return dt_error(error,"terminal_arguments");if(dt_check(r,error))return -1;
    struct bw_batch_terminal_record *copy=malloc(sizeof *copy);if(!copy)return dt_error(error,"terminal_allocation");*copy=*r;struct dt_cursor c={.size=BW_BATCH_TERMINAL_LIMIT-32,.at=32};dt_fields(&c,copy);size_t total=c.at+32;
    if(c.bad||total>BW_BATCH_TERMINAL_LIMIT||total>cap){free(copy);return dt_error(error,"terminal_output_limit");}
    uint8_t *out=output;memset(out,0,total);memcpy(out,"BWNDR002",8);dt_p32(out+8,2);dt_p32(out+12,32);dt_p64(out+16,total);c=(struct dt_cursor){.out=out,.size=total-32,.at=32};dt_fields(&c,copy);free(copy);if(c.bad||c.at!=total-32)return dt_error(error,"terminal_encode");dt_document_hash(out,total-32,out+total-32);*size=total;return 0;
}
int bw_batch_terminal_decode(struct bw_batch_terminal_record *out,const void *input,size_t n,const char **error){
    if(error)*error=NULL;if(!out)return dt_error(error,"terminal_arguments");memset(out,0,sizeof *out);if(!input||n<64||n>BW_BATCH_TERMINAL_LIMIT)return dt_error(error,"terminal_record_bounds");const uint8_t *b=input;
    if(memcmp(b,"BWNDR002",8)||dt_r32(b+8)!=2||dt_r32(b+12)!=32||dt_r64(b+16)!=n||dt_r32(b+24)||dt_r32(b+28))return dt_error(error,"terminal_record_header");uint8_t h[32];dt_document_hash(b,n-32,h);if(memcmp(h,b+n-32,32))return dt_error(error,"terminal_record_hash");
    struct bw_batch_terminal_record *next=calloc(1,sizeof *next);if(!next)return dt_error(error,"terminal_allocation");struct dt_cursor c={.in=b,.size=n-32,.at=32};dt_fields(&c,next);int bad=c.bad||c.at!=n-32;if(bad){free(next);return dt_error(error,"terminal_record_fields");}bad=dt_check(next,error);if(!bad)*out=*next;free(next);return bad;
}
static int dt_same_original(const struct bw_provider_binding *a,const struct bw_provider_binding *b){return a->kind==b->kind&&a->policy==b->policy&&!strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.title,b->binding.title)&&!memcmp(a->binding.sha256,b->binding.sha256,32);}
static int dt_same_prepared(const struct bw_derived_prepared_identity *a,const struct bw_derived_prepared_identity *b){return a->policy==b->policy&&!strcmp(a->domain,b->domain)&&!strcmp(a->component,b->component)&&!memcmp(a->policy_sha256,b->policy_sha256,32)&&!memcmp(a->namespace_sha256,b->namespace_sha256,32)&&!memcmp(a->commitment_sha256,b->commitment_sha256,32);}
static int dt_artifact(const struct bw_derived_artifact_ref *ref,const void *p,size_t n){if(!p||ref->size!=n)return -1;uint8_t h[32];cp_sha_bytes(p,n,h);return memcmp(h,ref->sha256,32)?-1:0;}
/* Reconstruct exact ordinary DATA index bytes with supplied recorded mtimes.
 * No stat/permission/body meaning is inferred from those recorded integers. */
static int dt_index(const struct bw_manifest *m,const void *input,size_t n){
    if(!input||n<48||n>BW_BATCH_TERMINAL_INDEX_LIMIT)return -1;const uint8_t *b=input;uint64_t pool=0;uint32_t slots=2;
    for(uint32_t i=0;i<m->count;i++){struct bw_manifest_record r;if(bw_manifest_get(m,i,&r))return -1;pool+=7u+r.path_length;}while(slots<2*m->count)slots*=2;
    uint64_t paths_at=48u+24u*m->count,slots_at=(paths_at+pool+15)&~UINT64_C(15),size=slots_at+16u*slots;
    if(size!=n||memcmp(b,"AMPRIDX3",8)||dt_r32(b+8)!=3||dt_r32(b+12)!=24||dt_r64(b+16)!=m->count||dt_r64(b+24)!=pool||dt_r64(b+32)!=slots_at||dt_r32(b+40)!=16||dt_r32(b+44)!=slots)return -1;
    uint8_t *expected=calloc(1,n);if(!expected)return -1;memcpy(expected,b,48);size_t at=0;int bad=0;
    for(uint32_t i=0;i<m->count;i++){struct bw_manifest_record r;if(bw_manifest_get(m,i,&r)){bad=1;break;}uint8_t *row=expected+48u+24u*i,*path=expected+(size_t)paths_at+at;uint32_t length=6u+r.path_length;uint64_t mtime=dt_r64(b+48u+24u*i+16);if(mtime>INT64_MAX){bad=1;break;}dt_p32(row,(uint32_t)at);dt_p32(row+4,length);dt_p64(row+8,r.size);dt_p64(row+16,mtime);memcpy(path,"/app0/",6);memcpy(path+6,r.path,r.path_length);at+=(size_t)length+1;
        uint64_t hash=UINT64_C(1469598103934665603);for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c=(uint8_t)(c+32);hash=(hash^c)*UINT64_C(1099511628211);}if(!hash)hash=1;uint32_t slot=(uint32_t)hash&(slots-1),duplicate=0;
        while(dt_r32(expected+(size_t)slots_at+16u*slot+8)){uint8_t *prior=expected+(size_t)slots_at+16u*slot;if(dt_r64(prior)==hash){dt_p32(prior+12,dt_r32(prior+12)|1u);duplicate=1;}slot=(slot+1)&(slots-1);}uint8_t *entry=expected+(size_t)slots_at+16u*slot;dt_p64(entry,hash);dt_p32(entry+8,i+1);dt_p32(entry+12,duplicate);
    }if(!bad)bad=memcmp(expected,b,n)!=0;free(expected);return bad?-1:0;
}
int bw_batch_terminal_validate(const struct bw_batch_terminal_record *r,const struct bw_batch_terminal_artifacts *a,const char **error){
    if(error)*error=NULL;if(dt_check(r,error))return -1;if(!a)return dt_error(error,"terminal_artifacts_arguments");if(dt_artifact(&r->provenance,a->provenance,a->provenance_size)||dt_artifact(&r->index,a->index,a->index_size)||dt_artifact(&r->marker,a->marker,a->marker_size)||dt_artifact(&r->identity,a->identity,a->identity_size))return dt_error(error,"terminal_artifact_hash");
    struct bw_batch_provenance p={0};if(bw_batch_provenance_decode(&p,a->provenance,a->provenance_size))return dt_error(error,"terminal_provenance_refused");const struct bw_batch_facts *f=bw_batch_provenance_view(&p);int bad=1;
    if(!f||!dt_same_original(&r->original,&f->original)||!dt_same_prepared(&r->prepared,&f->prepared)||r->material_policy!=BW_BATCH_POLICY||memcmp(r->material_policy_sha256,f->material_policy_sha256,32)||memcmp(r->mapping_rule_sha256,f->mapping_rule_sha256,32)||memcmp(r->material_sha256,f->material_sha256,32)||memcmp(r->native_snapshot_sha256,f->bootstrap.native_snapshot_sha256,32)||memcmp(r->base_map_sha256,f->base_map_sha256,32)||memcmp(r->base_digests_sha256,f->base_digests_sha256,32)||memcmp(r->facts_sha256,f->facts_sha256,32)||memcmp(r->origins_sha256,f->origins_sha256,32)||r->map.size!=f->map_size||memcmp(r->map.sha256,f->map_sha256,32)||r->digests.size!=f->digest_size||memcmp(r->digests.sha256,f->digests_sha256,32)||r->count!=f->count||r->local_count!=f->local_count||r->stream_count!=f->stream_count||r->local_bytes!=f->local_bytes||r->remote_bytes!=f->remote_bytes||r->bootstrap_bytes!=f->bootstrap_bytes)goto done;
    struct bw_manifest m;if(bw_manifest_open(&m,f->map,f->map_size)||dt_index(&m,a->index,a->index_size)||bw_generation_matches(a->marker,a->marker_size,r->run,r->title,r->prepared.component,r->map.sha256))goto done;bad=0;
done:bw_batch_provenance_destroy(&p);return bad?dt_error(error,"terminal_cross_binding_refused"):0;
}
