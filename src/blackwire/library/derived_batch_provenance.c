#include "derived_batch_provenance.h"
#include "derived_batch_internal.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>

/* All integers are unsigned little endian. No native padding/pointers enter
 * BWDPR002. The trailing hash is SHA256(LP32(domain),LE64(prefix size),prefix),
 * where domain is the exact ASCII string below, without a terminator. */
#define DOCUMENT_DOMAIN "Blackwire packed startup historical document v2"
struct cursor {uint8_t *bytes;size_t size,at;int failed;};
struct decoded {struct bw_batch_facts facts;uint8_t *bytes;size_t size;struct bw_pbb_archive *archives;struct bw_pbb_entry *entries;struct bw_batch_origin *origins;};
static uint32_t r32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t r64(const uint8_t *p){return r32(p)|(uint64_t)r32(p+4)<<32;}
static void w32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void w64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(8*i));}
static void put(struct cursor *c,const void *p,size_t n){if(c->failed||n>BW_BATCH_PROVENANCE_LIMIT-c->at){c->failed=1;return;}if(c->bytes){if(n>c->size-c->at){c->failed=1;return;}memcpy(c->bytes+c->at,p,n);}c->at+=n;}
static void u32(struct cursor *c,uint32_t x){uint8_t p[4];w32(p,x);put(c,p,4);}
static void u64(struct cursor *c,uint64_t x){uint8_t p[8];w64(p,x);put(c,p,8);}
static void txt(struct cursor *c,const char *s){size_t n=strlen(s);if(n>UINT32_MAX){c->failed=1;return;}u32(c,(uint32_t)n);put(c,s,n);}
static const uint8_t *take(struct cursor *c,size_t n){if(c->failed||n>c->size-c->at){c->failed=1;return NULL;}const uint8_t *p=c->bytes+c->at;c->at+=n;return p;}
static uint32_t get32(struct cursor *c){const uint8_t *p=take(c,4);return p?r32(p):0;}
static uint64_t get64(struct cursor *c){const uint8_t *p=take(c,8);return p?r64(p):0;}
static void raw(struct cursor *c,void *out,size_t n){const uint8_t *p=take(c,n);if(p)memcpy(out,p,n);}
static void gettxt(struct cursor *c,char *out,size_t cap){uint32_t n=get32(c);if(c->failed||n>=cap){c->failed=1;return;}const uint8_t *p=take(c,n);if(!p||memchr(p,0,n)){c->failed=1;return;}memcpy(out,p,n);out[n]=0;}
static void source_info(struct cursor *c,const struct bw_source_info *p){u64(c,p->size);u64(c,p->inode);u64(c,p->mtime_ticks);u64(c,p->ctime_ticks);u32(c,p->attributes);}
static void getinfo(struct cursor *c,struct bw_source_info *p){p->size=get64(c);p->inode=get64(c);p->mtime_ticks=get64(c);p->ctime_ticks=get64(c);p->attributes=get32(c);}
static void span(struct cursor *c,const struct bw_pbb_span *s){u64(c,s->offset);u64(c,s->size);put(c,s->sha256,32);put(c,s->sha1,20);}
static void getspan(struct cursor *c,struct bw_pbb_span *s){s->offset=get64(c);s->size=get64(c);raw(c,s->sha256,32);raw(c,s->sha1,20);}
static void pspan(struct cursor *c,const struct bw_pp_span *s){u64(c,s->offset);u64(c,s->size);put(c,s->sha256,32);put(c,s->sha1,20);}
static void getpspan(struct cursor *c,struct bw_pp_span *s){s->offset=get64(c);s->size=get64(c);raw(c,s->sha256,32);raw(c,s->sha1,20);}
static void blob(struct cursor *c,const uint8_t *p,size_t n){u64(c,n);put(c,p,n);}
static const uint8_t *getblob(struct cursor *c,size_t *n,size_t limit){uint64_t z=get64(c);if(c->failed||z>limit||z>SIZE_MAX){c->failed=1;return NULL;}*n=(size_t)z;return take(c,*n);}
static void document_hash(const uint8_t *p,size_t n,uint8_t out[32]){struct cp_sha h;uint8_t a[8];cp_sha_init(&h);w32(a,sizeof DOCUMENT_DOMAIN-1u);cp_sha_update(&h,a,4);cp_sha_update(&h,DOCUMENT_DOMAIN,sizeof DOCUMENT_DOMAIN-1u);w64(a,n);cp_sha_update(&h,a,8);cp_sha_update(&h,p,n);cp_sha_final(&h,out);}
static void encode(struct cursor *c,const struct bw_batch_facts *f,size_t total){
    put(c,"BWDPR002",8);u32(c,2);u32(c,0);u64(c,total);
    u32(c,(uint32_t)f->original.kind);u32(c,f->original.policy);txt(c,f->original.binding.source);txt(c,f->original.binding.game);txt(c,f->original.binding.title);put(c,f->original.binding.sha256,32);
    u32(c,f->prepared.policy);txt(c,f->prepared.domain);txt(c,f->prepared.component);put(c,f->prepared.policy_sha256,32);put(c,f->prepared.namespace_sha256,32);put(c,f->prepared.commitment_sha256,32);
    u64(c,f->base_bootstrap_bytes);u32(c,f->bootstrap.version);txt(c,f->bootstrap.prefix);put(c,f->bootstrap.native_snapshot_sha256,32);u32(c,f->bootstrap.archive_count);u32(c,f->bootstrap.entry_count);u64(c,f->bootstrap.bootstrap_bytes);u64(c,f->bootstrap.base_local_bytes);
    for(uint32_t i=0;i<f->bootstrap.archive_count;i++){const struct bw_pbb_archive *a=&f->bootstrap.archives[i];txt(c,a->path);source_info(c,&a->info);for(unsigned k=0;k<BW_PBB_SPANS;k++)span(c,&a->spans[k]);u32(c,a->filename_count);u32(c,a->directory_count);u64(c,a->hash_seed);}
    for(uint32_t i=0;i<f->bootstrap.entry_count;i++){const struct bw_pbb_entry *e=&f->bootstrap.entries[i];txt(c,e->destination);u32(c,e->archive_index);u32(c,e->entry_location);u32(c,e->flags);u64(c,e->header_offset);u64(c,e->body_offset);u64(c,e->size);put(c,e->header_sha256,32);put(c,e->body_sha1,20);put(c,e->body_sha256,32);}
    u32(c,f->has_project);if(f->has_project){const struct bw_pp_plan *p=&f->project;uint32_t ai=0;while(ai<f->bootstrap.archive_count&&strcmp(p->archive,f->bootstrap.archives[ai].path))ai++;u32(c,ai);txt(c,p->requested);txt(c,p->listed);txt(c,p->proposed_destination);u32(c,p->entry_location);pspan(c,&p->spans[BW_PP_HEADER]);pspan(c,&p->spans[BW_PP_BODY]);u32(c,p->module_count);for(uint32_t i=0;i<p->module_count;i++)txt(c,p->modules[i]);}
    blob(c,f->base_map,f->base_map_size);blob(c,f->base_digests,f->base_digest_size);blob(c,f->map,f->map_size);blob(c,f->digests,f->digest_size);u32(c,f->count);
    for(uint32_t i=0;i<f->count;i++){const struct bw_batch_origin *o=&f->origins[i];u32(c,o->file_id);u32(c,o->kind);u32(c,o->original_id);u32(c,o->archive_index);u32(c,o->entry_index);}
    u32(c,f->local_count);u32(c,f->stream_count);u64(c,f->local_bytes);u64(c,f->remote_bytes);u64(c,f->bootstrap_bytes);
    put(c,f->base_map_sha256,32);put(c,f->base_digests_sha256,32);put(c,f->facts_sha256,32);put(c,f->mapping_rule_sha256,32);put(c,f->material_policy_sha256,32);put(c,f->map_sha256,32);put(c,f->digests_sha256,32);put(c,f->origins_sha256,32);put(c,f->material_sha256,32);
}
int bw_batch_provenance_encode(const struct bw_batch_facts *f,uint8_t **out,size_t *size,const char **error){
    if(out)*out=NULL;if(size)*size=0;if(error)*error=NULL;if(!out||!size||db_check(f,error))return -1;
    struct cursor count={0};encode(&count,f,0);if(count.failed||count.at>BW_BATCH_PROVENANCE_LIMIT-32u){if(error)*error="batch_document_size";return -1;}size_t n=count.at+32;uint8_t *p=calloc(n,1);if(!p){if(error)*error="batch_document_allocation";return -1;}
    struct cursor c={.bytes=p,.size=n};encode(&c,f,n);if(c.failed||c.at!=n-32u){free(p);if(error)*error="batch_document_encode";return -1;}document_hash(p,c.at,p+c.at);*out=p;*size=n;return 0;
}
static void release(struct decoded *d){if(!d)return;free(d->archives);free(d->entries);free(d->origins);free(d->bytes);free(d);}
int bw_batch_provenance_decode(struct bw_batch_provenance *owner,const void *bytes,size_t size){
    if(!owner)return -1;if(owner->state||owner->error){owner->error="batch_document_new_zeroed_owner";return -1;}
    if(!bytes||size<64||size>BW_BATCH_PROVENANCE_LIMIT){owner->error="batch_document_bounds";return -1;}
    const uint8_t *input=bytes;uint8_t hash[32];document_hash(input,size-32,hash);if(memcmp(input,"BWDPR002",8)||r32(input+8)!=2||r32(input+12)||r64(input+16)!=size||memcmp(hash,input+size-32,32)){owner->error="batch_document_header_or_hash";return -1;}
    struct decoded *d=calloc(1,sizeof *d);if(!d){owner->error="batch_document_allocation";return -1;}d->bytes=malloc(size);if(!d->bytes){release(d);owner->error="batch_document_allocation";return -1;}memcpy(d->bytes,bytes,size);d->size=size;
    struct cursor c={.bytes=d->bytes,.size=size-32,.at=24};struct bw_batch_facts *f=&d->facts;f->original.kind=(enum bw_provider_kind)get32(&c);f->original.policy=get32(&c);gettxt(&c,f->original.binding.source,sizeof f->original.binding.source);gettxt(&c,f->original.binding.game,sizeof f->original.binding.game);gettxt(&c,f->original.binding.title,sizeof f->original.binding.title);raw(&c,f->original.binding.sha256,32);
    f->prepared.policy=get32(&c);gettxt(&c,f->prepared.domain,sizeof f->prepared.domain);gettxt(&c,f->prepared.component,sizeof f->prepared.component);raw(&c,f->prepared.policy_sha256,32);raw(&c,f->prepared.namespace_sha256,32);raw(&c,f->prepared.commitment_sha256,32);
    f->base_bootstrap_bytes=get64(&c);f->bootstrap.version=get32(&c);gettxt(&c,f->bootstrap.prefix,sizeof f->bootstrap.prefix);raw(&c,f->bootstrap.native_snapshot_sha256,32);f->bootstrap.archive_count=get32(&c);f->bootstrap.entry_count=get32(&c);f->bootstrap.bootstrap_bytes=get64(&c);f->bootstrap.base_local_bytes=get64(&c);
    if(c.failed||!f->bootstrap.archive_count||f->bootstrap.archive_count>BW_PBB_MAX_ARCHIVES||!f->bootstrap.entry_count||f->bootstrap.entry_count>BW_PBB_MAX_FILES||
       (uint64_t)f->bootstrap.archive_count*329u+(uint64_t)f->bootstrap.entry_count*125u+4u>c.size-c.at)goto malformed;
    d->archives=calloc(f->bootstrap.archive_count,sizeof *d->archives);d->entries=calloc(f->bootstrap.entry_count,sizeof *d->entries);if(!d->archives||!d->entries)goto allocation;f->bootstrap.archives=d->archives;f->bootstrap.entries=d->entries;
    memcpy(f->bootstrap.source,f->original.binding.source,sizeof f->bootstrap.source);memcpy(f->bootstrap.game,f->original.binding.game,sizeof f->original.binding.game);
    for(uint32_t i=0;i<f->bootstrap.archive_count&&!c.failed;i++){struct bw_pbb_archive *a=&d->archives[i];gettxt(&c,a->path,sizeof a->path);getinfo(&c,&a->info);for(unsigned k=0;k<BW_PBB_SPANS;k++)getspan(&c,&a->spans[k]);a->filename_count=get32(&c);a->directory_count=get32(&c);a->hash_seed=get64(&c);}
    for(uint32_t i=0;i<f->bootstrap.entry_count&&!c.failed;i++){struct bw_pbb_entry *e=&d->entries[i];gettxt(&c,e->destination,sizeof e->destination);e->archive_index=get32(&c);e->entry_location=get32(&c);e->flags=get32(&c);e->header_offset=get64(&c);e->body_offset=get64(&c);e->size=get64(&c);raw(&c,e->header_sha256,32);raw(&c,e->body_sha1,20);raw(&c,e->body_sha256,32);}
    f->has_project=get32(&c);if(f->has_project>1)goto malformed;if(f->has_project){uint32_t ai=get32(&c);if(ai>=f->bootstrap.archive_count)goto malformed;const struct bw_pbb_archive *a=&d->archives[ai];struct bw_pp_plan *p=&f->project;memcpy(p->archive,a->path,sizeof p->archive);p->archive_info=a->info;memcpy(p->source,f->bootstrap.source,sizeof p->source);memcpy(p->game,f->bootstrap.game,sizeof p->game);memcpy(p->native_snapshot_sha256,f->bootstrap.native_snapshot_sha256,32);static const unsigned shared[]={BW_PBB_FOOTER,BW_PBB_PRIMARY,BW_PBB_FILENAMES};for(unsigned k=0;k<3;k++){p->spans[k].offset=a->spans[shared[k]].offset;p->spans[k].size=a->spans[shared[k]].size;memcpy(p->spans[k].sha256,a->spans[shared[k]].sha256,32);if(k)memcpy(p->spans[k].sha1,a->spans[shared[k]].sha1,20);}gettxt(&c,p->requested,sizeof p->requested);gettxt(&c,p->listed,sizeof p->listed);gettxt(&c,p->proposed_destination,sizeof p->proposed_destination);p->entry_location=get32(&c);getpspan(&c,&p->spans[BW_PP_HEADER]);getpspan(&c,&p->spans[BW_PP_BODY]);p->module_count=get32(&c);if(p->module_count>BW_PP_MAX_MODULES)goto malformed;for(uint32_t i=0;i<p->module_count&&!c.failed;i++)gettxt(&c,p->modules[i],sizeof p->modules[i]);}
    f->base_map=getblob(&c,&f->base_map_size,40u+BW_BATCH_MAX_BASE*(32u+241u));f->base_digests=getblob(&c,&f->base_digest_size,56u+32u*BW_BATCH_MAX_BASE);f->map=getblob(&c,&f->map_size,DB_MAP_LIMIT);f->digests=getblob(&c,&f->digest_size,DB_DIG_LIMIT);f->count=get32(&c);if(c.failed||!f->count||f->count>BW_BATCH_MAX_RECORDS||(uint64_t)f->count*20u+320u>c.size-c.at)goto malformed;
    d->origins=calloc(f->count,sizeof *d->origins);if(!d->origins)goto allocation;f->origins=d->origins;for(uint32_t i=0;i<f->count&&!c.failed;i++){struct bw_batch_origin *o=&d->origins[i];o->file_id=get32(&c);o->kind=get32(&c);o->original_id=get32(&c);o->archive_index=get32(&c);o->entry_index=get32(&c);}
    f->local_count=get32(&c);f->stream_count=get32(&c);f->local_bytes=get64(&c);f->remote_bytes=get64(&c);f->bootstrap_bytes=get64(&c);raw(&c,f->base_map_sha256,32);raw(&c,f->base_digests_sha256,32);raw(&c,f->facts_sha256,32);raw(&c,f->mapping_rule_sha256,32);raw(&c,f->material_policy_sha256,32);raw(&c,f->map_sha256,32);raw(&c,f->digests_sha256,32);raw(&c,f->origins_sha256,32);raw(&c,f->material_sha256,32);
    if(c.failed||c.at!=c.size||db_check(f,&owner->error))goto malformed;
    /* Semantic validation plus byte-identical canonical reencoding rejects
     * alternate ordering, hidden suffixes, skipped/reserved bytes and padding. */
    uint8_t *canonical=NULL;size_t canonical_size=0;if(bw_batch_provenance_encode(f,&canonical,&canonical_size,&owner->error))goto malformed;int different=canonical_size!=size||memcmp(canonical,d->bytes,size);free(canonical);if(different)goto malformed;owner->state=d;return 0;
allocation:owner->error="batch_document_allocation";release(d);return -1;
malformed:if(!owner->error)owner->error="batch_document_canonical_or_semantic_refusal";release(d);return -1;
}
const struct bw_batch_facts *bw_batch_provenance_view(const struct bw_batch_provenance *p){return p&&p->state&&!p->error?&((const struct decoded *)p->state)->facts:NULL;}
void bw_batch_provenance_destroy(struct bw_batch_provenance *p){if(p){release(p->state);memset(p,0,sizeof *p);}}
