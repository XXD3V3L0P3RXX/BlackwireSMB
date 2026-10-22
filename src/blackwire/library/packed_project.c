#include "packed_project.h"
#include "packed_project_json.h"
#include "packed_sha1.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <float.h>
struct pp_state {
    struct bw_pp_request request;
    char archive[241],requested[250],listed[250],destination[241],source[BW_MANIFEST_MAX_PATH+1],game[241];
    struct bw_source_info info;uint8_t seal[32];
    struct bw_pp_plan plan;
};
_Static_assert(sizeof(struct pp_state)<=8192,"packed state bound");
_Static_assert(sizeof(struct bw_pp_plan)<=8192,"packed plan bound");
struct pp_work {struct bw_packed_project *owner;struct pp_state *state;struct bw_pp_plan *plan;size_t span_used,span_size;uint8_t *span_bytes;};
struct cursor {const uint8_t *bytes;size_t size,at;};
static int fail(struct bw_packed_project *p,const char *error){p->failed=1;p->error=error;if(p->state)memset(&((struct pp_state *)p->state)->plan,0,sizeof(struct bw_pp_plan));return -1;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static const uint8_t *take(struct cursor *c,size_t n){if(n>c->size-c->at)return NULL;const uint8_t *p=c->bytes+c->at;c->at+=n;return p;}
static int number(struct cursor *c,unsigned n,uint64_t *v){const uint8_t *p=take(c,n);if(!p)return -1;*v=n==4?u32(p):u64(p);return 0;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int folded(const char *a,const char *b){while(*a&&*b){if(fold((unsigned char)*a++)!=fold((unsigned char)*b++))return 0;}return *a==*b;}
static int copy(char *out,size_t cap,const char *value){if(!value)return -1;const char *end=memchr(value,0,cap);if(!end)return -1;memcpy(out,value,(size_t)(end-value)+1);return 0;}
static int path(const char *p){
    size_t n=strlen(p);if(!n||n>240)return -1;size_t begin=0;
    for(size_t i=0;i<=n;i++){
        unsigned c=(unsigned char)p[i];if(i<n&&(c<32||c>=127||strchr("\\:*?\"<>|~%",(int)c)))return -1;
        if(c=='/'||!c){size_t z=i-begin;if(!z||(z==1&&p[begin]=='.')||(z==2&&p[begin]=='.'&&p[begin+1]=='.')||p[i-1]==' '||p[i-1]=='.')return -1;
            size_t stem=0;while(stem<z&&p[begin+stem]!='.')stem++;char upper[5]={0};if(stem<=4){for(size_t k=0;k<stem;k++)upper[k]=(char)fold((unsigned char)p[begin+k]);
                if(!strcmp(upper,"con")||!strcmp(upper,"prn")||!strcmp(upper,"aux")||!strcmp(upper,"nul")||(stem==4&&(!memcmp(upper,"com",3)||!memcmp(upper,"lpt",3))&&upper[3]>='1'&&upper[3]<='9'))return -1;}
            begin=i+1;
        }
    }return 0;
}
static int ends(const char *p,const char *suffix){size_t a=strlen(p),b=strlen(suffix);return a>=b&&folded(p+a-b,suffix);}
static int same_info(const struct bw_source_info *a,const struct bw_source_info *b){return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;}
static int sealed(struct pp_work *w){
    struct pp_state *s=w->state;const struct bw_source_snapshot *snapshot=s->request.snapshot;
    if(bw_source_snapshot_validate(snapshot)||!snapshot->parent_count||memcmp(snapshot->sha256,s->seal,32)||strcmp(snapshot->game,s->game)||strcmp(snapshot->source,s->source))return fail(w->owner,"packed_native_snapshot_changed");
    const struct bw_source_info *info=bw_source_snapshot_find(snapshot,s->archive);if(!info||!same_info(info,&s->info))return fail(w->owner,"packed_native_archive_changed");return 0;
}
static int timely(struct pp_work *w){struct timespec now;double deadline=w->state->request.source->deadline;if(!(deadline>=-DBL_MAX&&deadline<=DBL_MAX)||clock_gettime(CLOCK_MONOTONIC,&now)||deadline<=(double)now.tv_sec+(double)now.tv_nsec/1e9)return fail(w->owner,"packed_deadline_refused");return 0;}
static int guard(void *opaque){
    struct pp_work *w=opaque;if(w->owner->failed||timely(w))return -1;if(w->plan->current_checks==UINT32_MAX)return fail(w->owner,"packed_current_counter_limit");w->plan->current_checks++;
    int bad=w->state->request.current(w->state->request.job);
    if(bad||w->owner->failed)return fail(w->owner,"packed_current_job_refused");if(timely(w))return -1;
    if(w->plan->read_calls>=BW_PP_MAX_READ_CALLS&&w->span_used<w->span_size)return fail(w->owner,"packed_read_call_limit");return 0;
}
static int consume(void *opaque,const void *raw,size_t n){
    struct pp_work *w=opaque;if(w->owner->failed||!n||n>65536||n>w->span_size-w->span_used||w->plan->read_calls>=BW_PP_MAX_READ_CALLS)return fail(w->owner,"packed_read_chunk_limit");
    memcpy(w->span_bytes+w->span_used,raw,n);w->span_used+=n;w->plan->read_calls++;w->plan->read_bytes+=(uint64_t)n;return 0;
}
static uint8_t *read_span(struct pp_work *w,uint64_t offset,uint64_t size){
    if(!size||size>BW_PP_MAX_READ_BYTES||offset>w->state->info.size||size>w->state->info.size-offset||
       size>BW_PP_MAX_READ_BYTES-w->plan->read_bytes||w->plan->read_calls>=BW_PP_MAX_READ_CALLS){fail(w->owner,"packed_span_or_budget_limit");return NULL;}
    if(sealed(w)||guard(w))return NULL;uint8_t *bytes=malloc((size_t)size);if(!bytes){fail(w->owner,"packed_allocation_refused");return NULL;}
    w->span_used=0;w->span_size=(size_t)size;w->span_bytes=bytes;
    int bad=bw_source_smb_read_range(w->state->request.source,w->state->request.snapshot,w->state->archive,offset,size,guard,w,consume,w);
    w->span_bytes=NULL;if(bad||w->span_used!=(size_t)size||sealed(w)||guard(w)){free(bytes);fail(w->owner,"packed_range_or_current_refused");return NULL;}
    return bytes;
}
static uint8_t *span(struct pp_work *w,enum bw_pp_span_kind kind,uint64_t offset,uint64_t size){
    uint8_t *bytes=read_span(w,offset,size);if(!bytes)return NULL;
    struct bw_pp_span *out=&w->plan->spans[kind];out->offset=offset;out->size=size;cp_sha_bytes(bytes,(size_t)size,out->sha256);
    if(kind==BW_PP_PRIMARY||kind==BW_PP_NAMES||kind==BW_PP_BODY)pp_sha1_bytes(bytes,(size_t)size,out->sha1);
    return bytes;
}
/* Deliberately stricter native policy: printable ASCII Pak strings only, in
 * either valid positive UTF-8 or negative UTF-16LE framing. Refusing all other
 * names prevents Unicode lower/case aliases (e.g. Kelvin sign) being missed. */
static int pak_string(struct cursor *c,char out[4097]){
    uint64_t raw;if(number(c,4,&raw))return -1;int64_t n=(raw&UINT64_C(0x80000000))?(int64_t)raw-INT64_C(4294967296):(int64_t)raw;
    if(n<-4096||n>4096)return -1;if(!n){out[0]=0;return 0;}size_t count=(size_t)(n<0?-n:n),width=n<0?2u:1u;const uint8_t *bytes=take(c,count*width);if(!bytes)return -1;
    for(size_t i=0;i<count;i++){unsigned ch=bytes[i*width];if(width==2&&bytes[i*width+1])return -1;if(i==count-1){if(ch)return -1;out[i]=0;}else{if(ch<32||ch>=127)return -1;out[i]=(char)ch;}}
    return 0;
}
/* Literal directory mount only; no stripping, case folding or path alias. */
static int mount_path(char *mount){
    size_t n=strlen(mount);if(n<9||memcmp(mount,"../../../",9))return -1;if(n==9)return 0;
    if(mount[n-1]!='/')return -1;mount[n-1]=0;int bad=path(mount+9);mount[n-1]='/';return bad;
}
static int inventory(struct pp_work *w){
    const struct bw_source_snapshot *s=w->state->request.snapshot;size_t at=0,rows=0;unsigned nonroot=0;
    while(at<s->path_size){const char *name=s->paths+at,*end=memchr(name,0,s->path_size-at);if(!end)return fail(w->owner,"packed_inventory_storage");size_t length=(size_t)(end-name);at+=length+1;rows++;
        const struct bw_source_info *info=bw_source_snapshot_find(s,name);if(!info)return fail(w->owner,"packed_inventory_metadata");if(!length)continue;
        if(++nonroot>BW_PP_MAX_INVENTORY||path(name))return fail(w->owner,"packed_inventory_policy_limit");
        for(size_t i=0;i<length;i++)if(name[i]=='/'){char ancestor[241];memcpy(ancestor,name,i);ancestor[i]=0;const struct bw_source_info *parent=bw_source_snapshot_find(s,ancestor);if(!parent||!(parent->attributes&0x10u))return fail(w->owner,"packed_inventory_parent_conflict");}
        const char *destination=w->state->destination;size_t dn=strlen(destination);
        for(size_t i=0;i<=dn;i++)if(destination[i]=='/'||!destination[i]){char part[241];memcpy(part,destination,i);part[i]=0;
            /* A new literal directory can coexist with a differently cased
             * original directory on the prepared case-sensitive filesystem.
             * Target leaves and regular-file ancestors still refuse. */
            if(folded(name,part)&&!(i<dn&&(info->attributes&0x10u)))return fail(w->owner,"packed_destination_inventory_collision");}
        if((nonroot&511u)==0&&guard(w))return -1;
    }
    return at==s->path_size&&rows==s->count?0:fail(w->owner,"packed_inventory_coverage");
}
static int derive(struct pp_work *w){
    struct pp_state *s=w->state;struct bw_pp_plan *p=w->plan;uint8_t *footer=NULL,*primary=NULL,*names=NULL,*header=NULL,*body=NULL,*empty_hash=NULL;int result=-1;
    memset(p,0,sizeof *p);copy(p->archive,sizeof p->archive,s->archive);copy(p->requested,sizeof p->requested,s->requested);copy(p->listed,sizeof p->listed,s->listed);copy(p->proposed_destination,sizeof p->proposed_destination,s->destination);
    copy(p->source,sizeof p->source,s->source);copy(p->game,sizeof p->game,s->game);memcpy(p->native_snapshot_sha256,s->seal,32);p->archive_info=s->info;
    if(sealed(w)||guard(w)||inventory(w))goto done;
    if(s->info.size<45){fail(w->owner,"packed_footer_size");goto done;}
    size_t footer_size=s->info.size<4096?(size_t)s->info.size:4096;
    footer=span(w,BW_PP_FOOTER,s->info.size-footer_size,footer_size);if(!footer)goto done;
    uint64_t primary_offset=0,primary_size=0,magic_offset=0;uint8_t primary_sha1[20]={0};unsigned candidates=0;
    for(size_t at=0;at<=footer_size-44;at++)if(u32(footer+at)==UINT32_C(0x5a6f12e1)){
        uint64_t off=u64(footer+at+8),size=u64(footer+at+16),physical=s->info.size-footer_size+(uint64_t)at;
        if(u32(footer+at+4)==11&&at&&footer[at-1]==0&&!(off>>63)&&!(size>>63)&&size&&off<=physical&&size<=physical-off){candidates++;primary_offset=off;primary_size=size;magic_offset=physical;memcpy(primary_sha1,footer+at+24,20);}
    }
    if(candidates!=1||!primary_size||primary_size>BW_PP_MAX_INDEX){fail(w->owner,"packed_unique_plain_v11_footer");goto done;}
    primary=span(w,BW_PP_PRIMARY,primary_offset,primary_size);if(!primary)goto done;if(memcmp(p->spans[BW_PP_PRIMARY].sha1,primary_sha1,20)){fail(w->owner,"packed_footer_primary_sha1");goto done;}
    struct cursor c={primary,(size_t)primary_size,0};char mount[4097];uint64_t count,ignored,has,names_offset=0,names_size=0,hash_offset=0,hash_size=0;uint8_t names_sha1[20]={0},hash_sha1[20]={0};unsigned hash_present=0;
    if(pak_string(&c,mount)){fail(w->owner,"packed_mount_or_encoding");goto done;}
    int empty_root=!strcmp(mount,"/");
    if(!empty_root&&mount_path(mount)){fail(w->owner,"packed_mount_or_encoding");goto done;}
    if(number(&c,4,&count)||count>BW_PP_MAX_ENTRIES||number(&c,8,&ignored)){fail(w->owner,"packed_entry_count_or_seed");goto done;}
    if(empty_root&&count){fail(w->owner,"packed_nonempty_root_mount");goto done;}
    size_t mn=strlen(mount);
    for(unsigned i=0;i<2;i++){
        if(number(&c,4,&has)||has>1){fail(w->owner,"packed_index_presence");goto done;}
        if(has){uint64_t off,size;if(number(&c,8,&off)||number(&c,8,&size)){fail(w->owner,"packed_index_descriptor_eof");goto done;}const uint8_t *sha=take(&c,20);if(!sha){fail(w->owner,"packed_index_hash_eof");goto done;}if(i){names_offset=off;names_size=size;memcpy(names_sha1,sha,20);}else{hash_present=1;hash_offset=off;hash_size=size;memcpy(hash_sha1,sha,20);}}
        else if(i){fail(w->owner,"packed_filename_index_missing");goto done;}
    }
    uint64_t encoded_size;if(number(&c,4,&encoded_size)||encoded_size>c.size-c.at){fail(w->owner,"packed_encoded_index_size");goto done;}const uint8_t *encoded=take(&c,(size_t)encoded_size);
    if(number(&c,4,&ignored)||ignored||c.at!=c.size){fail(w->owner,"packed_nonencoded_or_trailing_index");goto done;}
    if(empty_root&&encoded_size){fail(w->owner,"packed_empty_root_encoded_payload");goto done;}
    if(!names_size||names_size>BW_PP_MAX_INDEX||primary_offset>names_offset||primary_size>names_offset-primary_offset||names_offset>magic_offset||names_size>magic_offset-names_offset){fail(w->owner,"packed_index_order_or_limit");goto done;}
    names=span(w,BW_PP_NAMES,names_offset,names_size);if(!names)goto done;if(memcmp(p->spans[BW_PP_NAMES].sha1,names_sha1,20)){fail(w->owner,"packed_primary_filename_sha1");goto done;}
    struct cursor d={names,(size_t)names_size,0};uint64_t dirs,total=0,walked=0;if(number(&d,4,&dirs)||dirs>BW_PP_MAX_ENTRIES){fail(w->owner,"packed_directory_limit");goto done;}
    if(empty_root){
        if(dirs||d.at!=d.size){fail(w->owner,"packed_empty_root_filename_payload");goto done;}
        if(hash_present){
            if(hash_size!=8||primary_offset>hash_offset||primary_size>hash_offset-primary_offset||hash_offset>names_offset||hash_size>names_offset-hash_offset){fail(w->owner,"packed_empty_root_hash_span");goto done;}
            empty_hash=read_span(w,hash_offset,hash_size);if(!empty_hash)goto done;uint8_t digest[20];pp_sha1_bytes(empty_hash,8,digest);
            if(memcmp(digest,hash_sha1,20)||u64(empty_hash)){fail(w->owner,"packed_empty_root_hash_payload");goto done;}
        }
        if(sealed(w)||guard(w))goto done;
        /* This no-project token is reserved for fully checked empty indexes. */
        fail(w->owner,"packed_empty_archive");goto done;
    }
    unsigned matches=0;uint32_t location=0;char directory[4097],filename[4097];
    for(uint64_t di=0;di<dirs;di++){
        if((di&511u)==0&&guard(w))goto done;uint64_t files;if(pak_string(&d,directory)||number(&d,4,&files)||files>BW_PP_MAX_ENTRIES-total){fail(w->owner,"packed_directory_or_filename_limit");goto done;}total+=files;
        for(uint64_t fi=0;fi<files;fi++){
            uint64_t loc;if(pak_string(&d,filename)||number(&d,4,&loc)){fail(w->owner,"packed_filename_eof_or_encoding");goto done;}
            size_t dn=strlen(directory),fn=strlen(filename),target=strlen(s->listed);
            if(mn<=target&&dn<=target-mn&&fn==target-mn-dn){char candidate[250];memcpy(candidate,mount,mn);memcpy(candidate+mn,directory,dn);memcpy(candidate+mn+dn,filename,fn+1);
                if(folded(candidate,s->listed)){
                    if(++matches!=1||strcmp(candidate,s->listed)||strchr(filename,'/')||strchr(filename,'\\')||path(filename)){fail(w->owner,"packed_exact_filename_or_alias");goto done;}
                    if(dn){if(directory[dn-1]!='/'){fail(w->owner,"packed_directory_termination");goto done;}directory[dn-1]=0;int invalid=path(directory);directory[dn-1]='/';if(invalid){fail(w->owner,"packed_directory_alias");goto done;}}
                    location=(uint32_t)loc;
                }
            }
            walked++;if((walked&511u)==0&&guard(w))goto done;
        }
    }
    if(d.at!=d.size||total!=count){fail(w->owner,"packed_filename_count_or_trailing_index");goto done;}
    /* Retain the caller's no-match token only for a fully validated index. */
    if(!matches){fail(w->owner,"packed_exact_filename_count_location");goto done;}
    if(matches!=1||location>=encoded_size){fail(w->owner,"packed_encoded_entry_location");goto done;}p->entry_location=location;
    struct cursor e={encoded+(size_t)location,(size_t)encoded_size-location,0};uint64_t bits,entry_offset,unpacked,block_size;
    if(number(&e,4,&bits)){fail(w->owner,"packed_encoded_entry_eof");goto done;}
    uint64_t method=(bits>>23)&63,encrypted=(bits>>22)&1,blocks=(bits>>6)&65535;block_size=bits&63;
    if(block_size==63){if(number(&e,4,&block_size)){fail(w->owner,"packed_block_size_eof");goto done;}}else block_size<<=11;
    if(number(&e,(bits&(UINT64_C(1)<<31))?4:8,&entry_offset)||number(&e,(bits&(UINT64_C(1)<<30))?4:8,&unpacked)){fail(w->owner,"packed_entry_body_eof");goto done;}
    if(method||encrypted||blocks||block_size){fail(w->owner,"packed_compressed_or_encrypted_entry_unsupported");goto done;}
    if(!unpacked||unpacked>BW_PP_MAX_BODY||unpacked>UINT64_C(1048576)||s->request.base_local_bytes>s->request.startup_budget||unpacked>s->request.startup_budget-s->request.base_local_bytes||entry_offset>primary_offset||53>primary_offset-entry_offset||unpacked>primary_offset-entry_offset-53){fail(w->owner,"packed_project_span_or_startup_budget");goto done;}
    header=span(w,BW_PP_HEADER,entry_offset,53);if(!header)goto done;
    if(u64(header)||u64(header+8)!=unpacked||u64(header+16)!=unpacked||u32(header+24)||header[48]||u32(header+49)){fail(w->owner,"packed_plain_entry_header");goto done;}
    body=span(w,BW_PP_BODY,entry_offset+53,unpacked);if(!body)goto done;if(memcmp(header+28,p->spans[BW_PP_BODY].sha1,20)){fail(w->owner,"packed_entry_body_sha1");goto done;}
    unsigned modules=0;if(bw_pp_json(body,(size_t)unpacked,p->modules,&modules,guard,w)){fail(w->owner,"packed_project_json_refused");goto done;}p->module_count=modules;
    if(sealed(w)||guard(w))goto done;result=0;
done:
    free(footer);free(primary);free(names);free(header);free(body);free(empty_hash);if(result)memset(p,0,sizeof *p);return result;
}
static int request(struct bw_packed_project *p,struct pp_state *s,const struct bw_pp_request *r){
    if(!r||!r->source||!r->source->smb||!r->snapshot||!r->current||r->source_kind!=1||r->source_policy!=1||!r->profile||!memchr(r->profile,0,5)||strcmp(r->profile,"none")||
       r->startup_budget>UINT64_C(5368709120)||r->base_local_bytes>r->startup_budget)return fail(p,"packed_live_folder_profile_or_budget_required");
    if(copy(s->archive,sizeof s->archive,r->archive)||copy(s->requested,sizeof s->requested,r->requested)||copy(s->listed,sizeof s->listed,r->listed)||copy(s->destination,sizeof s->destination,r->proposed_destination)||
       path(s->archive)||!ends(s->archive,".pak")||path(s->destination)||!ends(s->destination,".uproject")||strncmp(s->requested,"../../../",9)||strcmp(s->requested,s->listed)||strcmp(s->requested+9,s->destination)||
       !strncmp(s->destination,"sce_sys/",8)||!strncmp(s->destination,".blackwire/",11))return fail(p,"packed_exact_path_policy_required");
    char root[241];copy(root,sizeof root,s->destination);char *slash=strchr(root,'/');if(slash)*slash=0;if(folded(root,"sce_sys")||folded(root,".blackwire"))return fail(p,"packed_reserved_destination");
    if(r->snapshot->count>BW_PP_MAX_INVENTORY+1u||bw_source_snapshot_validate(r->snapshot)||!r->snapshot->parent_count||copy(s->source,sizeof s->source,r->snapshot->source)||copy(s->game,sizeof s->game,r->snapshot->game))return fail(p,"packed_bound_native_snapshot_required");
    const struct bw_source_info *info=bw_source_snapshot_find(r->snapshot,s->archive);if(!info||(info->attributes&(0x10u|0x400u)))return fail(p,"packed_archive_not_exact_original_file");
    s->request=*r;s->request.archive=s->archive;s->request.requested=s->requested;s->request.listed=s->listed;s->request.proposed_destination=s->destination;s->request.profile="none";s->info=*info;memcpy(s->seal,r->snapshot->sha256,32);return 0;
}
int bw_packed_project_open(struct bw_packed_project *p,const struct bw_pp_request *r){
    if(!p)return -1;if(p->busy)return fail(p,"packed_callback_reentry");if(p->state||p->failed)return fail(p,"packed_new_zeroed_session_required");p->busy=1;
    struct pp_state *s=calloc(1,sizeof *s);int bad=-1;if(!s){fail(p,"packed_allocation_refused");goto done;}p->state=s;
    if(request(p,s,r))goto done;struct pp_work w={.owner=p,.state=s,.plan=&s->plan};bad=derive(&w);
done:
    if(bad){if(s)memset(&s->plan,0,sizeof s->plan);p->failed=1;if(!p->error)p->error="packed_validation_refused";}p->busy=0;return bad;
}
const struct bw_pp_plan *bw_packed_project_view(struct bw_packed_project *p){
    if(!p)return NULL;if(p->busy){fail(p,"packed_callback_reentry");return NULL;}if(!p->state||p->failed)return NULL;p->busy=1;
    struct pp_state *s=p->state;struct pp_work w={.owner=p,.state=s,.plan=&s->plan};int bad=sealed(&w)||guard(&w);
    if(bad){memset(&s->plan,0,sizeof s->plan);p->failed=1;}p->busy=0;return bad?NULL:&s->plan;
}
static int plan_equal(const struct bw_pp_plan *a,const struct bw_pp_plan *b){
    return !strcmp(a->archive,b->archive)&&!strcmp(a->requested,b->requested)&&!strcmp(a->listed,b->listed)&&!strcmp(a->proposed_destination,b->proposed_destination)&&
        !strcmp(a->source,b->source)&&!strcmp(a->game,b->game)&&!memcmp(a->native_snapshot_sha256,b->native_snapshot_sha256,32)&&same_info(&a->archive_info,&b->archive_info)&&
        !memcmp(a->spans,b->spans,sizeof a->spans)&&a->entry_location==b->entry_location&&a->module_count==b->module_count&&!memcmp(a->modules,b->modules,sizeof a->modules);
}
int bw_packed_project_revalidate(struct bw_packed_project *p){
    if(!p)return -1;if(p->busy)return fail(p,"packed_callback_reentry");if(!p->state||p->failed)return fail(p,"packed_session_not_current");p->busy=1;
    struct pp_state *s=p->state;struct bw_pp_plan *next=calloc(1,sizeof *next);int bad=-1;if(!next){fail(p,"packed_allocation_refused");goto done;}
    struct pp_work w={.owner=p,.state=s,.plan=next};if(derive(&w))goto done;if(!plan_equal(&s->plan,next)){fail(p,"packed_captured_span_or_project_changed");goto done;}
    s->plan.read_bytes=next->read_bytes;s->plan.read_calls=next->read_calls;s->plan.current_checks=next->current_checks;bad=0;
done:
    free(next);if(bad){memset(&s->plan,0,sizeof s->plan);p->failed=1;}p->busy=0;return bad;
}
int bw_packed_project_destroy(struct bw_packed_project *p){if(!p)return -1;if(p->busy)return fail(p,"packed_callback_reentry");if(p->state){memset(p->state,0,sizeof(struct pp_state));free(p->state);}memset(p,0,sizeof *p);return 0;}
