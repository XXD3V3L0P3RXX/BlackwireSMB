#include "derived_batch_internal.h"
#include "source_profile.h"
#include "packed_bootstrap_rules.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t rd64(const uint8_t *p){return rd32(p)|(uint64_t)rd32(p+4)<<32;}
static void wr32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));}
static void wr64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(i*8));}
static void h32(struct cp_sha *h,uint32_t x){uint8_t b[4];wr32(b,x);cp_sha_update(h,b,4);}
static void h64(struct cp_sha *h,uint64_t x){uint8_t b[8];wr64(b,x);cp_sha_update(h,b,8);}
static void text(struct cp_sha *h,const char *s){size_t n=strlen(s);h32(h,(uint32_t)n);cp_sha_update(h,s,n);}
static void begin(struct cp_sha *h,const char *s){cp_sha_init(h);text(h,s);}
static int zero(const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static int string(const char *p,size_t cap,int ascii){const char *end=p?memchr(p,0,cap):NULL;if(!end||end==p)return -1;for(const char *a=p;a<end;a++)if((unsigned char)*a<32||(ascii&&(unsigned char)*a>=127))return -1;return 0;}
static int path(const char *p,size_t cap,int ascii){return string(p,cap,ascii)||strlen(p)>240||bw_manifest_validate_path(p,strlen(p))||bw_source_excluded(p)?-1:0;}
static int bad(const char **error,const char *s){if(error)*error=s;return -1;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int prepared_equal(const struct bw_derived_prepared_identity *a,const struct bw_derived_prepared_identity *b){return a->policy==b->policy&&!memcmp(a->domain,b->domain,sizeof a->domain)&&!memcmp(a->component,b->component,sizeof a->component)&&!memcmp(a->policy_sha256,b->policy_sha256,32)&&!memcmp(a->namespace_sha256,b->namespace_sha256,32)&&!memcmp(a->commitment_sha256,b->commitment_sha256,32);}
static int order(const char *a,const char *b){while(*a&&fold((unsigned char)*a)==fold((unsigned char)*b)){a++;b++;}unsigned x=fold((unsigned char)*a),y=fold((unsigned char)*b);return x<y?-1:x>y;}
static int same_info(const struct bw_source_info *a,const struct bw_source_info *b){return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;}
struct physical {uint32_t archive,location;uint64_t start,end;};
static int physical_order(const void *left,const void *right){const struct physical *a=left,*b=right;if(a->archive!=b->archive)return a->archive<b->archive?-1:1;return a->start<b->start?-1:a->start>b->start;}
static int location_order(const void *left,const void *right){const struct physical *a=left,*b=right;if(a->archive!=b->archive)return a->archive<b->archive?-1:1;return a->location<b->location?-1:a->location>b->location;}
static int physical_unique(const struct bw_pbb_view *v){struct physical *rows=calloc(v->entry_count,sizeof *rows);if(!rows)return -1;for(uint32_t i=0;i<v->entry_count;i++){const struct bw_pbb_entry *e=&v->entries[i];rows[i]=(struct physical){e->archive_index,e->entry_location,e->header_offset,e->body_offset+e->size};}int failed=0;qsort(rows,v->entry_count,sizeof *rows,physical_order);for(uint32_t i=1;i<v->entry_count;i++)if(rows[i].archive==rows[i-1].archive&&rows[i].start<rows[i-1].end){failed=1;break;}qsort(rows,v->entry_count,sizeof *rows,location_order);for(uint32_t i=1;i<v->entry_count;i++)if(rows[i].archive==rows[i-1].archive&&rows[i].location==rows[i-1].location){failed=1;break;}free(rows);return failed?-1:0;}
static int binding(const struct bw_provider_binding *p){const struct bw_source_binding *b=&p->binding;
    if(p->kind!=BW_PROVIDER_FOLDER||p->policy!=1||string(b->source,sizeof b->source,0)||bw_manifest_validate_path(b->source,strlen(b->source))||string(b->title,sizeof b->title,1)||strlen(b->title)!=9||memcmp(b->title,"PPSA",4)||string(b->game,sizeof b->game,1)||strlen(b->game)!=74||memcmp(b->game,b->title,9)||b->game[9]!='_'||zero(b->sha256,32)||bw_source_profile_name(b->title))return -1;
    for(unsigned i=4;i<9;i++)if(b->title[i]<'0'||b->title[i]>'9')return -1;static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(b->game[10+2*i]!=hex[b->sha256[i]>>4]||b->game[11+2*i]!=hex[b->sha256[i]&15])return -1;return 0;}
static void info_hash(struct cp_sha *h,const struct bw_source_info *i){h64(h,i->size);h64(h,i->inode);h64(h,i->mtime_ticks);h64(h,i->ctime_ticks);h32(h,i->attributes);}
static void span_hash(struct cp_sha *h,const struct bw_pbb_span *s){h64(h,s->offset);h64(h,s->size);cp_sha_update(h,s->sha256,32);cp_sha_update(h,s->sha1,20);}
static int span_valid(const struct bw_pbb_archive *a,unsigned kind){const struct bw_pbb_span *s=&a->spans[kind];
    if(kind==BW_PBB_PATH_HASH&&!s->size)return s->offset||!zero(s->sha256,32)||!zero(s->sha1,20)?-1:0;
    return !s->size||s->size>BW_PBB_MAX_INDEX||s->offset>a->info.size||s->size>a->info.size-s->offset||zero(s->sha256,32)||zero(s->sha1,20)?-1:0;}
static int pp_valid(const struct bw_pbb_view *v,const struct bw_pp_plan *p){
    if(!p)return 0;if(path(p->archive,sizeof p->archive,1)||path(p->proposed_destination,sizeof p->proposed_destination,1)||string(p->requested,sizeof p->requested,1)||string(p->listed,sizeof p->listed,1)||string(p->source,sizeof p->source,0)||string(p->game,sizeof p->game,1)||strcmp(p->requested,p->listed)||strlen(p->requested)!=strlen(p->proposed_destination)+9||memcmp(p->requested,"../../../",9)||strcmp(p->requested+9,p->proposed_destination)||strcmp(p->source,v->source)||strcmp(p->game,v->game)||memcmp(p->native_snapshot_sha256,v->native_snapshot_sha256,32)||p->module_count>BW_PP_MAX_MODULES)return -1;
    uint32_t a=0;while(a<v->archive_count&&strcmp(p->archive,v->archives[a].path))a++;if(a==v->archive_count||p->archive_info.size<4096||!same_info(&p->archive_info,&v->archives[a].info))return -1;
    static const unsigned shared[]={BW_PBB_FOOTER,BW_PBB_PRIMARY,BW_PBB_FILENAMES};
    for(unsigned k=0;k<3;k++){const struct bw_pp_span *x=&p->spans[k];const struct bw_pbb_span *y=&v->archives[a].spans[shared[k]];
        if(x->offset!=y->offset||x->size!=y->size||memcmp(x->sha256,y->sha256,32)||(k?memcmp(x->sha1,y->sha1,20):!zero(x->sha1,20)))return -1;}
    const struct bw_pp_span *header=&p->spans[BW_PP_HEADER],*body=&p->spans[BW_PP_BODY];
    if(header->size!=53||header->offset>UINT64_MAX-53||body->offset!=header->offset+53||!body->size||body->size>BW_PP_MAX_BODY||body->offset>p->spans[BW_PP_PRIMARY].offset||body->size>p->spans[BW_PP_PRIMARY].offset-body->offset||p->entry_location>=p->spans[BW_PP_PRIMARY].size||zero(header->sha256,32)||!zero(header->sha1,20)||zero(body->sha256,32)||zero(body->sha1,20))return -1;
    for(uint32_t i=0;i<p->module_count;i++){if(string(p->modules[i],sizeof p->modules[i],1))return -1;for(size_t k=0;p->modules[i][k];k++){unsigned c=(unsigned char)p->modules[i][k];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||(k&&c>='0'&&c<='9')))return -1;}}
    for(uint32_t i=0;i<v->entry_count;i++){const struct bw_pbb_entry *e=&v->entries[i];if(!order(e->destination,p->proposed_destination))return -1;
        if(e->archive_index==a&&(e->entry_location==p->entry_location||(header->offset<e->body_offset+e->size&&e->header_offset<body->offset+body->size)))return -1;}
    return 0;}
static int library_directory_unique(const struct bw_pbb_view *v){
    if(bw_pbb_prefix_mode(v->prefix)<3)return 0;
    const char *first=NULL;size_t first_size=0;
    for(uint32_t i=0;i<v->entry_count;i++){
        const char *name=v->entries[i].destination;
        if(bw_pbb_selector_library_match(name)!=1)continue;
        const char *a=strchr(name,'/'),*b=a?strchr(a+1,'/'):NULL;if(!b)return -1;
        size_t n=(size_t)(b-name)+1;
        if(first&&(n!=first_size||memcmp(first,name,n)))return -1;
        first=name;first_size=n;
    }
    return 0;
}
/* Modes4/5 Global entries retain their literal indexed project/Content spelling.
 * Bind that directory to the packed descriptor, or to exactly one original
 * LOCAL loose descriptor. Hash-only facts cannot inspect the base manifest;
 * db_build repeats this check with its validated original map. */
static int canonical_project_root(const struct bw_pbb_view *v,const struct bw_pp_plan *p,const struct bw_manifest *base){
    if(bw_pbb_prefix_mode(v->prefix)<4)return 0;
    const char *root=NULL;size_t root_size=0;
    for(uint32_t i=0;i<v->entry_count;i++)if(bw_pbb_selector_library_match(v->entries[i].destination)==1){
        root=v->entries[i].destination;const char *slash=strchr(root,'/');if(!slash)return -1;root_size=(size_t)(slash-root);break;
    }
    if(!root)return 0;
    if(p){const char *slash=strrchr(p->proposed_destination,'/');return !slash||(size_t)(slash-p->proposed_destination)!=root_size||memcmp(p->proposed_destination,root,root_size)?-1:0;}
    if(!base)return 0;
    unsigned matches=0;
    for(uint32_t i=0;i<base->count;i++){
        struct bw_manifest_record r;if(bw_manifest_get(base,i,&r))return -1;
        const char *slash=strrchr(r.path,'/');if(!slash||(size_t)(slash-r.path)!=root_size||memcmp(r.path,root,root_size))continue;
        size_t n=strlen(slash+1);if(n<9||order(slash+1+n-9,".uproject"))continue;
        if(++matches!=1||r.flags!=BW_MANIFEST_LOCAL||!r.size)return -1;
    }
    return matches==1?0:-1;
}
static int metadata_size(const struct bw_pbb_view *v,uint64_t *bytes){
    *bytes=0;if(bw_pbb_prefix_mode(v->prefix)!=5)return 0;
    for(uint32_t i=0;i<v->archive_count;i++)for(unsigned k=0;k<BW_PBB_SPANS;k++){
        uint64_t n=v->archives[i].spans[k].size;
        if(n>BW_PBB_MAX_METADATA_SEED-*bytes)return -1;*bytes+=n;
    }
    return 0;
}
static int startup_budget(const struct bw_batch_facts *f,uint64_t budget){
    uint64_t seed_bytes=0;return metadata_size(&f->bootstrap,&seed_bytes)||f->local_bytes>budget||seed_bytes>budget-f->local_bytes?-1:0;
}
int bw_batch_facts_hash(const struct bw_pbb_view *v,const struct bw_pp_plan *p,uint8_t facts[32],uint8_t rule[32]){
    if(facts)memset(facts,0,32);if(rule)memset(rule,0,32);
    if(!v||!facts||!rule||v->version!=2||string(v->source,sizeof v->source,0)||bw_manifest_validate_path(v->source,strlen(v->source))||string(v->game,sizeof v->game,1)||string(v->prefix,sizeof v->prefix,1)||!bw_pbb_prefix_mode(v->prefix)||zero(v->native_snapshot_sha256,32)||!v->archives||!v->entries||!v->archive_count||v->archive_count>64||!v->entry_count||v->entry_count>4096||v->bootstrap_bytes>BW_PBB_MAX_BOOTSTRAP||v->base_local_bytes>BW_PROVIDER_MAX_LOCAL_BYTES)return -1;
    struct cp_sha h;begin(&h,"Blackwire native packed startup facts v2");text(&h,v->source);text(&h,v->game);text(&h,v->prefix);cp_sha_update(&h,v->native_snapshot_sha256,32);h32(&h,v->archive_count);
    for(uint32_t i=0;i<v->archive_count;i++){const struct bw_pbb_archive *a=&v->archives[i];if(path(a->path,sizeof a->path,1)||(i&&strcmp(v->archives[i-1].path,a->path)>=0)||(a->info.attributes&(0x10u|0x40u|0x400u))||a->info.size<44||a->info.size>INT64_MAX||a->filename_count>BW_PBB_MAX_ARCHIVE_ENTRIES||a->directory_count>BW_PBB_MAX_ARCHIVE_ENTRIES)return -1;
        for(uint32_t j=0;j<i;j++)if(!order(a->path,v->archives[j].path))return -1;
        for(unsigned k=0;k<BW_PBB_SPANS;k++)if(span_valid(a,k))return -1;
        const struct bw_pbb_span *f=&a->spans[0],*primary=&a->spans[1],*hash=&a->spans[2],*names=&a->spans[3];uint64_t tail=a->info.size<4096?a->info.size:4096;
        if(f->offset!=a->info.size-tail||f->size!=tail||primary->offset>names->offset||primary->size>names->offset-primary->offset||names->offset>a->info.size-44||names->size>a->info.size-44-names->offset)return -1;
        if(hash->size&&(primary->offset>hash->offset||primary->size>hash->offset-primary->offset||hash->offset>a->info.size-44||hash->size>a->info.size-44-hash->offset||!(hash->offset+hash->size<=names->offset||names->offset+names->size<=hash->offset)))return -1;
        text(&h,a->path);info_hash(&h,&a->info);for(unsigned k=0;k<BW_PBB_SPANS;k++)span_hash(&h,&a->spans[k]);h32(&h,a->filename_count);h32(&h,a->directory_count);h64(&h,a->hash_seed);
    }
    uint64_t metadata_bytes=0;if(metadata_size(v,&metadata_bytes))return -1;
    uint64_t sum=0;h32(&h,v->entry_count);
    for(uint32_t i=0;i<v->entry_count;i++){const struct bw_pbb_entry *e=&v->entries[i];if(path(e->destination,sizeof e->destination,1)||bw_pbb_selector_match(v->prefix,e->destination)!=1||(i&&order(v->entries[i-1].destination,e->destination)>=0)||e->archive_index>=v->archive_count||e->flags!=BW_PBB_LOCAL||e->header_offset>UINT64_MAX-53||e->body_offset!=e->header_offset+53||e->size>BW_SOURCE_RANGE_LIMIT||e->size>BW_PBB_MAX_BOOTSTRAP-sum||zero(e->header_sha256,32)||zero(e->body_sha1,20)||zero(e->body_sha256,32))return -1;
        const struct bw_pbb_archive *a=&v->archives[e->archive_index];if(e->entry_location>=a->spans[1].size||e->body_offset>a->spans[1].offset||e->size>a->spans[1].offset-e->body_offset)return -1;
        sum+=e->size;text(&h,e->destination);h32(&h,e->archive_index);h32(&h,e->entry_location);h32(&h,e->flags);h64(&h,e->header_offset);h64(&h,e->body_offset);h64(&h,e->size);cp_sha_update(&h,e->header_sha256,32);cp_sha_update(&h,e->body_sha1,20);cp_sha_update(&h,e->body_sha256,32);
    }
    if(sum!=v->bootstrap_bytes||physical_unique(v)||library_directory_unique(v)||pp_valid(v,p)||canonical_project_root(v,p,NULL))return -1;h32(&h,p?1u:0u);if(p){uint8_t ph[32];if(bw_derived_plan_recipe_sha256(p,ph))return -1;cp_sha_update(&h,ph,32);}cp_sha_final(&h,facts);
    begin(&h,"Blackwire native packed startup destination rule v2");h32(&h,2);h32(&h,1);h32(&h,1);text(&h,v->prefix);if(bw_pbb_prefix_mode(v->prefix)==2)text(&h,BW_PBB_ENGINE_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(v->prefix)==3)text(&h,BW_PBB_UNREAL_STARTUP_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(v->prefix)==4)text(&h,BW_PBB_UNREAL_CANONICAL_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(v->prefix)==5)text(&h,BW_PBB_UNREAL_METADATA_SELECTOR_SEMANTICS);text(&h,"literal-prepared-root;original-records-preserved;explicit-live-grant;no-visibility-proof");h32(&h,p?1u:0u);if(p){text(&h,p->archive);text(&h,p->requested);text(&h,p->listed);text(&h,p->proposed_destination);}cp_sha_final(&h,rule);return 0;
}
static void policy(uint8_t out[32],int material,const char *prefix){struct cp_sha h;begin(&h,material?BW_BATCH_MATERIAL_DOMAIN:BW_BATCH_PLAN_DOMAIN);h32(&h,2);text(&h,prefix);if(bw_pbb_prefix_mode(prefix)==2)text(&h,BW_PBB_ENGINE_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(prefix)==3)text(&h,BW_PBB_UNREAL_STARTUP_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(prefix)==4)text(&h,BW_PBB_UNREAL_CANONICAL_SELECTOR_SEMANTICS);else if(bw_pbb_prefix_mode(prefix)==5)text(&h,BW_PBB_UNREAL_METADATA_SELECTOR_SEMANTICS);text(&h,"folder1;profile-none;immutable-base;flat-native-archive-origins;no-publication");h32(&h,BW_BATCH_MAX_BASE);h32(&h,BW_PBB_MAX_ARCHIVES);h32(&h,BW_PBB_MAX_FILES);h32(&h,BW_BATCH_MAX_RECORDS);h64(&h,BW_PBB_MAX_BOOTSTRAP);h64(&h,BW_BATCH_BOOTSTRAP_LIMIT);h64(&h,BW_PROVIDER_MAX_LOCAL_BYTES);cp_sha_final(&h,out);}
struct row {const char *path;uint32_t flags,kind,original,archive,entry;uint64_t size,mtime;const uint8_t *sha;};
static int rows_compare(const void *a,const void *b){return order(((const struct row *)a)->path,((const struct row *)b)->path);}
static int base_map(const struct bw_provider_binding *identity,const uint8_t *map,size_t mn,const uint8_t *dig,size_t dn,uint64_t bs,struct bw_manifest *m,struct bw_source_map *summary){
    if(!map||!dig||mn<40||mn>40u+BW_BATCH_MAX_BASE*(32u+241u)||!rd32(map+16)||rd32(map+16)>BW_BATCH_MAX_BASE||dn<56||dn>56u+32u*BW_BATCH_MAX_BASE||bw_manifest_open(m,map,mn)||dn!=56u+32u*m->count||memcmp(dig,"BWDIG001",8)||rd32(dig+8)!=1||rd32(dig+12)!=m->count||rd64(dig+16)!=m->checksum)return -1;
    uint8_t hash[32];cp_sha_bytes(map,mn,hash);if(memcmp(hash,dig+24,32))return -1;struct bw_source_plan p={0};int failed=bw_source_plan_init(&p,identity->binding.game);
    for(uint32_t i=0;!failed&&i<m->count;i++){struct bw_manifest_record r;if(bw_manifest_get(m,i,&r)||r.path_length>240||bw_source_excluded(r.path)||(r.flags==BW_MANIFEST_STREAM&&!zero(dig+56u+32u*i,32))||bw_source_plan_add(&p,r.path,r.size,r.mtime_sec))failed=1;}
    if(!failed)failed=bw_source_plan_finish(&p,summary);if(!failed)failed=summary->size!=mn||memcmp(summary->bytes,map,mn)||summary->bootstrap_bytes!=bs||summary->local_bytes>BW_PROVIDER_MAX_LOCAL_BYTES;bw_source_plan_destroy(&p);if(failed)bw_source_map_destroy(summary);return failed?-1:0;
}
void db_release(struct db_product *p){if(!p)return;free(p->map);free(p->digests);free(p->origins);memset(p,0,sizeof *p);}
static uint32_t archive_id(const struct bw_manifest *m,const struct bw_pbb_archive *a){for(uint32_t i=0;i<m->count;i++){struct bw_manifest_record r;if(bw_manifest_get(m,i,&r))return UINT32_MAX;if(!strcmp(r.path,a->path))return r.flags==BW_MANIFEST_STREAM&&r.size==a->info.size&&r.mtime_sec==bw_source_unix_mtime(a->info.mtime_ticks)?i:UINT32_MAX;}return UINT32_MAX;}
int db_build(struct db_product *out,const struct bw_provider_binding *id,const void *map,size_t mn,const void *dig,size_t dn,uint64_t bs,const struct bw_pbb_view *batch,const struct bw_pp_plan *project,const char **error){
    if(error)*error=NULL;if(!out)return bad(error,"batch_output_required");memset(out,0,sizeof *out);
    struct bw_manifest base={0};struct bw_source_map summary={0};struct row *rows=NULL;int failed=1;uint8_t fh[32],rh[32];
    if(!id||!batch||binding(id)||bw_batch_facts_hash(batch,project,fh,rh)||strcmp(id->binding.source,batch->source)||strcmp(id->binding.game,batch->game)||base_map(id,map,mn,dig,dn,bs,&base,&summary)||summary.local_bytes!=batch->base_local_bytes||canonical_project_root(batch,project,&base))goto done;
    uint64_t extra=batch->bootstrap_bytes+(project?project->spans[BW_PP_BODY].size:0);if(extra>BW_PROVIDER_MAX_LOCAL_BYTES-summary.local_bytes||extra>BW_BATCH_BOOTSTRAP_LIMIT-bs)goto done;
    uint64_t seed_bytes=0;if(metadata_size(batch,&seed_bytes)||seed_bytes>BW_PROVIDER_MAX_LOCAL_BYTES-summary.local_bytes-extra||seed_bytes>BW_BATCH_BOOTSTRAP_LIMIT-bs-extra)goto done;
    uint32_t count=base.count+batch->entry_count+(project?1u:0u);if(count>BW_BATCH_MAX_RECORDS)goto done;rows=calloc(count,sizeof *rows);if(!rows)goto done;
    for(uint32_t i=0;i<base.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&base,i,&r))goto done;rows[i]=(struct row){r.path,r.flags,BW_BATCH_ORIGINAL,i,UINT32_MAX,UINT32_MAX,r.size,r.mtime_sec,(const uint8_t *)dig+56u+32u*i};}
    for(uint32_t i=0;i<batch->archive_count;i++)if(archive_id(&base,&batch->archives[i])==UINT32_MAX)goto done;
    for(uint32_t i=0;i<batch->entry_count;i++){const struct bw_pbb_entry *e=&batch->entries[i];rows[base.count+i]=(struct row){e->destination,BW_MANIFEST_LOCAL,BW_BATCH_ICU,UINT32_MAX,e->archive_index,i,e->size,0,e->body_sha256};}
    if(project){uint32_t ai=0;while(ai<batch->archive_count&&strcmp(project->archive,batch->archives[ai].path))ai++;if(ai==batch->archive_count)goto done;rows[count-1]=(struct row){project->proposed_destination,BW_MANIFEST_LOCAL,BW_BATCH_PROJECT,UINT32_MAX,ai,UINT32_MAX,project->spans[BW_PP_BODY].size,0,project->spans[BW_PP_BODY].sha256};}
    qsort(rows,count,sizeof *rows,rows_compare);size_t paths=0;for(uint32_t i=0;i<count;i++){size_t n=strlen(rows[i].path)+1;if(n>241||paths>SIZE_MAX-n||(i&&order(rows[i-1].path,rows[i].path)>=0))goto done;paths+=n;}
    size_t size=40u+(size_t)count*32u+paths;if(size>DB_MAP_LIMIT)goto done;out->map=calloc(size,1);out->digests=calloc(56u+32u*count,1);out->origins=calloc(count,sizeof *out->origins);if(!out->map||!out->digests||!out->origins)goto done;
    uint8_t *wire=out->map;memcpy(wire,"BWMAP002",8);wr32(wire+8,2);wr32(wire+12,32);wr32(wire+16,count);wr64(wire+24,paths);size_t at=0;
    for(uint32_t i=0;i<count;i++){const struct row *r=&rows[i];size_t n=strlen(r->path);uint8_t *record=wire+40u+32u*i;wr32(record,(uint32_t)at);wr32(record+4,(uint32_t)n);wr32(record+8,r->flags);wr64(record+16,r->size);wr64(record+24,r->mtime);memcpy(wire+40u+32u*count+at,r->path,n+1);at+=n+1;out->origins[i]=(struct bw_batch_origin){i,r->kind,r->original,r->archive,r->entry};memcpy(out->digests+56u+32u*i,r->sha,32);}
    wr64(wire+32,bw_manifest_fnv1a64(wire+40,size-40));struct bw_manifest checked;if(bw_manifest_open(&checked,wire,size))goto done;
    struct bw_batch_facts *f=&out->f;f->original=*id;f->bootstrap=*batch;f->bootstrap.epoch=0;f->has_project=project?1u:0u;if(project){f->project=*project;f->project.read_bytes=0;f->project.read_calls=f->project.current_checks=0;}
    f->base_map=map;f->base_map_size=mn;f->base_digests=dig;f->base_digest_size=dn;f->base_bootstrap_bytes=bs;f->map=wire;f->map_size=size;f->digests=out->digests;f->digest_size=56u+32u*count;f->origins=out->origins;f->count=count;f->local_count=summary.local_count+batch->entry_count+f->has_project;f->stream_count=summary.stream_count;f->local_bytes=summary.local_bytes+extra;f->remote_bytes=summary.remote_bytes;f->bootstrap_bytes=bs+extra;
    cp_sha_bytes(map,mn,f->base_map_sha256);cp_sha_bytes(dig,dn,f->base_digests_sha256);memcpy(f->facts_sha256,fh,32);memcpy(f->mapping_rule_sha256,rh,32);policy(f->prepared.policy_sha256,0,batch->prefix);policy(f->material_policy_sha256,1,batch->prefix);f->prepared.policy=2;memcpy(f->prepared.domain,BW_BATCH_PLAN_DOMAIN,sizeof BW_BATCH_PLAN_DOMAIN);
    struct cp_sha h;begin(&h,"Blackwire native packed startup namespace v2");cp_sha_update(&h,f->prepared.policy_sha256,32);text(&h,id->binding.source);text(&h,id->binding.game);text(&h,id->binding.title);cp_sha_update(&h,id->binding.sha256,32);cp_sha_update(&h,batch->native_snapshot_sha256,32);cp_sha_update(&h,f->base_map_sha256,32);cp_sha_update(&h,f->base_digests_sha256,32);cp_sha_update(&h,fh,32);cp_sha_update(&h,rh,32);cp_sha_final(&h,f->prepared.namespace_sha256);
    memcpy(f->prepared.component,id->binding.title,9);f->prepared.component[9]='_';static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++){f->prepared.component[10+2*i]=hex[f->prepared.namespace_sha256[i]>>4];f->prepared.component[11+2*i]=hex[f->prepared.namespace_sha256[i]&15];}f->prepared.component[74]=0;
    cp_sha_bytes(wire,size,f->map_sha256);memcpy(out->digests,"BWDIG001",8);wr32(out->digests+8,1);wr32(out->digests+12,count);wr64(out->digests+16,checked.checksum);memcpy(out->digests+24,f->map_sha256,32);cp_sha_bytes(out->digests,f->digest_size,f->digests_sha256);
    begin(&h,"Blackwire native packed startup origins v2");cp_sha_update(&h,f->prepared.namespace_sha256,32);cp_sha_update(&h,f->base_map_sha256,32);cp_sha_update(&h,f->base_digests_sha256,32);cp_sha_update(&h,fh,32);h32(&h,count);for(uint32_t i=0;i<count;i++){const struct bw_batch_origin *o=&out->origins[i];h32(&h,o->file_id);h32(&h,o->kind);h32(&h,o->original_id);h32(&h,o->archive_index);h32(&h,o->entry_index);}cp_sha_final(&h,f->origins_sha256);
    begin(&h,"Blackwire native packed startup plan commitment v2");cp_sha_update(&h,f->prepared.namespace_sha256,32);cp_sha_update(&h,f->map_sha256,32);cp_sha_update(&h,f->digests_sha256,32);cp_sha_update(&h,f->origins_sha256,32);cp_sha_final(&h,f->prepared.commitment_sha256);
    begin(&h,"Blackwire native packed startup material refinement v2");cp_sha_update(&h,f->material_policy_sha256,32);cp_sha_update(&h,rh,32);h32(&h,1);h32(&h,1);text(&h,id->binding.source);text(&h,id->binding.game);text(&h,id->binding.title);cp_sha_update(&h,id->binding.sha256,32);cp_sha_update(&h,batch->native_snapshot_sha256,32);cp_sha_update(&h,f->base_map_sha256,32);cp_sha_update(&h,f->base_digests_sha256,32);cp_sha_update(&h,f->prepared.namespace_sha256,32);cp_sha_update(&h,f->prepared.commitment_sha256,32);cp_sha_final(&h,f->material_sha256);failed=0;
done:free(rows);bw_source_map_destroy(&summary);if(failed){db_release(out);return bad(error,"batch_base_facts_collision_or_budget_refused");}return 0;
}
int db_check(const struct bw_batch_facts *f,const char **error){
    if(!f||f->has_project>1||f->project.read_bytes||f->project.read_calls||f->project.current_checks||(!f->has_project&&!zero(&f->project,sizeof f->project))||f->bootstrap.epoch||f->count>BW_BATCH_MAX_RECORDS||!f->origins||!f->map||!f->digests||f->map_size>DB_MAP_LIMIT||f->digest_size>DB_DIG_LIMIT)return bad(error,"batch_historical_bounds");
    struct db_product p;int rc=db_build(&p,&f->original,f->base_map,f->base_map_size,f->base_digests,f->base_digest_size,f->base_bootstrap_bytes,&f->bootstrap,f->has_project?&f->project:NULL,error);if(rc)return rc;
    const struct bw_batch_facts *g=&p.f;int different=f->count!=g->count||f->map_size!=g->map_size||f->digest_size!=g->digest_size||f->local_count!=g->local_count||f->stream_count!=g->stream_count||f->local_bytes!=g->local_bytes||f->remote_bytes!=g->remote_bytes||f->bootstrap_bytes!=g->bootstrap_bytes||!prepared_equal(&f->prepared,&g->prepared)||memcmp(f->base_map_sha256,g->base_map_sha256,32)||memcmp(f->base_digests_sha256,g->base_digests_sha256,32)||memcmp(f->facts_sha256,g->facts_sha256,32)||memcmp(f->mapping_rule_sha256,g->mapping_rule_sha256,32)||memcmp(f->material_policy_sha256,g->material_policy_sha256,32)||memcmp(f->map_sha256,g->map_sha256,32)||memcmp(f->digests_sha256,g->digests_sha256,32)||memcmp(f->origins_sha256,g->origins_sha256,32)||memcmp(f->material_sha256,g->material_sha256,32);
    if(!different)different=memcmp(f->map,g->map,g->map_size)||memcmp(f->digests,g->digests,g->digest_size)||memcmp(f->origins,g->origins,(size_t)g->count*sizeof *g->origins);db_release(&p);return different?bad(error,"batch_historical_cross_field_or_commitment"):0;
}

struct db_live {
    struct bw_batch_plan_request request;
    struct bw_pbb_request cache_request;
    struct bw_derived_plan_request project_request;
    struct bw_batch_destination_grant grant;
    struct bw_batch_destination_claim claim;
    struct bw_provider_binding binding;
    struct bw_source_smb *source;struct smb2_context *connection;double deadline;
    struct bw_source_snapshot snapshot;
    struct bw_source_map base;
    const uint8_t *base_digests;size_t base_digest_size;
    const void *cache_state,*local_state,*project_state;
    struct bw_pbb_view cache_view;
    struct bw_derived_plan_view project_view;
    struct bw_pp_plan project;
    struct bw_pbb_archive *archives;struct bw_pbb_entry *entries;
    struct db_product product;struct bw_batch_facts visible;
};
static int live_fail(struct bw_batch_plan *p,const char *s){if(p){p->failed=1;if(!p->error)p->error=s;if(p->state)memset(&((struct db_live *)p->state)->visible,0,sizeof(struct bw_batch_facts));}return -1;}
static int live_guard(struct bw_batch_plan *p){struct db_live *s=p->state;struct bw_source_provider *a=s->request.original;struct timespec now;
    if(p->failed||!a||a->failed||a->error||a->busy||a->source!=s->source||!s->source||s->source->error||s->source->smb!=s->connection||!s->connection||s->source->deadline!=s->deadline||clock_gettime(CLOCK_MONOTONIC,&now)||s->deadline<=(double)now.tv_sec+(double)now.tv_nsec/1e9||a->current!=s->cache_request.current||a->opaque!=s->cache_request.job||!a->current)return live_fail(p,"batch_live_owner_or_deadline");
    if(a->current(a->opaque)||p->failed)return live_fail(p,"batch_current_job_refused");return 0;}
static int grant_current(struct bw_batch_plan *p){struct db_live *s=p->state;const struct bw_batch_destination_grant *g=s->request.destination;
    if(live_guard(p)||!g||memcmp(g,&s->grant,sizeof *g))return live_fail(p,"batch_destination_grant_changed");
    int rc=g->check(g,&s->claim);if(rc||p->failed||memcmp(g,&s->grant,sizeof *g)||live_guard(p))return live_fail(p,"batch_destination_grant_refused");return 0;}
static int live_base(struct bw_batch_plan *p){struct db_live *s=p->state;struct bw_source_provider *a=s->request.original;struct bw_source_preparation *f=&a->folder;const struct bw_pbb_request *q=s->request.bootstrap_request;
    if(live_guard(p)||!q||memcmp(q,&s->cache_request,sizeof *q)||binding(&a->identity)||memcmp(&a->identity,&s->binding,sizeof s->binding)||!a->bundle.ready||!f->verified||a->budget<s->request.startup_budget||a->budget>BW_PROVIDER_MAX_LOCAL_BYTES||!zero(f->profile_sha256,32)||a->bundle.map.bytes!=s->base.bytes||f->map.bytes!=s->base.bytes||a->bundle.map.size!=s->base.size||f->map.size!=s->base.size||a->bundle.digests!=s->base_digests||f->digests!=s->base_digests||a->bundle.digest_size!=s->base_digest_size||f->digest_size!=s->base_digest_size||memcmp(&f->snapshot,&s->snapshot,sizeof s->snapshot)||bw_source_snapshot_validate(&f->snapshot)||memcmp(a->bundle.generation_sha256,s->snapshot.sha256,32)||a->bundle.map.local_bytes!=s->base.local_bytes||a->bundle.map.remote_bytes!=s->base.remote_bytes||a->bundle.map.bootstrap_bytes!=s->base.bootstrap_bytes||a->bundle.map.local_count!=s->base.local_count||a->bundle.map.stream_count!=s->base.stream_count)return live_fail(p,"batch_original_binding_or_generation_changed");
    if(string(a->bundle.source,sizeof a->bundle.source,0)||string(a->bundle.game,sizeof a->bundle.game,1)||string(a->bundle.title,sizeof a->bundle.title,1)||strcmp(a->bundle.source,s->binding.binding.source)||strcmp(a->bundle.game,s->binding.binding.game)||strcmp(a->bundle.title,s->binding.binding.title)||strcmp(f->snapshot.source,s->binding.binding.source)||strcmp(f->snapshot.game,s->binding.binding.game))return live_fail(p,"batch_original_labels_changed");
    uint8_t h[32];cp_sha_bytes(s->base.bytes,s->base.size,h);if(memcmp(h,s->claim.base_map_sha256,32))return live_fail(p,"batch_original_map_changed");cp_sha_bytes(s->base_digests,s->base_digest_size,h);if(memcmp(h,s->claim.base_digests_sha256,32))return live_fail(p,"batch_original_digests_changed");
    return live_guard(p);}
static int live_cache(struct bw_batch_plan *p){struct db_live *s=p->state;const struct bw_pbb_request *q=&s->cache_request;
    if(live_guard(p)||s->request.bootstrap->state!=s->cache_state||s->request.bootstrap->busy||s->request.bootstrap->failed)return live_fail(p,"batch_cache_owner_changed");
    const struct bw_pbb_view *v=bw_packed_bootstrap_view(s->request.bootstrap);
    if(!v||p->failed||memcmp(v,&s->cache_view,sizeof *v)||memcmp(v->archives,s->archives,(size_t)v->archive_count*sizeof *s->archives)||memcmp(v->entries,s->entries,(size_t)v->entry_count*sizeof *s->entries))return live_fail(p,"batch_cache_facts_changed");
    for(uint32_t i=0;i<v->archive_count;i++)if(!q->archives||!q->archives[i]||string(q->archives[i],241,1)||strcmp(q->archives[i],s->archives[i].path))return live_fail(p,"batch_cache_archive_request_changed");
    return live_guard(p);}
static int live_project(struct bw_batch_plan *p){struct db_live *s=p->state;if(!s->request.project_local)return 0;
    if(live_guard(p)||!s->request.project_request||memcmp(s->request.project_request,&s->project_request,sizeof s->project_request)||s->request.project_local->state!=s->local_state||s->project_request.project->state!=s->project_state)return live_fail(p,"batch_project_owner_changed");
    const struct bw_derived_plan_view *v=bw_derived_local_view(s->request.project_local);const struct bw_pp_plan *r=bw_packed_project_view(s->project_request.project);uint8_t h[32];
    if(!v||!r||p->failed||memcmp(v,&s->project_view,sizeof *v)||bw_derived_plan_recipe_sha256(r,h)||memcmp(h,v->recipe_sha256,32)||memcmp(v->base_snapshot_sha256,s->snapshot.sha256,32)||memcmp(v->base_map_sha256,s->claim.base_map_sha256,32)||memcmp(v->base_digests_sha256,s->claim.base_digests_sha256,32)||bw_derived_plan_recipe_sha256(&s->project,h)||memcmp(h,v->recipe_sha256,32))return live_fail(p,"batch_project_facts_changed");return live_guard(p);}
static int live_metadata(struct bw_batch_plan *p){return live_base(p)||live_cache(p)||live_project(p)||grant_current(p)?-1:0;}
static int fresh(struct bw_batch_plan *p){struct db_live *s=p->state;
    if(live_metadata(p)||bw_source_provider_current(s->request.original)||live_metadata(p))return live_fail(p,"batch_provider_current_refused");
    if(s->request.project_local&&(bw_packed_project_revalidate(s->project_request.project)||live_metadata(p)))return live_fail(p,"batch_project_revalidation_refused");
    if(bw_source_provider_current(s->request.original)||live_metadata(p))return live_fail(p,"batch_provider_after_metadata_refused");return 0;}
int bw_batch_plan_open(struct bw_batch_plan *p,const struct bw_batch_plan_request *r){
    if(!p)return -1;if(p->state||p->busy||p->failed)return live_fail(p,"batch_new_zeroed_session_required");
    if(!r||r->policy!=2||!r->original||!r->original->source||!r->bootstrap||!r->bootstrap->state||!r->bootstrap_request||!r->destination||r->destination->version!=2||!r->destination->owner||!r->destination->epoch||!r->destination->check||!r->startup_budget||r->startup_budget>BW_PROVIDER_MAX_LOCAL_BYTES||((r->project_local==NULL)!=(r->project_request==NULL)))return live_fail(p,"batch_arguments_or_explicit_grant");
    struct bw_source_provider *a=r->original;const struct bw_pbb_request *q=r->bootstrap_request;
    if(q->version!=2||!q->epoch||q->epoch!=r->destination->epoch||q->source!=a->source||q->snapshot!=&a->folder.snapshot||q->current!=a->current||q->job!=a->opaque||!q->current||!q->job||q->source_kind!=BW_PROVIDER_FOLDER||q->source_policy!=1||!q->profile||string(q->profile,5,1)||strcmp(q->profile,"none")||!q->prefix||string(q->prefix,sizeof BW_PBB_PREFIX,1)||!bw_pbb_prefix_mode(q->prefix)||q->base_local_bytes!=a->bundle.map.local_bytes||q->startup_budget!=r->startup_budget||!a->bundle.map.bytes||a->bundle.map.size>40u+4096u*(32u+241u)||!a->bundle.digests||a->bundle.digest_size<56||a->bundle.digest_size>56u+32u*4096u||!(a->source->deadline>=0&&a->source->deadline<=DBL_MAX))return live_fail(p,"batch_coherent_native_cache_request_required");
    if(r->project_local&&(!r->project_local->state||!r->project_request->project||!r->project_request->project->state||r->project_request->original!=a||r->project_request->current_job!=q->current||r->project_request->job!=q->job||!r->project_request->destination||r->project_request->destination->epoch!=q->epoch||r->project_request->policy!=1))return live_fail(p,"batch_coherent_project_request_required");
    struct db_live *s=calloc(1,sizeof *s);if(!s)return live_fail(p,"batch_allocation");p->state=s;p->busy=1;s->request=*r;s->cache_request=*q;s->grant=*r->destination;s->binding=a->identity;s->source=a->source;s->connection=a->source->smb;s->deadline=a->source->deadline;s->snapshot=a->folder.snapshot;s->base=a->bundle.map;s->base_digests=a->bundle.digests;s->base_digest_size=a->bundle.digest_size;s->cache_state=r->bootstrap->state;
    s->claim=(struct bw_batch_destination_claim){.original=a,.bootstrap=r->bootstrap,.project_local=r->project_local,.current_job=q->current,.job=q->job,.epoch=q->epoch};memcpy(s->claim.binding_sha256,a->identity.binding.sha256,32);memcpy(s->claim.snapshot_sha256,s->snapshot.sha256,32);cp_sha_bytes(s->base.bytes,s->base.size,s->claim.base_map_sha256);cp_sha_bytes(s->base_digests,s->base_digest_size,s->claim.base_digests_sha256);
    int rc=-1;if(live_guard(p))goto done;const struct bw_pbb_view *v=bw_packed_bootstrap_view(r->bootstrap);
    if(!v||v->archive_count>64||v->entry_count>4096||!v->archive_count||!v->entry_count||v->epoch!=q->epoch||v->version!=2||strcmp(v->prefix,q->prefix)||v->base_local_bytes!=s->base.local_bytes||memcmp(v->native_snapshot_sha256,s->snapshot.sha256,32))goto done;
    s->cache_view=*v;s->archives=calloc(v->archive_count,sizeof *s->archives);s->entries=calloc(v->entry_count,sizeof *s->entries);if(!s->archives||!s->entries)goto done;memcpy(s->archives,v->archives,(size_t)v->archive_count*sizeof *s->archives);memcpy(s->entries,v->entries,(size_t)v->entry_count*sizeof *s->entries);
    if(r->project_local){s->project_request=*r->project_request;s->local_state=r->project_local->state;s->project_state=s->project_request.project->state;const struct bw_derived_plan_view *pv=bw_derived_local_view(r->project_local);const struct bw_pp_plan *pr=bw_packed_project_view(s->project_request.project);if(!pv||!pr||p->failed)goto done;s->project_view=*pv;s->project=*pr;s->project.read_bytes=0;s->project.read_calls=s->project.current_checks=0;}
    if(bw_batch_facts_hash(v,r->project_local?&s->project:NULL,s->claim.facts_sha256,s->claim.mapping_rule_sha256)||memcmp(s->claim.mapping_rule_sha256,s->grant.mapping_rule_sha256,32)||fresh(p))goto done;
    struct bw_pbb_view copied=*v;copied.archives=s->archives;copied.entries=s->entries;
    if(db_build(&s->product,&s->binding,s->base.bytes,s->base.size,s->base_digests,s->base_digest_size,s->base.bootstrap_bytes,&copied,r->project_local?&s->project:NULL,&p->error)||startup_budget(&s->product.f,r->startup_budget)||live_metadata(p))goto done;s->visible=s->product.f;rc=0;
done:p->busy=0;if(rc)return live_fail(p,p->error?p->error:"batch_live_composition_refused");return 0;
}
const struct bw_batch_facts *bw_batch_plan_view(struct bw_batch_plan *p){
    if(!p||!p->state||p->failed)return NULL;if(p->busy){live_fail(p,"batch_callback_reentry");return NULL;}p->busy=1;struct db_live *s=p->state;
    int rc=memcmp(&s->visible,&s->product.f,sizeof s->visible)||live_metadata(p)||db_check(&s->visible,&p->error)||live_metadata(p)||p->failed;p->busy=0;if(rc){live_fail(p,"batch_view_changed_or_current_refused");return NULL;}return &s->visible;
}
int bw_batch_plan_request_matches(struct bw_batch_plan *p,const struct bw_batch_plan_request *r){
    if(!p||!p->state||!r||p->failed)return -1;
    if(p->busy)return live_fail(p,"batch_callback_reentry");
    struct db_live *s=p->state;
    if(r->policy!=s->request.policy||r->original!=s->request.original||r->bootstrap!=s->request.bootstrap||r->bootstrap_request!=s->request.bootstrap_request||r->project_local!=s->request.project_local||r->project_request!=s->request.project_request||r->destination!=s->request.destination||r->startup_budget!=s->request.startup_budget)return live_fail(p,"batch_request_owner_mismatch");
    return bw_batch_plan_view(p)?0:-1;
}
int bw_batch_plan_revalidate(struct bw_batch_plan *p){
    if(!p||!p->state||p->failed)return -1;if(p->busy)return live_fail(p,"batch_callback_reentry");p->busy=1;struct db_live *s=p->state;
    int rc=memcmp(&s->visible,&s->product.f,sizeof s->visible)||fresh(p)||db_check(&s->visible,&p->error)||live_metadata(p)||p->failed;p->busy=0;return rc?live_fail(p,"batch_fresh_revalidation_refused"):0;
}
int bw_batch_plan_destroy(struct bw_batch_plan *p){if(!p)return -1;if(p->busy)return live_fail(p,"batch_destroy_busy");if(p->state){struct db_live *s=p->state;db_release(&s->product);free(s->archives);free(s->entries);free(s);}memset(p,0,sizeof *p);return 0;}

struct db_material_state {
    struct bw_batch_material_request request;struct bw_native_prepare_selection accepted;
    struct bw_native_job_token token;struct bw_native_job_view *jobs;
    const struct bw_batch_facts *facts;struct bw_batch_material_view visible,captured;
};
static int material_fail(struct bw_batch_material *m,const char *s){if(m){m->failed=1;if(!m->error)m->error=s;if(m->state)memset(&((struct db_material_state *)m->state)->visible,0,sizeof(struct bw_batch_material_view));}return -1;}
static int running(struct db_material_state *s){const struct bw_native_job_token *t=s->request.token;if(t->owner!=s->request.jobs||memcmp(t,&s->token,sizeof *t)||!s->token.generation||string(t->id,sizeof t->id,1)||strlen(t->id)!=32||strcmp(t->id,s->accepted.run)||bw_native_jobs_snapshot(s->request.jobs,s->jobs)||s->jobs->count>BW_NATIVE_JOB_LIMIT)return -1;
    unsigned matches=0;for(size_t i=0;i<s->jobs->count;i++){const struct bw_native_job_record *j=&s->jobs->jobs[i];if(j->state!=BW_JOB_RUNNING&&j->state!=BW_JOB_QUEUED)continue;if(j->state!=BW_JOB_RUNNING||string(j->id,sizeof j->id,1)||string(j->source,sizeof j->source,0)||strcmp(j->id,t->id)||j->generation!=t->generation||strcmp(j->source,s->accepted.identity.binding.source))return -1;matches++;}return matches==1?0:-1;}
static int catalog_info(const struct bw_source_info *a,const struct bw_source_info *b){return a&&b&&a->size==b->size&&a->inode==b->inode&&a->mtime_ticks/10u==b->mtime_ticks/10u&&a->ctime_ticks/10u==b->ctime_ticks/10u&&a->attributes==b->attributes;}
static int material_inspect(struct bw_batch_material *m){struct db_material_state *s=m->state;
    if(memcmp(s->request.accepted,&s->accepted,sizeof s->accepted)||running(s))return -1;const struct bw_batch_facts *f=bw_batch_plan_view(s->request.plan);if(!f||m->failed||f!=s->facts||memcmp(&f->original,&s->accepted.identity,sizeof f->original))return -1;
    struct db_live *p=s->request.plan->state;struct bw_source_provider *a=p->request.original;
    if(p->grant.epoch!=s->token.generation||p->cache_request.epoch!=s->token.generation||s->accepted.container_kind||s->accepted.logical_boot_size||!catalog_info(&s->accepted.boot,bw_source_snapshot_find(&a->folder.snapshot,"eboot.bin"))||!catalog_info(&s->accepted.param,bw_source_snapshot_find(&a->folder.snapshot,"sce_sys/param.json")))return -1;
    struct bw_manifest map;if(bw_manifest_open(&map,a->bundle.map.bytes,a->bundle.map.size))return -1;char expected[2048];int n=snprintf(expected,sizeof expected,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%016"PRIx64"\n",s->accepted.run,s->accepted.identity.binding.title,s->accepted.identity.binding.game,s->accepted.boot.size,map.count,map.checksum);
    if(n<0||(size_t)n>=sizeof expected||!memchr(a->bundle.selection,0,sizeof a->bundle.selection)||!memchr(a->folder.selection,0,sizeof a->folder.selection)||strcmp(expected,a->bundle.selection)||strcmp(expected,a->folder.selection)||running(s))return -1;return 0;
}
int bw_batch_material_open(struct bw_batch_material *m,const struct bw_batch_material_request *r){
    if(!m)return -1;if(m->state||m->busy||m->failed)return material_fail(m,"batch_material_new_zeroed_required");
    if(!r||!r->plan||!r->plan->state||!r->accepted||!r->jobs||!r->token||r->token->owner!=r->jobs||!r->token->generation||binding(&r->accepted->identity)||string(r->accepted->run,sizeof r->accepted->run,1)||strlen(r->accepted->run)!=32)return material_fail(m,"batch_material_actual_acceptance_required");
    unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",r->accepted->run[i]))return material_fail(m,"batch_material_run");nonzero|=r->accepted->run[i]!='0';}if(!nonzero)return material_fail(m,"batch_material_run");
    struct db_material_state *s=calloc(1,sizeof *s);if(!s)return material_fail(m,"batch_material_allocation");s->jobs=calloc(1,sizeof *s->jobs);if(!s->jobs){free(s);return material_fail(m,"batch_material_allocation");}m->state=s;m->busy=1;s->request=*r;s->accepted=*r->accepted;s->token=*r->token;s->facts=bw_batch_plan_view(r->plan);
    int rc=!s->facts||material_inspect(m)||m->failed;if(!rc){s->captured.facts=s->facts;memcpy(s->captured.run,s->accepted.run,33);s->captured.job_generation=s->token.generation;s->visible=s->captured;}m->busy=0;return rc?material_fail(m,"batch_material_admission_refused"):0;
}
const struct bw_batch_material_view *bw_batch_material_view(struct bw_batch_material *m){if(!m||!m->state||m->failed)return NULL;if(m->busy){material_fail(m,"batch_material_reentry");return NULL;}m->busy=1;struct db_material_state *s=m->state;int rc=memcmp(&s->visible,&s->captured,sizeof s->visible)||material_inspect(m)||m->failed;m->busy=0;if(rc){material_fail(m,"batch_material_changed");return NULL;}return &s->visible;}
int bw_batch_material_destroy(struct bw_batch_material *m){if(!m)return -1;if(m->busy)return material_fail(m,"batch_material_destroy_busy");if(m->state){struct db_material_state *s=m->state;free(s->jobs);free(s);}memset(m,0,sizeof *m);return 0;}
