/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuses Blackwire's canonical parsers/binding/cache and libsmb2 read APIs.
 * No source mutation, container fallback or game/process control. */
#include "source.h"
#include "../../library/reader_selection.h"
#include "../../library/reader_projection.h"
#include "../../library/source_binding.h"
#include "../../streamreader/container_manifest.h"
#include "../../streamreader/cache.h"
#include "../../streamreader/copy_sha256.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>
/* Experimental source-only policy. Default STRICT retains every per-read
 * metadata fence. ON_OPEN assumes shared files remain immutable for the whole
 * open-handle lifetime: it does not detect in-place edits/path replacement on
 * an already admitted slot. It must be selected explicitly at compilation. */
#define BP_METADATA_PROFILE_STRICT 0
#define BP_METADATA_PROFILE_ON_OPEN 1
#ifndef BP_METADATA_PROFILE
#define BP_METADATA_PROFILE BP_METADATA_PROFILE_STRICT
#endif
#if BP_METADATA_PROFILE != BP_METADATA_PROFILE_STRICT && BP_METADATA_PROFILE != BP_METADATA_PROFILE_ON_OPEN
#error "Unsupported BP_METADATA_PROFILE"
#endif
#define BP_MAP_LIMIT (40u*1024u*1024u)
#define BP_HANDLES 16u
#define BP_READ_LIMIT (64u*1024u*1024u)
struct bp_handle {struct smb2fh *fh;struct smb2_stat_64 admitted;uint32_t id;uint64_t age;};
struct bp_source {
    struct bw_reader_selection selected;
    struct bw_source_binding binding;
    struct bw_share_config config;
    struct bw_manifest map;
    void *map_bytes;uint32_t *lookup;size_t lookup_count;
    struct smb2_context *smb;struct bw_cache cache;struct bp_handle handles[BP_HANDLES];
    struct bw_container *image;struct bw_container_manifest image_files;
    uint64_t image_mtime;
    uint8_t original_map_sha256[32],original_generation_sha256[32];unsigned projected;
    bp_current_fn current;void *opaque;uint64_t clock,network;int failed,ready;
    struct bp_connect_diagnostic connect_diagnostic;
    uint8_t *scratch;
    struct bp_source_performance performance;
    struct bp_source_progress progress;bp_source_progress_fn observe;void *observer;
};
static void count(uint64_t *value,uint64_t add){*value=UINT64_MAX-*value<add?UINT64_MAX:*value+add;}
static int perf_now(struct bp_source *p,uint64_t *out){
    int saved=errno;struct timespec stamp;int rc=clock_gettime(CLOCK_MONOTONIC,&stamp);errno=saved;
    if(rc||stamp.tv_sec<0||stamp.tv_nsec<0||stamp.tv_nsec>=1000000000L||
       (uint64_t)stamp.tv_sec>(UINT64_MAX-999999999u)/1000000000u){count(&p->performance.clock_failures,1);return -1;}
    *out=(uint64_t)stamp.tv_sec*1000000000u+(uint64_t)stamp.tv_nsec;return 0;
}
struct bp_stamp {uint64_t start;int valid;};
static struct bp_stamp perf_begin(struct bp_source *p,struct bp_io_performance *op){
    struct bp_stamp stamp={0,0};count(&op->calls,1);stamp.valid=!perf_now(p,&stamp.start);return stamp;
}
static void perf_end(struct bp_source *p,struct bp_io_performance *op,struct bp_stamp stamp){
    uint64_t end;if(!stamp.valid||perf_now(p,&end))return;
    if(end<stamp.start){count(&p->performance.clock_failures,1);return;}count(&op->nanoseconds,end-stamp.start);
}
static void progress_signal(struct bp_source *p){
    if(!p->observe)return;int saved=errno;struct bp_source_performance perf;
    bp_source_performance(p,&perf);p->observe(p->observer,&p->progress,&perf);errno=saved;
}
static void progress_begin(struct bp_source *p,unsigned stage){
    count(&p->progress.sequence,1);p->progress.stage=stage;p->progress.in_flight=1;
    p->progress.raw=0;p->progress.error=0;p->progress.error_observed=0;progress_signal(p);
}
static void progress_end(struct bp_source *p,int64_t raw,int error,unsigned failed){
    p->progress.in_flight=0;p->progress.raw=raw;p->progress.error=failed?error:0;
    p->progress.error_observed=failed;progress_signal(p);
}
static int named_stat(struct bp_source *p,const char *path,struct smb2_stat_64 *st,unsigned stage){
    progress_begin(p,stage);struct bp_stamp stamp=perf_begin(p,&p->performance.named_stat);int rc=smb2_stat(p->smb,path,st);int error=errno;
    perf_end(p,&p->performance.named_stat,stamp);progress_end(p,rc,error,rc!=0);errno=error;return rc;
}
static int held_stat(struct bp_source *p,struct smb2fh *fh,struct smb2_stat_64 *st,unsigned stage){
    progress_begin(p,stage);struct bp_stamp stamp=perf_begin(p,&p->performance.held_stat);int rc=smb2_fstat(p->smb,fh,st);int error=errno;
    perf_end(p,&p->performance.held_stat,stamp);progress_end(p,rc,error,rc!=0);errno=error;return rc;
}
static struct smb2fh *open_file(struct bp_source *p,const char *path){
    progress_begin(p,BP_SOURCE_OPEN);struct bp_stamp stamp=perf_begin(p,&p->performance.open);struct smb2fh *fh=smb2_open(p->smb,path,O_RDONLY);int error=errno;
    perf_end(p,&p->performance.open,stamp);progress_end(p,fh?1:0,error,!fh);errno=error;return fh;
}
static int close_file(struct bp_source *p,struct smb2fh *fh,unsigned stage){
    progress_begin(p,stage);struct bp_stamp stamp=perf_begin(p,&p->performance.close);int rc=smb2_close(p->smb,fh);int error=errno;
    perf_end(p,&p->performance.close,stamp);progress_end(p,rc,error,rc!=0);errno=error;return rc;
}
static int numeric_ipv4(const char *s){
    unsigned parts=0;
    while(*s){unsigned n=0,digits=0;const char *start=s;
        while(*s>='0'&&*s<='9'){if(++digits>3)return 0;n=n*10u+(unsigned)(*s++-'0');}
        if(!digits||n>255||(digits>1&&*start=='0')||++parts>4)return 0;
        if(!*s)return parts==4;
        if(*s++!='.')return 0;
    }return 0;
}
static int poison(struct bp_source *p,unsigned stage){
    int saved=errno;
    if(!p->progress.first_poison.valid){
        struct bp_source_failure *f=&p->progress.first_poison;f->valid=1;f->stage=stage;f->sequence=p->progress.sequence;
        if(p->progress.stage==stage&&!p->progress.in_flight&&p->progress.error_observed){
            f->raw=p->progress.raw;f->error=p->progress.error;f->error_observed=1;
            if(p->smb){f->ntstatus=(uint32_t)smb2_get_nterror(p->smb);f->ntstatus_observed=1;}
        }
    }
    p->failed=1;p->progress.poisoned=1;progress_signal(p);errno=saved;
    /* A failed synchronous SMB call may still own its destination. Destroy
     * on this SAME worker before returning it to the cache/caller. */
    if(p->smb){smb2_destroy_context(p->smb);p->smb=NULL;}
    for(unsigned i=0;i<BP_HANDLES;i++)p->handles[i].fh=NULL;
    errno=saved;return -1;
}
static int current(struct bp_source *p){return !p||p->failed||!p->ready||!p->smb||!p->current||p->current(p->opaque)?-1:0;}
static int error_code(struct bp_source *p){return p&&p->failed?EIO:p&&p->current&&p->current(p->opaque)?ECANCELED:EIO;}
/* Queue the independent named/held metadata requests together on this exact
 * DATA worker. Both replies must finish before identity comparison or return.
 * Callback state and destinations remain on this stack until normal completion
 * or SAME-worker context destruction has drained every pending destination.
 * Callbacks record results only: destroying a context from smb2_service's own
 * callback would reenter its queue traversal. Ordinary cancellation drains both
 * requests, bounded by the configured SMB timeout, before returning canceled. */
struct bp_metadata_op {
    struct bp_source *source;struct bp_io_performance *performance;
    struct bp_stamp stamp;int done,raw,error;unsigned stage;
};
static void metadata_done(struct smb2_context *smb,int status,void *data,void *opaque){
    (void)smb;(void)data;struct bp_metadata_op *op=opaque;int error=errno;
    if(op->done)return;
    op->raw=status;op->error=error;op->done=1;
    perf_end(op->source,op->performance,op->stamp);errno=error;
}
static int metadata_failure(struct bp_source *p,struct bp_metadata_op *op,int raw,int error){
    p->progress.stage=op->stage;progress_end(p,raw,error,1);
    return poison(p,op->stage);
}
static int paired_stat(struct bp_source *p,const char *path,struct smb2fh *fh,
    struct smb2_stat_64 *named,struct smb2_stat_64 *held,unsigned ns,unsigned hs){
    if(current(p)||!fh)return -1;
    struct bp_metadata_op ops[2]={{.source=p,.performance=&p->performance.named_stat,.stage=ns},
                                  {.source=p,.performance=&p->performance.held_stat,.stage=hs}};
    progress_begin(p,ns);ops[0].stamp=perf_begin(p,ops[0].performance);
    int rc=smb2_stat_async(p->smb,path,named,metadata_done,&ops[0]);int error=errno;
    if(rc<0){if(!ops[0].done)perf_end(p,ops[0].performance,ops[0].stamp);return metadata_failure(p,&ops[0],rc,error);}
    progress_begin(p,hs);ops[1].stamp=perf_begin(p,ops[1].performance);
    rc=smb2_fstat_async(p->smb,fh,held,metadata_done,&ops[1]);error=errno;
    if(rc<0){if(!ops[1].done)perf_end(p,ops[1].performance,ops[1].stamp);return metadata_failure(p,&ops[1],rc,error);}
    while(!ops[0].done||!ops[1].done){
        struct bp_metadata_op *pending=ops[0].done?&ops[1]:&ops[0];
        struct pollfd fd={smb2_get_fd(p->smb),(short)smb2_which_events(p->smb),0};
        if(fd.fd<0)return metadata_failure(p,pending,-1,EIO);
        rc=poll(&fd,1,250);error=errno;
        if(rc<0&&error==EINTR)continue;
        if(rc<0)return metadata_failure(p,pending,rc,error);
        /* service(0) also expires timed-out PDUs; polling without servicing on
         * an idle socket could otherwise leave callback storage live forever. */
        rc=smb2_service(p->smb,rc?fd.revents:0);error=errno;
        if(rc<0)return metadata_failure(p,pending,rc,error);
        for(unsigned i=0;i<2;i++)if(ops[i].done&&ops[i].raw)
            return metadata_failure(p,&ops[i],ops[i].raw,ops[i].error);
    }
    for(unsigned i=0;i<2;i++)if(ops[i].raw)
        return metadata_failure(p,&ops[i],ops[i].raw,ops[i].error);
    p->progress.stage=hs;progress_end(p,0,0,0);
    /* The caller must compare both identities BEFORE observing cancellation:
     * a real replacement plus cancellation remains a permanent source error. */
    return 0;
}
static int stat_same(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b){
    return a->smb2_type==SMB2_TYPE_FILE&&b->smb2_type==SMB2_TYPE_FILE&&
        a->smb2_size==b->smb2_size&&a->smb2_ino==b->smb2_ino&&
        a->smb2_mtime==b->smb2_mtime&&a->smb2_mtime_nsec==b->smb2_mtime_nsec&&
        a->smb2_ctime==b->smb2_ctime&&a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
static int remote_path(struct bp_source *p,uint32_t id,char out[2048],struct bw_manifest_record *row){
    if(bw_manifest_get(&p->map,id,row)||row->flags!=BW_MANIFEST_STREAM)return -1;
    int n=snprintf(out,2048,"%s/%s",p->binding.source,row->path);return n<0||n>=2048?-1:0;
}
/* Container metadata and decoded blocks share this DATA worker's exact image
 * handle. Remote generation checks bracket the entire logical read, including
 * cache hits. Decoder callbacks check cancellation in RAM between bounded
 * payload calls; they do not multiply SMB stat requests for each PFSC block. */
static int image_current(void *opaque){
    struct bp_source *p=opaque;
    if(!p||p->failed||!p->ready||!p->smb||!p->current)return -1;
    return p->current(p->opaque)?BW_CONTAINER_CANCELED:0;
}
static int image_payload(void *opaque,uint64_t at,void *output,size_t size){
    struct bp_source *p=opaque;struct bp_handle *h=&p->handles[0];
    int state=image_current(p);if(state)return state;
    if(!h->fh||size>BW_PFSC_MAX_READ||at>h->admitted.smb2_size||size>h->admitted.smb2_size-at)return -1;
    uint32_t maximum=smb2_get_max_read_size(p->smb);if(!maximum)return poison(p,BP_SOURCE_MAX_READ);
    for(size_t done=0;done<size;){
        state=image_current(p);if(state)return state;
        uint32_t want=(uint32_t)(size-done);if(want>maximum)want=maximum;
        if(want>p->performance.largest_pread_request)p->performance.largest_pread_request=want;
        progress_begin(p,BP_SOURCE_PREAD);struct bp_stamp stamp=perf_begin(p,&p->performance.pread);
        int got=smb2_pread(p->smb,h->fh,(uint8_t *)output+done,want,at+done);int error=errno;
        perf_end(p,&p->performance.pread,stamp);progress_end(p,got,error,got<=0||(uint32_t)got>want);errno=error;
        if(got<=0||(uint32_t)got>want)return poison(p,BP_SOURCE_PREAD);
        if((uint64_t)got>UINT64_MAX-p->network)return poison(p,BP_SOURCE_COUNTER);
        p->network+=(uint64_t)got;done+=(size_t)got;
    }
    return image_current(p);
}
static int image_fence(struct bp_source *p,int final){
    struct bp_handle *h=&p->handles[0];struct smb2_stat_64 named,held;
    unsigned ns=final?BP_SOURCE_FINAL_NAMED:BP_SOURCE_ENTRY_NAMED,hs=final?BP_SOURCE_FINAL_HELD:BP_SOURCE_ENTRY_HELD;
    if(current(p)||!h->fh)return -1;
    if(paired_stat(p,p->binding.source,h->fh,&named,&held,ns,hs))return -1;
    if(!stat_same(&named,&held)||!stat_same(&h->admitted,&held))return poison(p,final?BP_SOURCE_FINAL_IDENTITY:BP_SOURCE_ENTRY_IDENTITY);
    return current(p);
}
/* A canceled first read may leave lazy admission incomplete. Release only
 * its owned handle/workspace on this same worker; keep the source connection
 * and immutable admission so a subsequent job can initialize it afresh. */
static struct bp_handle *image_open_canceled(struct bp_source *p){
    struct bp_handle *h=&p->handles[0];struct smb2fh *held=h->fh;h->fh=NULL;
    bw_container_manifest_destroy(&p->image_files);free(p->image);p->image=NULL;
    if(held&&close_file(p,held,BP_SOURCE_DISCONNECT_CLOSE))poison(p,BP_SOURCE_DISCONNECT_CLOSE);
    return NULL;
}
static struct bp_handle *image_remote(struct bp_source *p,uint32_t id){
    struct bp_handle *h=&p->handles[0];
    if(!p->image){
        if(named_stat(p,p->binding.source,&h->admitted,BP_SOURCE_ENTRY_NAMED))return poison(p,BP_SOURCE_ENTRY_NAMED),NULL;
        if(h->admitted.smb2_type!=SMB2_TYPE_FILE||h->admitted.smb2_size<48||h->admitted.smb2_size>BW_PFS_MAX_VOLUME||h->admitted.smb2_mtime!=p->image_mtime)
            return poison(p,BP_SOURCE_ENTRY_METADATA),NULL;
        if(p->projected){
            struct bw_provider_binding identity={.kind=BW_PROVIDER_CONTAINER,.policy=BW_PROVIDER_POLICY_V1,.binding=p->binding};uint8_t generation[32];
            bw_provider_image_generation(&identity,&h->admitted,p->original_map_sha256,generation);
            if(memcmp(generation,p->original_generation_sha256,32))return poison(p,BP_SOURCE_ENTRY_IDENTITY),NULL;
        }
        h->fh=open_file(p,p->binding.source);if(!h->fh)return poison(p,BP_SOURCE_OPEN),NULL;
        if(image_fence(p,0))return image_current(p)==BW_CONTAINER_CANCELED?image_open_canceled(p):NULL;
        p->image=calloc(1,sizeof *p->image);if(!p->image)return poison(p,BP_SOURCE_CONTAINER),NULL;
        struct bw_container_ops ops={image_payload,image_current};
        int result=bw_container_open(p->image,&ops,p,h->admitted.smb2_size);
        if(!result)result=p->projected?bw_container_manifest_bind_streams(&p->image_files,p->image,&p->map):bw_container_manifest_bind(&p->image_files,p->image,&p->map);
        if(result==BW_CONTAINER_CANCELED&&!p->failed)return image_open_canceled(p);
        if(result)return poison(p,BP_SOURCE_CONTAINER),NULL;
        if(image_fence(p,1))return image_current(p)==BW_CONTAINER_CANCELED?image_open_canceled(p):NULL;
    }else if(image_fence(p,0))return NULL;
    h->id=id;return h;
}
static struct bp_handle *remote(struct bp_source *p,uint32_t id){
    if(p->selected.source_kind==BW_PROVIDER_CONTAINER)return image_remote(p,id);
    char path[2048];struct bw_manifest_record row;
    if(current(p)||remote_path(p,id,path,&row))return NULL;
    struct bp_handle *slot=NULL,*victim=&p->handles[0];
    for(unsigned i=0;i<BP_HANDLES;i++){
        struct bp_handle *h=&p->handles[i];if(h->fh&&h->id==id)slot=h;
        if(!h->fh||h->age<victim->age)victim=h;
    }
#if BP_METADATA_PROFILE == BP_METADATA_PROFILE_ON_OPEN
    /* Only this exact live slot is reused. The reader/loader/finish scope still
     * checks current session and captured handle in RAM on this one worker. */
    if(slot){
        if(current(p))return NULL;
        if(p->clock==UINT64_MAX){poison(p,BP_SOURCE_COUNTER);return NULL;}
        slot->age=++p->clock;return slot;
    }
#endif
    struct smb2_stat_64 named,held;
    if(slot){
        if(paired_stat(p,path,slot->fh,&named,&held,BP_SOURCE_ENTRY_NAMED,BP_SOURCE_ENTRY_HELD))return NULL;
    }else if(named_stat(p,path,&named,BP_SOURCE_ENTRY_NAMED)){poison(p,BP_SOURCE_ENTRY_NAMED);return NULL;}
    if(named.smb2_type!=SMB2_TYPE_FILE||named.smb2_size!=row.size||named.smb2_mtime!=row.mtime_sec){poison(p,BP_SOURCE_ENTRY_METADATA);return NULL;}
    if(!slot){
        slot=victim;
        if(slot->fh){struct smb2fh *old=slot->fh;slot->fh=NULL;if(close_file(p,old,BP_SOURCE_EVICT_CLOSE)){poison(p,BP_SOURCE_EVICT_CLOSE);return NULL;}}
        slot->fh=open_file(p,path);
        if(!slot->fh){poison(p,BP_SOURCE_OPEN);return NULL;}
        slot->admitted=named;slot->id=id;
        if(held_stat(p,slot->fh,&held,BP_SOURCE_ENTRY_HELD)){poison(p,BP_SOURCE_ENTRY_HELD);return NULL;}
    }
    if(!stat_same(&named,&held)||!stat_same(&slot->admitted,&held)){poison(p,BP_SOURCE_ENTRY_IDENTITY);return NULL;}
    if(current(p))return NULL;
    if(p->clock==UINT64_MAX){poison(p,BP_SOURCE_COUNTER);return NULL;}
    slot->age=++p->clock;return slot;
}
/* A synchronous logical read pins one exact slot/handle. The cache never
 * retains this stack-owned scope and no block loader opens or evicts a file. */
struct bp_read_scope {
    struct bp_source *source;struct bp_handle *slot;struct smb2fh *fh;uint32_t id;
    uint64_t last_block,staged_at;size_t staged_size;
};
static int scope_current(const struct bp_read_scope *scope,uint32_t id){
    struct bp_source *p=scope?scope->source:NULL;
    if(current(p))return -1;
    if(scope->id!=id||!scope->slot||!scope->fh||scope->slot->id!=id||scope->slot->fh!=scope->fh)
        return poison(p,BP_SOURCE_SCOPE);
    return 0;
}
static int remote_finish(const struct bp_read_scope *scope){
    if(!scope||scope_current(scope,scope->id))return -1;
    struct bp_source *p=scope->source;
    if(p->selected.source_kind==BW_PROVIDER_CONTAINER)return image_fence(p,1);
#if BP_METADATA_PROFILE == BP_METADATA_PROFILE_STRICT
    char path[2048];struct bw_manifest_record row;
    if(remote_path(p,scope->id,path,&row))return poison(p,BP_SOURCE_FINAL_PATH);
    struct smb2_stat_64 named,held;
    if(paired_stat(p,path,scope->fh,&named,&held,BP_SOURCE_FINAL_NAMED,BP_SOURCE_FINAL_HELD))return -1;
    if(named.smb2_type!=SMB2_TYPE_FILE||named.smb2_size!=row.size||named.smb2_mtime!=row.mtime_sec)return poison(p,BP_SOURCE_FINAL_METADATA);
    if(!stat_same(&named,&held)||!stat_same(&scope->slot->admitted,&held))return poison(p,BP_SOURCE_FINAL_IDENTITY);
    if(scope_current(scope,scope->id))return -1;
#endif
    if(p->clock==UINT64_MAX)return poison(p,BP_SOURCE_COUNTER);
    scope->slot->age=++p->clock;return 0;
}
static int load(void *opaque,uint32_t id,uint64_t block,uint8_t *dest,size_t capacity,size_t *out_size){
    struct bp_read_scope *scope=opaque;struct bp_source *p=scope?scope->source:NULL;*out_size=0;
    if(scope_current(scope,id)||capacity!=BW_CACHE_BLOCK||block>UINT64_MAX/BW_CACHE_BLOCK)return -1;
    struct bw_manifest_record row;if(bw_manifest_get(&p->map,id,&row)||row.flags!=BW_MANIFEST_STREAM)return -1;
    uint64_t at=block*BW_CACHE_BLOCK;if(at>=row.size||block>scope->last_block||!p->scratch)return -1;
    size_t n=row.size-at<capacity?(size_t)(row.size-at):capacity;
    /* The loader still publishes one64KiB cache entry. A complete staged cold
     * run is borrowed only inside this logical request and never outlives it. */
    if(scope->staged_size&&at>=scope->staged_at&&at-scope->staged_at<=scope->staged_size&&
       n<=scope->staged_size-(size_t)(at-scope->staged_at)){
        memcpy(dest,p->scratch+(size_t)(at-scope->staged_at),n);*out_size=n;return 0;
    }
    scope->staged_size=0;
    size_t blocks=1;
    while(blocks<BP_COALESCE_BYTES/BW_CACHE_BLOCK&&block+blocks<=scope->last_block&&
          !bw_cache_contains(&p->cache,id,block+blocks))blocks++;
    size_t run=blocks*BW_CACHE_BLOCK;if(row.size-at<run)run=(size_t)(row.size-at);
    if(p->selected.source_kind==BW_PROVIDER_CONTAINER){
        if(id>=p->image_files.count||!p->image_files.files)return poison(p,BP_SOURCE_CONTAINER);
        int decoded=bw_container_file_read(&p->image_files.files[id],at,p->scratch,run);
        if(decoded==BW_CONTAINER_CANCELED)return -1;
        if(decoded)return poison(p,BP_SOURCE_CONTAINER);
        if(scope_current(scope,id))return -1;
        scope->staged_at=at;scope->staged_size=run;
        count(&p->performance.coalesced_runs,1);count(&p->performance.coalesced_blocks,blocks);
        memcpy(dest,p->scratch,n);*out_size=n;return 0;
    }
    uint32_t maximum=smb2_get_max_read_size(p->smb);if(!maximum)return poison(p,BP_SOURCE_MAX_READ);
    size_t done=0;
    while(done<run){
        if(scope_current(scope,id))return -1;
        uint32_t want=(uint32_t)(run-done);if(want>maximum)want=maximum;
        if(want>p->performance.largest_pread_request)p->performance.largest_pread_request=want;
        progress_begin(p,BP_SOURCE_PREAD);struct bp_stamp stamp=perf_begin(p,&p->performance.pread);
        int got=smb2_pread(p->smb,scope->fh,p->scratch+done,want,at+done);int error=errno;
        perf_end(p,&p->performance.pread,stamp);progress_end(p,got,error,got<=0||(uint32_t)got>want);errno=error;
        if(got<=0||(uint32_t)got>want){poison(p,BP_SOURCE_PREAD);return -1;}
        if((uint64_t)got>UINT64_MAX-p->network){poison(p,BP_SOURCE_COUNTER);return -1;}
        p->network+=(uint64_t)got;done+=(size_t)got;
        if(scope_current(scope,id))return -1;
    }
    /* No scratch byte or partial block is published before the whole run
     * and current/cancellation checks complete. STRICT additionally requires
     * final remote metadata identity checks; optional ON_OPEN assumes immutable
     * files and uses the captured-slot/current RAM fence before result/witness. */
    scope->staged_at=at;scope->staged_size=run;
    count(&p->performance.coalesced_runs,1);count(&p->performance.coalesced_blocks,blocks);
    memcpy(dest,p->scratch,n);*out_size=n;return 0;
}
static uint64_t path_hash(const char *path,size_t size){return bw_manifest_fnv1a64(path,size);}
static int source_admit(struct bp_source **out,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,
    const void *envelope,size_t envelope_size,bp_current_fn guard,void *opaque){
    if(!out)return -1;
    *out=NULL;
    if(!map||!map_size||map_size>BP_MAP_LIMIT||!guard||guard(opaque))return -1;
    struct bp_source *p=calloc(1,sizeof *p);struct bw_share_config config={0};
    if(!p)return -1;
    p->current=guard;p->opaque=opaque;
    struct bw_provider_binding identity;
    if(bw_reader_selection_parse(&p->selected,selection,selection_size)||
       (bw_reader_selection_is_projection(&p->selected)?(!envelope||!envelope_size):(envelope||envelope_size))||
       (p->selected.source_kind!=BW_PROVIDER_FOLDER&&p->selected.source_kind!=BW_PROVIDER_CONTAINER)||p->selected.source_policy!=BW_PROVIDER_POLICY_V1||
       bw_share_parse(&config,share,share_size)||!numeric_ipv4(config.server)||
       bw_provider_binding_init(&identity,&config,p->selected.source,p->selected.title,(enum bw_provider_kind)p->selected.source_kind)||
       strcmp(identity.binding.game,p->selected.game)||strcmp(identity.binding.game+10,p->selected.binding_hex))goto fail;
    p->binding=identity.binding;
    p->map_bytes=malloc(map_size);if(!p->map_bytes)goto fail;memcpy(p->map_bytes,map,map_size);
    if(bw_manifest_open(&p->map,p->map_bytes,map_size)||p->map.count!=p->selected.manifest_count||
       p->map.checksum!=p->selected.manifest_hash)goto fail;
    if(bw_reader_selection_is_projection(&p->selected)){
        struct bw_reader_projection *proof=NULL;
        if(bw_reader_projection_open(&proof,&p->selected,p->map_bytes,map_size,envelope,envelope_size,NULL))goto fail;
        int bad=bw_reader_projection_source(proof,p->original_map_sha256,p->original_generation_sha256,&p->image_mtime);
        bw_reader_projection_clear(&proof);if(bad)goto fail;p->projected=1;
    }
    p->lookup_count=1;while(p->lookup_count<(size_t)p->map.count*2u)p->lookup_count*=2;
    p->lookup=calloc(p->lookup_count,sizeof *p->lookup);if(!p->lookup)goto fail;
    unsigned boot=0;
    for(uint32_t i=0;i<p->map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&p->map,i,&row))goto fail;
        if(p->selected.source_kind==BW_PROVIDER_CONTAINER&&!p->projected){
            if(!i)p->image_mtime=row.mtime_sec;else if(row.mtime_sec!=p->image_mtime)goto fail;
        }
        if(!strcmp(row.path,"eboot.bin")){
            if(row.flags!=BW_MANIFEST_LOCAL||row.size!=p->selected.eboot_size)goto fail;
            boot++;
        }
        size_t at=(size_t)path_hash(row.path,row.path_length)&(p->lookup_count-1);
        while(p->lookup[at])at=(at+1)&(p->lookup_count-1);
        p->lookup[at]=i+1;
    }
    if(boot!=1||guard(opaque))goto fail;
    p->config=config;bw_share_clear(&config);*out=p;return 0;
fail:
    bw_share_clear(&config);bp_source_close(p);return -1;
}
int bp_source_admit(struct bp_source **out,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,bp_current_fn guard,void *opaque){
    return source_admit(out,map,map_size,selection,selection_size,share,share_size,NULL,0,guard,opaque);
}
int bp_source_admit_projection(struct bp_source **out,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,
    const void *envelope,size_t envelope_size,bp_current_fn guard,void *opaque){
    if(!envelope||!envelope_size){if(out)*out=NULL;return -1;}
    return source_admit(out,map,map_size,selection,selection_size,share,share_size,envelope,envelope_size,guard,opaque);
}
static void connect_failure(struct bp_source *p,unsigned stage,int raw,int error){
    if(!p->connect_diagnostic.stage){p->connect_diagnostic.stage=stage;p->connect_diagnostic.raw=raw;p->connect_diagnostic.error=error;}
}
static int connect_source(struct bp_source *p,void *storage,size_t storage_size,int external){
    if(!p)return -1;
    if(p->failed||p->ready||!p->current){
        connect_failure(p,BP_CONNECT_STATE,(p->failed?1:0)|(p->ready?2:0)|(!p->current?4:0),0);
        bw_share_clear(&p->config);return -1;
    }
    int raw=p->current(p->opaque);int error=errno;
    if(raw){connect_failure(p,BP_CONNECT_INITIAL_CURRENT,raw,error);bw_share_clear(&p->config);return -1;}
    raw=external?bw_cache_init_with_storage(&p->cache,storage,storage_size):bw_cache_init(&p->cache);error=errno;
    if(raw){connect_failure(p,BP_CONNECT_CACHE,raw,error);goto fail;}
    p->scratch=malloc(BP_COALESCE_BYTES);error=errno;
    if(!p->scratch){connect_failure(p,BP_CONNECT_SCRATCH,0,error);goto fail;}
    p->smb=smb2_init_context();error=errno;
    if(!p->smb){connect_failure(p,BP_CONNECT_CONTEXT,0,error);goto fail;}
    smb2_set_timeout(p->smb,10);p->connect_diagnostic.options_returned++;
    smb2_set_user(p->smb,p->config.user);p->connect_diagnostic.options_returned++;
    smb2_set_password(p->smb,p->config.password);p->connect_diagnostic.options_returned++;
    smb2_set_sign(p->smb,1);p->connect_diagnostic.options_returned++;
    smb2_set_security_mode(p->smb,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);p->connect_diagnostic.options_returned++;
    raw=smb2_connect_share(p->smb,p->config.server,p->config.share,p->config.user);error=errno;
    if(raw<0){
        connect_failure(p,BP_CONNECT_SHARE,raw,error);
        p->connect_diagnostic.ntstatus=(uint32_t)smb2_get_nterror(p->smb);p->connect_diagnostic.ntstatus_valid=1;
        goto fail;
    }
    raw=p->current(p->opaque);error=errno;
    if(raw){connect_failure(p,BP_CONNECT_FINAL_CURRENT,raw,error);goto fail;}
    bw_share_clear(&p->config);p->ready=1;return 0;
fail:
    bw_share_clear(&p->config);bp_source_disconnect(p);return -1;
}
int bp_source_connect(struct bp_source *p){return connect_source(p,NULL,0,0);}
int bp_source_connect_with_storage(struct bp_source *p,void *storage,size_t size){return connect_source(p,storage,size,1);}
void bp_source_connect_diagnostic(const struct bp_source *p,struct bp_connect_diagnostic *out){
    if(out){if(p)*out=p->connect_diagnostic;else memset(out,0,sizeof *out);}
}
int bp_source_open(struct bp_source **out,const void *map,size_t map_size,
    const void *selection,size_t selection_size,const void *share,size_t share_size,bp_current_fn guard,void *opaque){
    if(bp_source_admit(out,map,map_size,selection,selection_size,share,share_size,guard,opaque))return -1;
    if(bp_source_connect(*out)){bp_source_close(*out);*out=NULL;return -1;}return 0;
}
int bp_source_match(const struct bp_source *p,const char *path,size_t size,uint64_t bytes,uint32_t *out){
    if(out)*out=0;
    if(!p||!path||!out||size<6||size>BW_MANIFEST_MAX_PATH+6u||memcmp(path,"/app0/",6))return -1;
    const char *relative=path+6;size_t length=size-6;
    if(bw_manifest_validate_path(relative,length))return -1;
    size_t at=(size_t)path_hash(relative,length)&(p->lookup_count-1);
    for(size_t attempts=0;attempts<p->lookup_count;attempts++,at=(at+1)&(p->lookup_count-1)){
        uint32_t item=p->lookup[at];if(!item)return 0;
        struct bw_manifest_record row;if(bw_manifest_get(&p->map,item-1,&row))return -1;
        if(row.path_length==length&&!memcmp(row.path,relative,length)){
            if(row.size!=bytes)return -1;
            *out=item-1;return row.flags==BW_MANIFEST_STREAM?1:0;
        }
    }return -1;
}
static int64_t read_source_impl(struct bp_source *p,uint32_t id,uint64_t at,void *output,size_t length,struct bp_read_witness *witness){
    if(witness)memset(witness,0,sizeof *witness);
    const size_t requested=length;
    struct bw_manifest_record row;
    if(current(p))return -error_code(p);
    if(length>BP_READ_LIMIT||bw_manifest_get(&p->map,id,&row)||row.flags!=BW_MANIFEST_STREAM)return -EINVAL;
    if(!length||at>=row.size)return 0;
    if(!output)return -EFAULT;
    if(row.size-at<length)length=(size_t)(row.size-at);
    struct bp_handle *held=remote(p,id);if(!held)return -error_code(p);
    struct bp_read_scope scope={p,held,held->fh,id,(at+length-1u)/BW_CACHE_BLOCK,0,0};
    size_t done=0,digested=0;uint64_t hit_bytes=0;struct cp_sha sha;
    if(witness)cp_sha_init(&sha);
    while(done<length){
        if(current(p))return -error_code(p);
        const uint8_t *data=NULL;size_t available=0,within=(size_t)((at+done)%BW_CACHE_BLOCK);
        uint64_t hits=p->cache.hits;
        if(bw_cache_get(&p->cache,id,(at+done)/BW_CACHE_BLOCK,load,&scope,&data,&available)||!data||within>=available)return -error_code(p);
        size_t take=available-within;if(take>length-done)take=length-done;
        if(p->cache.hits!=hits)hit_bytes+=take;
        if(witness&&digested<65536){
            size_t sample=take;if(sample>65536-digested)sample=65536-digested;
            cp_sha_update(&sha,data+within,sample);digested+=sample;
        }
        memcpy((uint8_t*)output+done,data+within,take);done+=take;
    }
    if(remote_finish(&scope)||current(p))return -error_code(p);
    count(&p->performance.cache_hit_bytes,hit_bytes);
    if(witness){
        witness->map_id=id;witness->offset=at;witness->requested=requested;witness->returned=done;
        witness->digest_offset=at;witness->digest_size=(uint32_t)digested;cp_sha_final(&sha,witness->digest_sha256);
        witness->fetched_payload_bytes=p->network;witness->valid=1;
    }
    return (int64_t)done;
}
static int64_t read_source(struct bp_source *p,uint32_t id,uint64_t at,void *output,size_t length,struct bp_read_witness *witness){
    if(!p)return read_source_impl(p,id,at,output,length,witness);
    count(&p->performance.reads,1);count(&p->performance.requested_bytes,length);
    struct bp_stamp stamp=perf_begin(p,&p->performance.read);
    int64_t result=read_source_impl(p,id,at,output,length,witness);
    perf_end(p,&p->performance.read,stamp);
    if(result>0){count(&p->performance.completed_reads,1);count(&p->performance.completed_bytes,(uint64_t)result);}
    else if(result<0){
        count(&p->performance.failed_reads,1);if(result==-ECANCELED)count(&p->performance.canceled_reads,1);
        if(!p->progress.first_failed_read.valid)p->progress.first_failed_read=(struct bp_logical_failure){.offset=at,.requested=length,.result=result,.map_id=id,.valid=1,.poisoned=p->failed?1u:0u};
    }
    progress_signal(p);return result;
}
int64_t bp_source_read(struct bp_source *p,uint32_t id,uint64_t at,void *output,size_t size){return read_source(p,id,at,output,size,NULL);}
int64_t bp_source_read_observed(struct bp_source *p,uint32_t id,uint64_t at,void *output,size_t size,struct bp_read_witness *witness){return read_source(p,id,at,output,size,witness);}
int bp_source_disconnect(struct bp_source *p){
    if(!p)return 0;
    int result=0;
    if(p->smb){
        for(unsigned i=0;i<BP_HANDLES;i++)if(p->handles[i].fh){
            struct smb2fh *h=p->handles[i].fh;p->handles[i].fh=NULL;
            if(close_file(p,h,BP_SOURCE_DISCONNECT_CLOSE)){result=-1;poison(p,BP_SOURCE_DISCONNECT_CLOSE);break;}
        }
        if(p->smb){smb2_destroy_context(p->smb);p->smb=NULL;}
    }
    free(p->scratch);p->scratch=NULL;
    bw_container_manifest_destroy(&p->image_files);free(p->image);p->image=NULL;
    bw_share_clear(&p->config);bw_cache_destroy(&p->cache);p->ready=0;p->failed=1;return result;
}
int bp_source_close(struct bp_source *p){if(!p)return 0;int result=bp_source_disconnect(p);free(p->lookup);free(p->map_bytes);memset(p,0,sizeof *p);free(p);return result;}
uint64_t bp_source_network_bytes(const struct bp_source *p){return p?p->network:0;}
int bp_source_worker_priority(const struct bp_source *p,int upstream){
    return p&&p->ready&&!p->failed&&p->selected.source_kind==BW_PROVIDER_CONTAINER?700:upstream;
}
uint64_t bp_source_cache_hits(const struct bp_source *p){return p?p->cache.hits:0;}
void bp_source_performance(const struct bp_source *p,struct bp_source_performance *out){
    if(!out)return;if(!p){memset(out,0,sizeof *out);return;}*out=p->performance;
    out->payload_bytes=p->network;out->cache_hits=p->cache.hits;out->cache_misses=p->cache.misses;
    out->cache_evictions=p->cache.evictions;out->cache_resident=p->cache.resident_count;
    out->scratch_bytes=p->scratch?BP_COALESCE_BYTES:0;
}

void bp_source_observe(struct bp_source *p,bp_source_progress_fn observe,void *opaque){if(p){p->observe=observe;p->observer=opaque;}}
void bp_source_progress(const struct bp_source *p,struct bp_source_progress *out){if(out){if(p)*out=p->progress;else memset(out,0,sizeof *out);}}
