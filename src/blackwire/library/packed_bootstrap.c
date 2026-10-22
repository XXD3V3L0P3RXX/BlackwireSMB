#include "packed_bootstrap.h"
#include "packed_sha1.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <float.h>
#include <limits.h>

#define PBB_RESERVE UINT64_C(131072)
struct pbb_table {struct bw_pbb_archive *archives;struct bw_pbb_entry *entries;uint32_t count;uint64_t bytes;char project_root[241];};
struct pbb_state {
    struct bw_packed_bootstrap *owner;struct bw_pbb_request request;
    struct smb2_context *context;uint8_t seal[32];char source[BW_MANIFEST_MAX_PATH+1],game[241],prefix[sizeof BW_PBB_PREFIX];
    struct pbb_table facts;struct bw_pbb_archive *public_archives;struct bw_pbb_entry *public_entries;
    const char **original_names;size_t original_count;
    uint8_t *cache;uint64_t cache_size,cache_offsets[BW_PBB_MAX_FILES];
    struct bw_pbb_view view;struct bw_pbb_stats stats;uint8_t delivered[BW_PBB_MAX_FILES];
    uint8_t metadata_delivered[BW_PBB_MAX_ARCHIVES];uint64_t metadata_size,metadata_emitted;
    uint32_t request_start,request_last;uint64_t byte_start,byte_last;double deadline;
    bw_source_observer_fn previous;void *previous_opaque;double saved_deadline;
    int observing,inside_observer,latched;
};
struct cursor {const uint8_t *p;size_t size,at;};
struct key {const char *directory,*filename;size_t dn,fn;};
struct read_work {struct pbb_state *s;uint8_t *buffer;uint64_t size,used;struct cp_sha sha256;struct pp_sha1 sha1;bw_source_consume_fn consume;void *opaque;int body;};
static int fail(struct pbb_state *s,const char *e){if(s){s->owner->failed=1;if(!s->owner->error)s->owner->error=e;}return -1;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int folded(const char *a,const char *b){while(*a&&*b){unsigned x=fold((unsigned char)*a++),y=fold((unsigned char)*b++);if(x!=y)return x<y?-1:1;}return *a?1:*b?-1:0;}
static int prefix_fold(const char *a,const char *b){while(*b){if(!*a||fold((unsigned char)*a++)!=fold((unsigned char)*b++))return 0;}return 1;}
static int copy(char *out,size_t cap,const char *p){if(!p)return -1;const char *end=memchr(p,0,cap);if(!end)return -1;memcpy(out,p,(size_t)(end-p)+1);return 0;}
static int safe_path(const char *p,size_t cap){
    size_t n=strlen(p),begin=0;if(!n||n>cap)return -1;
    for(size_t i=0;i<=n;i++){
        unsigned c=(unsigned char)p[i];if(i<n&&(c<32||c>=127||strchr("\\:*?\"<>|~%",(int)c)))return -1;
        if(c=='/'||!c){size_t z=i-begin;if(!z||(z==1&&p[begin]=='.')||(z==2&&p[begin]=='.'&&p[begin+1]=='.')||p[i-1]==' '||p[i-1]=='.')return -1;
            size_t k=0;while(k<z&&p[begin+k]!='.')k++;char stem[5]={0};if(k<=4){for(size_t j=0;j<k;j++)stem[j]=(char)fold((unsigned char)p[begin+j]);if(!strcmp(stem,"con")||!strcmp(stem,"prn")||!strcmp(stem,"aux")||!strcmp(stem,"nul")||(k==4&&(!memcmp(stem,"com",3)||!memcmp(stem,"lpt",3))&&stem[3]>='1'&&stem[3]<='9'))return -1;}begin=i+1;
        }
    }return 0;
}
static int same_info(const struct bw_source_info *a,const struct bw_source_info *b){return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;}
static int snapshot(struct pbb_state *s){
    const struct bw_source_snapshot *p=s->request.snapshot;
    if(bw_source_snapshot_validate(p)||!p->parent_count||memcmp(p->sha256,s->seal,32)||strcmp(p->source,s->source)||strcmp(p->game,s->game))return fail(s,"bootstrap_snapshot_changed");
    for(uint32_t i=0;i<s->request.archive_count;i++){const struct bw_pbb_archive *a=&s->facts.archives[i];const struct bw_source_info *info=bw_source_snapshot_find(p,a->path);if(!info||!same_info(info,&a->info))return fail(s,"bootstrap_archive_generation_changed");}
    return 0;
}
/* Attribute only deltas observed while our scoped observer owns the source to
 * this cache's transport cap. Idle external work remains a checked lifetime
 * fact; its actual controller owns the separate job-wide cap. */
static int meter(struct pbb_state *s){
    struct bw_source_smb *p=s->request.source;if(!p)return 0;
    if(p->requests<s->request_last||p->content_bytes<s->byte_last)return fail(s,"bootstrap_counter_rewound");
    uint32_t requests=p->requests-s->request_last;uint64_t bytes=p->content_bytes-s->byte_last;
    uint32_t *owned_requests=s->observing?&s->stats.cache_owned_requests:&s->stats.external_requests;
    uint64_t *owned_bytes=s->observing?&s->stats.cache_owned_source_bytes:&s->stats.external_source_bytes;
    if(requests>UINT32_MAX-s->stats.source_requests||requests>UINT32_MAX-*owned_requests||
       bytes>UINT64_MAX-s->stats.accepted_source_bytes||bytes>UINT64_MAX-*owned_bytes)return fail(s,"bootstrap_counter_overflow");
    s->stats.source_requests+=requests;s->stats.accepted_source_bytes+=bytes;
    *owned_requests+=requests;*owned_bytes+=bytes;s->request_last=p->requests;s->byte_last=p->content_bytes;
    return 0;
}
static int transport_limit(struct pbb_state *s){
    /* One request may be queued after a progress callback; content accounting
     * may trail an accepted 64KiB reply and that newly queued 64KiB read. */
    if(s->stats.cache_owned_requests>=s->request.request_limit-1u||
       s->stats.cache_owned_source_bytes>s->request.read_byte_limit-PBB_RESERVE)return fail(s,"bootstrap_cumulative_source_limit");
    return 0;
}
static int guard(void *opaque){
    struct pbb_state *s=opaque;struct bw_source_smb *p=s->request.source;struct timespec now;
    if(s->owner->failed)return -1;
    if(!p->smb||p->smb!=s->context||p->error||clock_gettime(CLOCK_MONOTONIC,&now))return fail(s,"bootstrap_source_not_owned_current");
    double t=(double)now.tv_sec+(double)now.tv_nsec/1e9;
    if(!(p->deadline>=-DBL_MAX&&p->deadline<=DBL_MAX)||t>=s->deadline||t>=p->deadline)return fail(s,"bootstrap_deadline_refused");
    if(meter(s)||transport_limit(s))return -1;
    if(p->requests>UINT32_MAX-2u||p->content_bytes>UINT64_MAX-PBB_RESERVE)return fail(s,"bootstrap_counter_headroom");
    if(s->stats.current_checks==UINT32_MAX)return fail(s,"bootstrap_current_counter_limit");s->stats.current_checks++;
    if(s->request.current(s->request.job)||s->owner->failed)return fail(s,"bootstrap_job_refused");
    if(p->smb!=s->context||meter(s)||transport_limit(s)||clock_gettime(CLOCK_MONOTONIC,&now))return fail(s,"bootstrap_callback_source_changed");
    t=(double)now.tv_sec+(double)now.tv_nsec/1e9;
    if(!(p->deadline>=-DBL_MAX&&p->deadline<=DBL_MAX)||t>=s->deadline||t>=p->deadline)return fail(s,"bootstrap_deadline_refused");
    return 0;
}
static void observe(void *opaque,const struct bw_source_progress *progress){
    struct pbb_state *s=opaque;
    if(s->inside_observer){fail(s,"bootstrap_observer_reentry");s->latched=1;s->request.source->deadline=-1;return;}
    s->inside_observer=1;
    if(guard(s))s->latched=1;
    if(!s->latched&&s->previous)s->previous(s->previous_opaque,progress);
    if(guard(s))s->latched=1;
    if(s->latched)s->request.source->deadline=-1;
    s->inside_observer=0;
}
static int watch(struct pbb_state *s){
    struct bw_source_smb *p=s->request.source;
    if(s->observing||p->observe==observe)return fail(s,"bootstrap_nested_source_operation");
    if(meter(s)||transport_limit(s))return -1;
    s->previous=p->observe;s->previous_opaque=p->observe_opaque;s->saved_deadline=p->deadline;s->latched=0;s->observing=1;p->observe=observe;p->observe_opaque=s;return 0;
}
static int unwatch(struct pbb_state *s){
    if(!s->observing)return 0;struct bw_source_smb *p=s->request.source;
    int bad=meter(s)||transport_limit(s);
    if(p->observe!=observe||p->observe_opaque!=s)fail(s,"bootstrap_observer_replaced");
    else{p->observe=s->previous;p->observe_opaque=s->previous_opaque;}
    if(s->latched&&p->deadline==-1)p->deadline=s->saved_deadline;
    s->observing=0;return bad||s->owner->failed?-1:0;
}
static int fence(struct pbb_state *s){if(snapshot(s)||guard(s))return -1;s->stats.fences++;if(s->request.fence(s->request.fence_opaque)||s->owner->failed)return fail(s,"bootstrap_whole_source_fence_refused");return snapshot(s)||guard(s)?-1:0;}
static int consume(void *opaque,const void *raw,size_t n){
    struct read_work *w=opaque;struct pbb_state *s=w->s;
    if(!n||n>65536||n>w->size-w->used||guard(s))return fail(s,"bootstrap_range_chunk_refused");
    if(s->stats.range_chunks==UINT32_MAX||n>UINT64_MAX-s->stats.range_bytes||(w->body&&n>UINT64_MAX-s->stats.body_bytes))return fail(s,"bootstrap_read_counter_overflow");
    s->stats.range_chunks++;s->stats.range_bytes+=(uint64_t)n;if(w->body)s->stats.body_bytes+=(uint64_t)n;
    cp_sha_update(&w->sha256,raw,n);pp_sha1_update(&w->sha1,raw,n);if(w->buffer)memcpy(w->buffer+(size_t)w->used,raw,n);w->used+=(uint64_t)n;
    if(w->consume&&w->consume(w->opaque,raw,n))return fail(s,"bootstrap_consumer_refused");return guard(s);
}
static int read_range(struct pbb_state *s,const struct bw_pbb_archive *a,uint64_t offset,uint64_t size,uint8_t *buffer,int body,bw_source_consume_fn user,void *opaque,uint8_t sha256[32],uint8_t sha1[20]){
    if(size>BW_SOURCE_RANGE_LIMIT||offset>a->info.size||size>a->info.size-offset||guard(s))return fail(s,"bootstrap_range_bounds");
    uint64_t used=s->stats.cache_owned_source_bytes;
    if(used>s->request.read_byte_limit-PBB_RESERVE||size>s->request.read_byte_limit-PBB_RESERVE-used)return fail(s,"bootstrap_cumulative_read_budget");
    if(s->stats.range_calls==UINT32_MAX)return fail(s,"bootstrap_range_call_limit");s->stats.range_calls++;
    struct read_work w={.s=s,.buffer=buffer,.size=size,.consume=user,.opaque=opaque,.body=body};cp_sha_init(&w.sha256);pp_sha1_init(&w.sha1);
    int bad=bw_source_smb_read_range(s->request.source,s->request.snapshot,a->path,offset,size,guard,s,consume,&w);
    if(bad||w.used!=size||guard(s))return fail(s,"bootstrap_range_or_current_refused");cp_sha_final(&w.sha256,sha256);pp_sha1_final(&w.sha1,sha1);return 0;
}
static uint8_t *span(struct pbb_state *s,struct bw_pbb_archive *a,enum bw_pbb_span_kind kind,uint64_t offset,uint64_t size){
    if(!size||size>BW_SOURCE_RANGE_LIMIT){fail(s,"bootstrap_metadata_span_limit");return NULL;}uint8_t *b=malloc((size_t)size);if(!b){fail(s,"bootstrap_allocation_refused");return NULL;}
    struct bw_pbb_span *p=&a->spans[kind];p->offset=offset;p->size=size;if(read_range(s,a,offset,size,b,0,NULL,NULL,p->sha256,p->sha1)){free(b);return NULL;}return b;
}
static const uint8_t *take(struct cursor *c,size_t n){if(n>c->size-c->at)return NULL;const uint8_t *p=c->p+c->at;c->at+=n;return p;}
static int number(struct cursor *c,unsigned width,uint64_t *out){const uint8_t *p=take(c,width);if(!p)return -1;*out=width==4?u32(p):u64(p);return 0;}
static int string(struct cursor *c,char out[4097]){
    uint64_t raw;if(number(c,4,&raw))return -1;int64_t n=(raw&UINT64_C(0x80000000))?(int64_t)raw-INT64_C(4294967296):(int64_t)raw;
    if(n<-4096||n>4096)return -1;if(!n){out[0]=0;return 0;}size_t count=(size_t)(n<0?-n:n),width=n<0?2u:1u;const uint8_t *p=take(c,count*width);if(!p)return -1;
    for(size_t i=0;i<count;i++){unsigned ch=p[i*width];if(width==2&&p[i*width+1])return -1;if(i==count-1){if(ch)return -1;out[i]=0;}else{if(ch<32||ch>=127)return -1;out[i]=(char)ch;}}return 0;
}
static char *pool_string(char *pool,size_t cap,size_t *used,const char *value){size_t n=strlen(value)+1;if(n>cap-*used)return NULL;char *p=pool+*used;memcpy(p,value,n);*used+=n;return p;}
static unsigned key_char(const struct key *k,size_t at){return at<k->dn?(unsigned char)k->directory[at]:at<k->dn+k->fn?(unsigned char)k->filename[at-k->dn]:0;}
static int key_compare(const void *aa,const void *bb){const struct key *a=aa,*b=bb;size_t at=0;for(;;at++){unsigned x=fold(key_char(a,at)),y=fold(key_char(b,at));if(x!=y)return x<y?-1:1;if(!x)return 0;}}
static int dir_compare(const void *aa,const void *bb){return folded(*(const char *const *)aa,*(const char *const *)bb);}
static int entry_compare(const void *aa,const void *bb){return folded(((const struct bw_pbb_entry *)aa)->destination,((const struct bw_pbb_entry *)bb)->destination);}
static int parent_conflict(struct pbb_state *s,const char *destination){
    char part[241];size_t n=strlen(destination);for(size_t i=0;i<=n;i++)if(destination[i]=='/'||!destination[i]){memcpy(part,destination,i);part[i]=0;const char *key=part;const char *const *found=bsearch(&key,s->original_names,s->original_count,sizeof *s->original_names,dir_compare);
        if(found){const struct bw_source_info *old=bw_source_snapshot_find(s->request.snapshot,*found);if(!old||i==n||!(old->attributes&0x10u))return fail(s,"bootstrap_existing_destination_or_file_ancestor");}}
    return 0;
}
/* Library directories are validated before any entry is created. Only mode3
 * substitutes existing directory case; modes4/5 preserve literal listed case.
 * The sealed exact source inventory has already rejected folded collisions.
 * No file leaf, source name, archive path or indexed location is rewritten. */
static int library_destination(struct pbb_state *s,struct pbb_table *t,const char *listed,char out[241]){
    size_t n=strlen(listed);if(n>BW_PBB_MAX_PATH||safe_path(listed,BW_PBB_MAX_PATH))return fail(s,"bootstrap_library_path");
    const char *slash=strchr(listed,'/');if(!slash||strncmp(slash+1,"Content/",sizeof("Content/")-1))return fail(s,"bootstrap_library_listed_content_case");
    size_t root=(size_t)(slash-listed);char project[241];memcpy(project,listed,root);project[root]=0;
    if(t->project_root[0]&&strcmp(t->project_root,project))return fail(s,"bootstrap_multiple_project_roots_or_alias");
    if(!t->project_root[0])copy(t->project_root,sizeof t->project_root,project);copy(out,241,listed);
    for(size_t i=0;i<n;i++)if(listed[i]=='/'){
        char part[241];memcpy(part,listed,i);part[i]=0;const char *key=part;
        const char *const *found=bsearch(&key,s->original_names,s->original_count,sizeof *s->original_names,dir_compare);
        if(found){
            size_t at=(size_t)(found-s->original_names);
            if((at&&!folded(found[-1],*found))||(at+1<s->original_count&&!folded(found[1],*found)))return fail(s,"bootstrap_library_directory_ambiguous");
            const struct bw_source_info *info=bw_source_snapshot_find(s->request.snapshot,*found);
            if(!info||!(info->attributes&0x10u)||(info->attributes&0x440u)||strlen(*found)!=i)return fail(s,"bootstrap_library_file_ancestor");
            if(bw_pbb_prefix_mode(s->prefix)==3)memcpy(out,*found,i);
        }
    }
    return guard(s);
}
static int entry(struct pbb_state *s,struct pbb_table *t,uint32_t archive_index,const char *destination,uint32_t location,const uint8_t *encoded,size_t encoded_size,uint64_t primary){
    if(t->count==BW_PBB_MAX_FILES||strlen(destination)>BW_PBB_MAX_PATH||safe_path(destination,BW_PBB_MAX_PATH)||parent_conflict(s,destination)||location>=encoded_size)return fail(s,"bootstrap_entry_path_count_or_location");
    struct bw_pbb_entry *out=&t->entries[t->count];struct bw_pbb_archive *a=&t->archives[archive_index];struct cursor c={encoded+location,encoded_size-location,0};uint64_t bits,offset,size,block;
    if(number(&c,4,&bits))return fail(s,"bootstrap_encoded_entry_eof");block=bits&63;if(block==63){if(number(&c,4,&block))return fail(s,"bootstrap_block_size_eof");}else block<<=11;
    if(number(&c,(bits&(UINT64_C(1)<<31))?4u:8u,&offset)||number(&c,(bits&(UINT64_C(1)<<30))?4u:8u,&size))return fail(s,"bootstrap_encoded_body_eof");
    if(((bits>>23)&63)||((bits>>22)&1)||((bits>>6)&65535)||block)return fail(s,"bootstrap_compressed_or_encrypted_selected_entry");
    if(size>BW_SOURCE_RANGE_LIMIT||offset>primary||53>primary-offset||size>primary-offset-53||size>BW_PBB_MAX_BOOTSTRAP-t->bytes||s->request.base_local_bytes>s->request.startup_budget||t->bytes>s->request.startup_budget-s->request.base_local_bytes||size>s->request.startup_budget-s->request.base_local_bytes-t->bytes)return fail(s,"bootstrap_body_or_startup_budget");
    for(uint32_t i=0;i<t->count;i++)if(t->entries[i].archive_index==archive_index){const struct bw_pbb_entry *old=&t->entries[i];if(old->entry_location==location||(offset<old->body_offset+old->size&&old->header_offset<offset+53+size))return fail(s,"bootstrap_selected_physical_alias");if((i&511u)==0&&guard(s))return -1;}
    (void)a;copy(out->destination,sizeof out->destination,destination);out->archive_index=archive_index;out->entry_location=location;out->flags=BW_PBB_LOCAL;out->header_offset=offset;out->body_offset=offset+53;out->size=size;
    t->bytes+=size;t->count++;return 0;
}
static int archive(struct pbb_state *s,struct pbb_table *table,uint32_t ai){
    struct bw_pbb_archive *a=&table->archives[ai];uint8_t *footer=NULL,*primary=NULL,*names=NULL,*hash=NULL;char *pool=NULL;struct key *keys=NULL;const char **directories=NULL;int result=-1;
    if(a->info.size<45){fail(s,"bootstrap_footer_size");goto done;}size_t footer_size=a->info.size<4096?(size_t)a->info.size:4096;
    footer=span(s,a,BW_PBB_FOOTER,a->info.size-footer_size,footer_size);if(!footer)goto done;
    uint64_t po=0,ps=0,magic=0;uint8_t expected_primary[20]={0};unsigned candidates=0,version=0,encrypted=0;
    for(size_t at=1;at<=footer_size-44;at++)if(u32(footer+at)==UINT32_C(0x5a6f12e1)){
        uint32_t v=u32(footer+at+4);uint64_t o=u64(footer+at+8),n=u64(footer+at+16),physical=a->info.size-footer_size+(uint64_t)at;
        if(v>=1&&v<=20&&!(o>>63)&&!(n>>63)&&n&&o<=physical&&n<=physical-o){candidates++;po=o;ps=n;magic=physical;version=v;encrypted=footer[at-1];memcpy(expected_primary,footer+at+24,20);}
    }
    if(candidates!=1||version!=11||encrypted||ps>BW_PBB_MAX_INDEX){fail(s,"bootstrap_unique_plain_v11_footer");goto done;}
    primary=span(s,a,BW_PBB_PRIMARY,po,ps);if(!primary)goto done;if(memcmp(a->spans[BW_PBB_PRIMARY].sha1,expected_primary,20)){fail(s,"bootstrap_primary_sha1");goto done;}
    struct cursor c={primary,(size_t)ps,0};char mount[4097],directory[4097],filename[4097];uint64_t count,seed,has,no=0,ns=0,ho=0,hs=0;uint8_t expected_names[20]={0},expected_hash[20]={0};unsigned hash_present=0;
    if(string(&c,mount)){fail(s,"bootstrap_mount_encoding");goto done;}size_t mn=strlen(mount);int empty_root=!strcmp(mount,"/");
    if(!empty_root){if(mn<9||memcmp(mount,"../../../",9)){fail(s,"bootstrap_mount_literal");goto done;}if(mn>9){if(mount[mn-1]!='/'){fail(s,"bootstrap_mount_directory");goto done;}mount[mn-1]=0;int bad=safe_path(mount+9,4096);mount[mn-1]='/';if(bad){fail(s,"bootstrap_mount_path");goto done;}}}
    if(number(&c,4,&count)||count>BW_PBB_MAX_ARCHIVE_ENTRIES||number(&c,8,&seed)){fail(s,"bootstrap_filename_count");goto done;}a->filename_count=(uint32_t)count;a->hash_seed=seed;
    for(unsigned i=0;i<2;i++){if(number(&c,4,&has)||has>1){fail(s,"bootstrap_index_presence");goto done;}if(has){uint64_t o,n;if(number(&c,8,&o)||number(&c,8,&n)){fail(s,"bootstrap_index_descriptor");goto done;}const uint8_t *sha=take(&c,20);if(!sha){fail(s,"bootstrap_index_hash_eof");goto done;}if(i){no=o;ns=n;memcpy(expected_names,sha,20);}else{hash_present=1;ho=o;hs=n;memcpy(expected_hash,sha,20);}}else if(i){fail(s,"bootstrap_filename_index_missing");goto done;}}
    uint64_t encoded_size,nonencoded;if(number(&c,4,&encoded_size)||encoded_size>c.size-c.at){fail(s,"bootstrap_encoded_table_size");goto done;}const uint8_t *encoded=take(&c,(size_t)encoded_size);
    if(number(&c,4,&nonencoded)||nonencoded||c.at!=c.size){fail(s,"bootstrap_nonencoded_or_primary_trailing");goto done;}
    if(!ns||ns>BW_PBB_MAX_INDEX||po>no||ps>no-po||no>magic||ns>magic-no){fail(s,"bootstrap_filename_span_order");goto done;}
    if(hash_present&&(!hs||hs>BW_PBB_MAX_INDEX||po>ho||ps>ho-po||ho>magic||hs>magic-ho||!(ho+hs<=no||no+ns<=ho))){fail(s,"bootstrap_path_hash_span_order");goto done;}
    int hash_zero=0;
    if(hash_present){hash=span(s,a,BW_PBB_PATH_HASH,ho,hs);if(!hash)goto done;if(memcmp(a->spans[BW_PBB_PATH_HASH].sha1,expected_hash,20)){fail(s,"bootstrap_path_hash_sha1");goto done;}hash_zero=hs==8&&!u64(hash);free(hash);hash=NULL;}
    names=span(s,a,BW_PBB_FILENAMES,no,ns);if(!names)goto done;if(memcmp(a->spans[BW_PBB_FILENAMES].sha1,expected_names,20)){fail(s,"bootstrap_filename_sha1");goto done;}
    struct cursor d={names,(size_t)ns,0};uint64_t dirs,total=0,walked=0;if(number(&d,4,&dirs)||dirs>BW_PBB_MAX_ARCHIVE_ENTRIES){fail(s,"bootstrap_directory_count");goto done;}a->directory_count=(uint32_t)dirs;
    if(!count){if(encoded_size||dirs||d.at!=d.size||(hash_present&&!hash_zero)){fail(s,"bootstrap_empty_archive_payload");goto done;}if(empty_root&&hash_present&&ho+hs>no){fail(s,"bootstrap_empty_root_hash_order");goto done;}result=0;goto done;}
    if(empty_root||!dirs){fail(s,"bootstrap_nonempty_root_or_directory");goto done;}
    keys=calloc((size_t)count,sizeof *keys);directories=calloc((size_t)dirs,sizeof *directories);pool=malloc((size_t)ns);size_t pool_used=0;
    if(!keys||!directories||!pool){fail(s,"bootstrap_index_allocation");goto done;}
    for(uint64_t di=0;di<dirs;di++){
        uint64_t files;if((di&511u)==0&&guard(s))goto done;if(string(&d,directory)){fail(s,"bootstrap_directory_encoding");goto done;}
        if(!strcmp(directory,"/"))directory[0]=0;size_t dn=strlen(directory);
        if(dn){if(directory[dn-1]!='/'){fail(s,"bootstrap_directory_termination");goto done;}directory[dn-1]=0;int bad=safe_path(directory,4096);directory[dn-1]='/';if(bad){fail(s,"bootstrap_directory_path");goto done;}}
        const char *dir=pool_string(pool,(size_t)ns,&pool_used,directory);if(!dir||number(&d,4,&files)||files>count-total){fail(s,"bootstrap_directory_file_count");goto done;}directories[di]=dir;total+=files;
        for(uint64_t fi=0;fi<files;fi++){
            uint64_t loc;if(string(&d,filename)||strchr(filename,'/')||safe_path(filename,4096)||number(&d,4,&loc)){fail(s,"bootstrap_filename_path_or_location");goto done;}size_t fn=strlen(filename);const char *file=pool_string(pool,(size_t)ns,&pool_used,filename);if(!file){fail(s,"bootstrap_filename_pool_bound");goto done;}
            keys[walked]=(struct key){dir,file,dn,fn};char candidate[3*4096+1];size_t relative=mn-9;
            if(relative>sizeof candidate-1||dn>sizeof candidate-1-relative||fn>sizeof candidate-1-relative-dn){fail(s,"bootstrap_combined_path_limit");goto done;}memcpy(candidate,mount+9,relative);memcpy(candidate+relative,dir,dn);memcpy(candidate+relative+dn,file,fn+1);
            int selected=bw_pbb_selector_match(s->prefix,candidate);
            if(selected<0){fail(s,"bootstrap_prefix_case_alias_or_shader_token");goto done;}
            if(selected){
                char mapped[241];const char *destination=candidate;
                if(bw_pbb_prefix_mode(s->prefix)>=3&&bw_pbb_selector_library_match(candidate)==1){if(library_destination(s,table,candidate,mapped))goto done;destination=mapped;}
                if(entry(s,table,ai,destination,(uint32_t)loc,encoded,(size_t)encoded_size,po))goto done;
            }
            walked++;if((walked&511u)==0&&guard(s))goto done;
        }
    }
    if(d.at!=d.size||total!=count||walked!=count){fail(s,"bootstrap_filename_count_or_trailing");goto done;}
    qsort(directories,(size_t)dirs,sizeof *directories,dir_compare);for(size_t i=1;i<(size_t)dirs;i++){
        const char *aa=directories[i-1],*bb=directories[i];size_t j=0,last_slash=0;while(aa[j]&&bb[j]&&fold((unsigned char)aa[j])==fold((unsigned char)bb[j])){if(aa[j]=='/')last_slash=j+1;j++;}
        if(!folded(aa,bb)||(last_slash&&memcmp(aa,bb,last_slash))){fail(s,"bootstrap_directory_alias");goto done;}
    }
    qsort(keys,(size_t)count,sizeof *keys,key_compare);for(size_t i=1;i<(size_t)count;i++){
        size_t n=keys[i-1].dn+keys[i-1].fn,j=0;while(j<n&&fold(key_char(&keys[i-1],j))==fold(key_char(&keys[i],j)))j++;
        if(j==n&&(!key_char(&keys[i],j)||key_char(&keys[i],j)=='/')){fail(s,"bootstrap_filename_alias_or_ancestor");goto done;}
    }
    if(guard(s))goto done;result=0;
done:
    free(footer);free(primary);free(names);free(hash);free(pool);free(keys);free(directories);return result;
}
static int table_init(struct pbb_table *t){t->archives=calloc(BW_PBB_MAX_ARCHIVES,sizeof *t->archives);t->entries=calloc(BW_PBB_MAX_FILES,sizeof *t->entries);return t->archives&&t->entries?0:-1;}
static void table_free(struct pbb_table *t){free(t->archives);free(t->entries);memset(t,0,sizeof *t);}
static int derive(struct pbb_state *s,struct pbb_table *t){
    for(uint32_t i=0;i<s->request.archive_count;i++){if(t!=&s->facts){copy(t->archives[i].path,sizeof t->archives[i].path,s->facts.archives[i].path);t->archives[i].info=s->facts.archives[i].info;}if(archive(s,t,i))return -1;}
    if(!t->count)return BW_PBB_NOT_APPLICABLE;qsort(t->entries,t->count,sizeof *t->entries,entry_compare);
    for(uint32_t i=1;i<t->count;i++){
        const char *a=t->entries[i-1].destination,*b=t->entries[i].destination;size_t n=strlen(a),j=0,last_slash=0;while(a[j]&&b[j]&&fold((unsigned char)a[j])==fold((unsigned char)b[j])){if(a[j]=='/')last_slash=j+1;j++;}
        if((j==n&&(!b[j]||b[j]=='/'))||(last_slash&&memcmp(a,b,last_slash)))return fail(s,"bootstrap_cross_archive_alias_or_ancestor");
    }
    return 0;
}
/* Mode5 accounts original metadata conservatively: the footer may overlap an
 * index, but each original span still needs its own hash and emission. */
static int metadata_budget(struct pbb_state *s){
    if(bw_pbb_prefix_mode(s->prefix)!=5)return 0;uint64_t total=0;
    for(uint32_t i=0;i<s->request.archive_count;i++)for(unsigned k=0;k<BW_PBB_SPANS;k++){
        const struct bw_pbb_archive *a=&s->facts.archives[i];const struct bw_pbb_span *p=&a->spans[k];
        if(!p->size)continue;
        if(p->size>BW_SOURCE_RANGE_LIMIT||p->offset>a->info.size||p->size>a->info.size-p->offset||
           p->size>BW_PBB_MAX_METADATA_SEED-total)return fail(s,"bootstrap_metadata_seed_budget");
        total+=p->size;
    }
    if(!total||s->request.base_local_bytes>s->request.startup_budget||
       s->facts.bytes>s->request.startup_budget-s->request.base_local_bytes||
       total>s->request.startup_budget-s->request.base_local_bytes-s->facts.bytes)return fail(s,"bootstrap_metadata_startup_budget");
    s->metadata_size=total;return 0;
}
static int metadata_complete(struct pbb_state *s){
    if(bw_pbb_prefix_mode(s->prefix)!=5)return 0;
    if(!s->metadata_size||s->metadata_emitted!=s->metadata_size)return fail(s,"bootstrap_metadata_emission_incomplete");
    for(uint32_t i=0;i<s->request.archive_count;i++){
        unsigned expected=0;for(unsigned k=0;k<BW_PBB_SPANS;k++)if(s->facts.archives[i].spans[k].size)expected|=1u<<k;
        if(s->metadata_delivered[i]!=expected)return fail(s,"bootstrap_metadata_emission_incomplete");
    }return 0;
}
struct physical {uint32_t id;uint64_t begin,end;};
struct data_work {
    struct pbb_state *s;struct pbb_table *table;const struct physical *order;uint32_t count,which;
    uint64_t position;int active,capture;uint8_t header[53];struct cp_sha sha256;struct pp_sha1 sha1;
};
static int physical_compare(const void *aa,const void *bb){const struct physical *a=aa,*b=bb;return a->begin<b->begin?-1:a->begin>b->begin?1:0;}
static int body_work(struct pbb_state *s,size_t bytes){if(bytes>BW_PBB_MAX_BODY_WORK-s->stats.body_bytes)return fail(s,"bootstrap_body_work_limit");s->stats.body_bytes+=(uint64_t)bytes;return 0;}
static int data_consume(void *opaque,const void *raw,size_t n){
    struct data_work *w=opaque;const uint8_t *p=raw;
    while(n){
        if(w->which>=w->count)return fail(w->s,"bootstrap_coalesced_extra_bytes");uint32_t id=w->order[w->which].id;struct bw_pbb_entry *e=&w->table->entries[id];uint64_t end=e->body_offset+e->size;
        if(!w->active){if(w->position!=e->header_offset)return fail(w->s,"bootstrap_coalesced_gap");memset(w->header,0,sizeof w->header);cp_sha_init(&w->sha256);pp_sha1_init(&w->sha1);w->active=1;}
        if(w->position<e->header_offset||w->position>=end)return fail(w->s,"bootstrap_coalesced_entry_bounds");
        size_t amount=n;
        if(w->position<e->body_offset){
            if((uint64_t)amount>e->body_offset-w->position)amount=(size_t)(e->body_offset-w->position);memcpy(w->header+(size_t)(w->position-e->header_offset),p,amount);
            if(w->position+(uint64_t)amount==e->body_offset){
                if(u64(w->header)||u64(w->header+8)!=e->size||u64(w->header+16)!=e->size||u32(w->header+24)||w->header[48]||u32(w->header+49))return fail(w->s,"bootstrap_plain_header_crosslink");
                cp_sha_bytes(w->header,sizeof w->header,e->header_sha256);memcpy(e->body_sha1,w->header+28,20);
                if(!w->capture&&(memcmp(e->header_sha256,w->s->facts.entries[id].header_sha256,32)||memcmp(e->body_sha1,w->s->facts.entries[id].body_sha1,20)))return fail(w->s,"bootstrap_final_header_changed");
            }
        }else{
            if((uint64_t)amount>end-w->position)amount=(size_t)(end-w->position);
            uint64_t relative=w->position-e->body_offset,base=w->s->cache_offsets[id];
            if(base>w->s->cache_size||e->size>w->s->cache_size-base||relative>e->size||(uint64_t)amount>e->size-relative||body_work(w->s,amount))return fail(w->s,"bootstrap_cache_bounds");
            uint8_t *cached=w->s->cache+(size_t)(base+relative);
            if(w->capture)memcpy(cached,p,amount);else if(memcmp(cached,p,amount))return fail(w->s,"bootstrap_final_body_changed");
            cp_sha_update(&w->sha256,p,amount);pp_sha1_update(&w->sha1,p,amount);
        }
        w->position+=(uint64_t)amount;p+=amount;n-=amount;
        if(w->position==end){uint8_t sha1[20];cp_sha_final(&w->sha256,e->body_sha256);pp_sha1_final(&w->sha1,sha1);
            if(memcmp(sha1,e->body_sha1,20)||(!w->capture&&memcmp(e->body_sha256,w->s->facts.entries[id].body_sha256,32)))return fail(w->s,"bootstrap_body_sha1_or_capture_mismatch");
            w->active=0;w->which++;
        }
    }return guard(w->s);
}
static int verify_data(struct pbb_state *s,struct pbb_table *table,int capture){
    struct physical *order=calloc(table->count,sizeof *order);if(!order)return fail(s,"bootstrap_range_plan_allocation");int result=-1;uint32_t before=s->stats.range_calls;
    for(uint32_t ai=0;ai<s->request.archive_count;ai++){
        uint32_t count=0;for(uint32_t i=0;i<table->count;i++)if(table->entries[i].archive_index==ai){const struct bw_pbb_entry *e=&table->entries[i];order[count++]=(struct physical){i,e->header_offset,e->body_offset+e->size};}
        qsort(order,count,sizeof *order,physical_compare);struct data_work w={.s=s,.table=table,.order=order,.count=count,.capture=capture};
        for(uint32_t first=0;first<count;){uint32_t last=first+1;uint64_t end=order[first].end;
            while(last<count&&order[last].begin==end){end=order[last].end;last++;}
            if(first&&order[first].begin<order[first-1].end){fail(s,"bootstrap_selected_range_overlap");goto done;}
            w.position=order[first].begin;
            for(uint64_t at=w.position;at<end;){uint64_t size=end-at;if(size>BW_SOURCE_RANGE_LIMIT)size=BW_SOURCE_RANGE_LIMIT;uint8_t sha256[32],sha1[20];
                if(w.position!=at||read_range(s,&table->archives[ai],at,size,NULL,0,data_consume,&w,sha256,sha1))goto done;at+=size;
            }
            if(w.which!=last||w.active){fail(s,"bootstrap_coalesced_incomplete_entry");goto done;}first=last;
        }
        if(w.which!=count||guard(s)){fail(s,"bootstrap_coalesced_incomplete_archive");goto done;}
    }
    result=0;
done:
    if(capture)s->stats.capture_range_calls+=s->stats.range_calls-before;else s->stats.final_range_calls+=s->stats.range_calls-before;free(order);return result;
}
static int inventory(struct pbb_state *s){
    const struct bw_source_snapshot *p=s->request.snapshot;size_t at=0,rows=0;
    s->original_names=calloc(p->count,sizeof *s->original_names);if(!s->original_names)return fail(s,"bootstrap_inventory_allocation");
    while(at<p->path_size){const char *name=p->paths+at,*end=memchr(name,0,p->path_size-at);if(!end)return fail(s,"bootstrap_inventory_storage");size_t n=(size_t)(end-name);at+=n+1;rows++;
        if(n){s->original_names[s->original_count++]=name;if(safe_path(name,BW_PBB_MAX_PATH))return fail(s,"bootstrap_inventory_path_policy");for(size_t i=0;i<n;i++)if(name[i]=='/'){char part[241];memcpy(part,name,i);part[i]=0;const struct bw_source_info *info=bw_source_snapshot_find(p,part);if(!info||!(info->attributes&0x10u))return fail(s,"bootstrap_inventory_parent");}}
        if((rows&511u)==0&&guard(s))return -1;
    }if(at!=p->path_size||rows!=p->count)return fail(s,"bootstrap_inventory_coverage");qsort(s->original_names,s->original_count,sizeof *s->original_names,dir_compare);return 0;
}
static int request(struct pbb_state *s,const struct bw_pbb_request *r){
    if(!r||r->version!=BW_PBB_VERSION||!r->epoch||!r->source||!r->source->smb||r->source->error||!r->snapshot||!r->current||!r->fence||r->source_kind!=1||r->source_policy!=1||!r->profile||!memchr(r->profile,0,5)||strcmp(r->profile,"none")||!r->prefix||!memchr(r->prefix,0,sizeof BW_PBB_PREFIX)||!bw_pbb_prefix_mode(r->prefix)||!r->archives||!r->archive_count||r->archive_count>BW_PBB_MAX_ARCHIVES||r->startup_budget>BW_PBB_MAX_STARTUP||r->base_local_bytes>r->startup_budget||r->read_byte_limit<=PBB_RESERVE||r->read_byte_limit>BW_PBB_MAX_READ_BYTES||r->request_limit<2||r->request_limit>BW_PBB_MAX_REQUESTS||r->source->requests>UINT32_MAX-BW_PBB_MAX_REQUESTS-8192u)return fail(s,"bootstrap_request_contract");
    if(!(r->source->deadline>=-DBL_MAX&&r->source->deadline<=DBL_MAX)||r->snapshot->count>BW_PBB_MAX_FILES+1u||bw_source_snapshot_validate(r->snapshot)||!r->snapshot->parent_count||copy(s->source,sizeof s->source,r->snapshot->source)||copy(s->game,sizeof s->game,r->snapshot->game))return fail(s,"bootstrap_owned_snapshot_or_deadline");
    s->request=*r;s->request.profile="none";copy(s->prefix,sizeof s->prefix,r->prefix);s->request.prefix=s->prefix;s->request.archives=NULL;s->context=r->source->smb;s->deadline=r->source->deadline;s->request_start=s->request_last=r->source->requests;s->byte_start=s->byte_last=r->source->content_bytes;memcpy(s->seal,r->snapshot->sha256,32);
    for(uint32_t i=0;i<r->archive_count;i++){
        struct bw_pbb_archive *a=&s->facts.archives[i];if(copy(a->path,sizeof a->path,r->archives[i])||safe_path(a->path,BW_PBB_MAX_PATH)||(i&&strcmp(s->facts.archives[i-1].path,a->path)>=0))return fail(s,"bootstrap_literal_sorted_archive_paths");
        for(uint32_t j=0;j<i;j++)if(!folded(a->path,s->facts.archives[j].path))return fail(s,"bootstrap_archive_alias");
        const struct bw_source_info *info=bw_source_snapshot_find(r->snapshot,a->path);if(!info||(info->attributes&(0x10u|0x40u|0x400u)))return fail(s,"bootstrap_original_regular_archive_required");a->info=*info;
    }return guard(s);
}
static int view_current(struct pbb_state *s){
    if(s->view.version!=BW_PBB_VERSION||s->view.epoch!=s->request.epoch||s->cache_size!=s->facts.bytes||s->view.archives!=s->public_archives||s->view.entries!=s->public_entries||s->view.archive_count!=s->request.archive_count||s->view.entry_count!=s->facts.count||s->view.bootstrap_bytes!=s->facts.bytes||s->view.base_local_bytes!=s->request.base_local_bytes||memcmp(s->view.native_snapshot_sha256,s->seal,32)||memcmp(s->view.source,s->source,sizeof s->source)||memcmp(s->view.game,s->game,sizeof s->game)||memcmp(s->view.prefix,s->prefix,sizeof s->prefix))return fail(s,"bootstrap_public_view_changed");return snapshot(s)||guard(s)?-1:0;
}
int bw_packed_bootstrap_open(struct bw_packed_bootstrap *p,const struct bw_pbb_request *r){
    if(!p)return -1;if(p->busy){p->failed=1;p->error="bootstrap_callback_reentry";return -1;}if(p->state||p->failed){p->failed=1;p->error="bootstrap_new_zeroed_session_required";return -1;}p->busy=1;int bad=-1;struct pbb_state *s=calloc(1,sizeof *s);
    if(!s){p->failed=1;p->error="bootstrap_allocation_refused";goto done;}p->state=s;s->owner=p;
    if(table_init(&s->facts)||request(s,r)){if(!p->error)fail(s,"bootstrap_allocation_refused");goto done;}
    if(watch(s)||inventory(s)||fence(s))goto done;
    int derived=derive(s,&s->facts);if(derived<0)goto done;
    if(derived==BW_PBB_NOT_APPLICABLE){if(fence(s))goto done;p->error="bootstrap_not_applicable";bad=BW_PBB_NOT_APPLICABLE;goto done;}
    if(metadata_budget(s))goto done;
    s->cache_size=s->facts.bytes;s->cache=malloc(s->cache_size?(size_t)s->cache_size:1);if(!s->cache){fail(s,"bootstrap_cache_allocation");goto done;}
    uint64_t at=0;for(uint32_t i=0;i<s->facts.count;i++){s->cache_offsets[i]=at;if(s->facts.entries[i].size>s->cache_size-at){fail(s,"bootstrap_cache_offsets");goto done;}at+=s->facts.entries[i].size;}
    if(at!=s->cache_size||verify_data(s,&s->facts,1)||fence(s))goto done;
    s->public_archives=calloc(s->request.archive_count,sizeof *s->public_archives);s->public_entries=calloc(s->facts.count,sizeof *s->public_entries);
    if(!s->public_archives||!s->public_entries){fail(s,"bootstrap_view_allocation");goto done;}
    memcpy(s->public_archives,s->facts.archives,s->request.archive_count*sizeof *s->public_archives);memcpy(s->public_entries,s->facts.entries,s->facts.count*sizeof *s->public_entries);
    copy(s->view.source,sizeof s->view.source,s->source);copy(s->view.game,sizeof s->view.game,s->game);memcpy(s->view.prefix,s->prefix,sizeof s->prefix);memcpy(s->view.native_snapshot_sha256,s->seal,32);
    s->view.version=BW_PBB_VERSION;s->view.epoch=s->request.epoch;s->view.archives=s->public_archives;s->view.entries=s->public_entries;s->view.archive_count=s->request.archive_count;s->view.entry_count=s->facts.count;s->view.bootstrap_bytes=s->facts.bytes;s->view.base_local_bytes=s->request.base_local_bytes;bad=0;
done:
    if(s&&unwatch(s))bad=-1;if(bad)p->failed=1;p->busy=0;return bad;
}
const struct bw_pbb_view *bw_packed_bootstrap_view(struct bw_packed_bootstrap *p){
    if(!p||!p->state||p->failed)return NULL;struct pbb_state *s=p->state;if(p->busy){fail(s,"bootstrap_callback_reentry");return NULL;}p->busy=1;int bad=view_current(s);
    if(!bad&&(memcmp(s->public_archives,s->facts.archives,s->request.archive_count*sizeof *s->public_archives)||memcmp(s->public_entries,s->facts.entries,s->facts.count*sizeof *s->public_entries)))bad=fail(s,"bootstrap_public_facts_changed");p->busy=0;return bad?NULL:&s->view;
}
int bw_packed_bootstrap_read(struct bw_packed_bootstrap *p,uint32_t id,bw_source_consume_fn user,void *opaque){
    if(!p||!p->state)return -1;struct pbb_state *s=p->state;if(p->busy)return fail(s,"bootstrap_callback_reentry");if(p->failed)return -1;p->busy=1;int bad=-1;uint8_t *scratch=NULL;
    if(!user||id>=s->facts.count||s->delivered[id]||s->stats.finished||view_current(s))goto done;
    const struct bw_pbb_entry *e=&s->facts.entries[id];const struct bw_pbb_archive *a=&s->facts.archives[e->archive_index];uint8_t sha256[32],sha1[20];
    if(memcmp(&s->public_entries[id],e,sizeof *e)||memcmp(&s->public_archives[e->archive_index],a,sizeof *a)){fail(s,"bootstrap_public_facts_changed");goto done;}
    uint64_t base=s->cache_offsets[id];if(base>s->cache_size||e->size>s->cache_size-base){fail(s,"bootstrap_cache_bounds");goto done;}s->delivered[id]=1;
    if(e->size){scratch=malloc(65536);if(!scratch){fail(s,"bootstrap_scratch_allocation");goto done;}}
    struct cp_sha hash256;struct pp_sha1 hash1;cp_sha_init(&hash256);pp_sha1_init(&hash1);
    for(uint64_t at=0;at<e->size;){size_t n=e->size-at<65536?(size_t)(e->size-at):65536;if(guard(s)||body_work(s,n))goto done;
        if(s->stats.emitted_chunks==UINT32_MAX||n>BW_PBB_MAX_BOOTSTRAP-s->stats.emitted_bytes){fail(s,"bootstrap_emit_counter_limit");goto done;}
        memcpy(scratch,s->cache+(size_t)(base+at),n);cp_sha_update(&hash256,scratch,n);pp_sha1_update(&hash1,scratch,n);s->stats.emitted_chunks++;s->stats.emitted_bytes+=(uint64_t)n;
        if(user(opaque,scratch,n)||guard(s)||memcmp(scratch,s->cache+(size_t)(base+at),n)){fail(s,"bootstrap_memory_consumer_refused");goto done;}at+=(uint64_t)n;
    }
    cp_sha_final(&hash256,sha256);pp_sha1_final(&hash1,sha1);
    if(memcmp(sha1,e->body_sha1,20)||memcmp(sha256,e->body_sha256,32)||view_current(s)||memcmp(&s->public_entries[id],e,sizeof *e)){fail(s,"bootstrap_emitted_body_or_facts_changed");goto done;}
    s->stats.delivered_files++;bad=0;
done:
    free(scratch);if(bad)fail(s,"bootstrap_read_refused");p->busy=0;return bad;
}
struct metadata_work {
    struct pbb_state *s;uint32_t archive_index;enum bw_pbb_span_kind kind;
    struct bw_pbb_archive archive;uint8_t *scratch;uint64_t used;
    bw_pbb_metadata_consume_fn consume;void *opaque;
};
static int metadata_consume(void *opaque,const void *raw,size_t n){
    struct metadata_work *w=opaque;const struct bw_pbb_archive *a=&w->s->facts.archives[w->archive_index];
    const struct bw_pbb_span *p=&a->spans[w->kind];
    if(!raw||!n||n>65536||w->used>p->size||n>p->size-w->used||memcmp(&w->archive,a,sizeof *a))return fail(w->s,"bootstrap_metadata_chunk_bounds");
    memcpy(w->scratch,raw,n);
    if(w->consume(w->opaque,w->archive_index,w->kind,&w->archive,p->offset+w->used,w->scratch,n)||
       memcmp(&w->archive,a,sizeof *a)||memcmp(w->scratch,raw,n))return fail(w->s,"bootstrap_metadata_consumer_refused");
    w->used+=(uint64_t)n;return 0;
}
int bw_packed_bootstrap_read_metadata(struct bw_packed_bootstrap *p,uint32_t ai,enum bw_pbb_span_kind kind,bw_pbb_metadata_consume_fn user,void *opaque){
    if(!p||!p->state)return -1;struct pbb_state *s=p->state;if(p->busy)return fail(s,"bootstrap_callback_reentry");if(p->failed)return -1;
    p->busy=1;int bad=-1;struct metadata_work w={.s=s,.archive_index=ai,.kind=kind,.consume=user,.opaque=opaque};
    if(!user||bw_pbb_prefix_mode(s->prefix)!=5||ai>=s->request.archive_count||(unsigned)kind>=BW_PBB_SPANS||s->stats.finished||view_current(s))goto done;
    const struct bw_pbb_archive *a=&s->facts.archives[ai];const struct bw_pbb_span *span_fact=&a->spans[kind];uint8_t sha256[32],sha1[20];
    if(!span_fact->size||s->metadata_delivered[ai]&(1u<<(unsigned)kind)||
       memcmp(&s->public_archives[ai],a,sizeof *a)||s->metadata_emitted>s->metadata_size||span_fact->size>s->metadata_size-s->metadata_emitted)goto done;
    memcpy(&w.archive,a,sizeof w.archive);w.scratch=malloc(65536);if(!w.scratch){fail(s,"bootstrap_metadata_scratch_allocation");goto done;}
    if(watch(s)||read_range(s,a,span_fact->offset,span_fact->size,NULL,0,metadata_consume,&w,sha256,sha1)||
       w.used!=span_fact->size||memcmp(sha256,span_fact->sha256,32)||memcmp(sha1,span_fact->sha1,20)||
       view_current(s)||memcmp(&s->public_archives[ai],a,sizeof *a))goto done;
    s->metadata_delivered[ai]|=(uint8_t)(1u<<(unsigned)kind);s->metadata_emitted+=span_fact->size;bad=0;
done:
    free(w.scratch);if(unwatch(s))bad=-1;if(bad)fail(s,"bootstrap_metadata_read_refused");p->busy=0;return bad;
}
int bw_packed_bootstrap_finish(struct bw_packed_bootstrap *p){
    if(!p||!p->state)return -1;struct pbb_state *s=p->state;if(p->busy)return fail(s,"bootstrap_callback_reentry");if(p->failed)return -1;p->busy=1;int bad=-1;struct pbb_table next={0};
    if(s->stats.finished||s->stats.delivered_files!=s->facts.count||metadata_complete(s)||view_current(s)||watch(s)||fence(s)||table_init(&next)||derive(s,&next))goto done;
    if(next.count!=s->facts.count||next.bytes!=s->facts.bytes||memcmp(next.archives,s->facts.archives,s->request.archive_count*sizeof *next.archives)){fail(s,"bootstrap_final_archive_or_count_changed");goto done;}
    for(uint32_t i=0;i<next.count;i++)if(memcmp(&next.entries[i],&s->facts.entries[i],offsetof(struct bw_pbb_entry,header_sha256))){fail(s,"bootstrap_final_entry_changed");goto done;}
    if(verify_data(s,&next,0))goto done;for(uint32_t i=0;i<next.count;i++)if(memcmp(&next.entries[i],&s->facts.entries[i],sizeof next.entries[i])){fail(s,"bootstrap_final_entry_changed");goto done;}
    if(fence(s))goto done;s->stats.finished=1;bad=0;
done:
    table_free(&next);if(unwatch(s))bad=-1;if(bad)fail(s,"bootstrap_final_refused");p->busy=0;return bad;
}
int bw_packed_bootstrap_stats(struct bw_packed_bootstrap *p,struct bw_pbb_stats *out){
    if(out)memset(out,0,sizeof *out);if(!p||!p->state||!out)return -1;struct pbb_state *s=p->state;if(p->busy)return fail(s,"bootstrap_callback_reentry");int bad=meter(s);*out=s->stats;return bad;
}
int bw_packed_bootstrap_destroy(struct bw_packed_bootstrap *p){
    if(!p)return -1;if(p->busy){p->failed=1;p->error="bootstrap_callback_reentry";return -1;}struct pbb_state *s=p->state;if(s){unwatch(s);table_free(&s->facts);free(s->public_archives);free(s->public_entries);free(s->original_names);free(s->cache);memset(s,0,sizeof *s);free(s);}memset(p,0,sizeof *p);return 0;
}
