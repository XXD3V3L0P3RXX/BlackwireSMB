#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "source_smb.h"
#include "source_directory.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/smb2-errors.h>

#define BS_MAX_DEPTH 48u
#define BS_MAX_ENTRIES 262144u
#define BS_ATTR_DIRECTORY 0x10u
#define BS_ATTR_DEVICE 0x40u
#define BS_ATTR_REPARSE 0x400u
struct bs_op {
    int done,bad;
    uint32_t status;
    smb2_file_id id;
    struct bw_source_info info;
    uint8_t *page;
    size_t size;
    size_t capacity;
};
struct bs_directory { smb2_file_id id;struct bw_source_info before;int opened; };
struct bs_limits {uint32_t entries,directories;};
struct bs_walk { struct bw_source_smb *source;struct bw_source_plan *plan;const char *relative;unsigned depth;struct bw_source_snapshot *snapshot;struct bs_limits *limits;const char *remote;unsigned prefix_depth; };
static int bs_catalog_parents(struct bw_source_smb *,const char *,int,struct bs_directory *,unsigned *);
static int bs_catalog_close(struct bw_source_smb *,struct bs_directory *,unsigned);

static double bs_now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
const char *bw_source_phase_name(enum bw_source_phase phase){
    static const char *names[]={"idle","scanning","checking_metadata","hashing_startup","checking_source","source_verified",
        "checking_destination","copying_startup","preparing_assets","building_index","verifying_index","measuring_storage","prepared","error"};
    return (unsigned)phase<BW_SOURCE_PHASE_COUNT?names[phase]:"unknown";
}
static void bs_progress(struct bw_source_smb *p){
    if(!p->observe)return;
    struct bw_source_progress value=p->progress;
    value.requests=p->requests;value.entries=p->entries;value.pages=p->pages;value.source_bytes=p->content_bytes;
    value.phase_entries=p->entries-p->phase_entry_base;value.monotonic_seconds=bs_now();
    p->observe(p->observe_opaque,&value);
}
void bw_source_smb_progress(struct bw_source_smb *p,enum bw_source_phase phase,uint32_t files,uint32_t total,uint64_t bytes,uint64_t byte_total){
    if(!p)return;
    if(p->progress.phase!=phase)p->phase_entry_base=p->entries;
    p->progress.phase=phase;p->progress.files_done=files;p->progress.files_total=total;p->progress.bytes_done=bytes;p->progress.bytes_total=byte_total;
    bs_progress(p);
}
static int bs_fail(struct bw_source_smb *p,const char *error){if(!p->error)p->error=error;return -1;}
static int bs_poison(struct bw_source_smb *p,const char *error){
    bs_fail(p,error);
    if(p->smb){struct smb2_context *owned=p->smb;p->smb=NULL;smb2_destroy_context(owned);}
    return -1;
}
static int bs_live(struct bw_source_smb *p){
    double now=bs_now();
    return !p->smb||now<0||now>=p->deadline?bs_fail(p,"source_scan_expired"):0;
}
static int bs_wait(struct bw_source_smb *p,struct smb2_pdu *pdu,struct bs_op *op){
    if(!pdu)return bs_fail(p,"source_request_allocation_failed");
    if(bs_live(p)){smb2_free_pdu(p->smb,pdu);return -1;}
    p->requests++;bs_progress(p);smb2_queue_pdu(p->smb,pdu);
    while(!op->done){
        if(bs_live(p))return bs_poison(p,"source_scan_expired");
        struct pollfd fd={smb2_get_fd(p->smb),(short)smb2_which_events(p->smb),0};
        if(fd.fd<0)return bs_poison(p,"source_transport_unavailable");
        int result=poll(&fd,1,250);
        if(result<0&&errno==EINTR)continue;
        if(result<0||smb2_service(p->smb,result?fd.revents:0)<0)return bs_poison(p,"source_transport_failed");
        bs_progress(p);
    }
    if(op->status&&op->status!=SMB2_STATUS_NO_MORE_FILES)p->last_status=op->status;
    bs_progress(p);
    return op->bad?bs_poison(p,"source_reply_bounds_or_fields_invalid"):0;
}
static int bs_ticks(const struct smb2_timeval *value,uint64_t *out){
    const uint64_t epoch=UINT64_C(116444736000000000);
    if(value->tv_sec<0||value->tv_usec<0||value->tv_usec>=1000000||
        (uint64_t)value->tv_sec>(UINT64_MAX-epoch-(uint64_t)value->tv_usec*10)/10000000)return -1;
    *out=epoch+(uint64_t)value->tv_sec*10000000+(uint64_t)value->tv_usec*10;return 0;
}
static void bs_created(struct smb2_context *smb,int status,void *reply,void *opaque){
    (void)smb;struct bs_op *op=opaque;op->status=(uint32_t)status;
    if(!status){const struct smb2_create_reply *value=reply;
        if(!value)op->bad=1;
        else {memcpy(op->id,value->file_id,SMB2_FD_SIZE);op->info=(struct bw_source_info){value->end_of_file,0,value->last_write_time,value->change_time,value->file_attributes};}
    }
    op->done=1;
}
static void bs_queried(struct smb2_context *smb,int status,void *reply,void *opaque){
    struct bs_op *op=opaque;op->status=(uint32_t)status;
    if(!status){const struct smb2_query_info_reply *value=reply;
        struct smb2_file_all_info *info=value?value->output_buffer:NULL;
        if(!info||info->standard.delete_pending||bs_ticks(&info->basic.last_write_time,&op->info.mtime_ticks)||bs_ticks(&info->basic.change_time,&op->info.ctime_ticks))op->bad=1;
        else {op->info.size=info->standard.end_of_file;op->info.inode=info->index_number;op->info.attributes=info->basic.file_attributes;}
        if(info)smb2_free_data(smb,info);
    }
    op->done=1;
}
static void bs_page(struct smb2_context *smb,int status,void *reply,void *opaque){
    (void)smb;struct bs_op *op=opaque;op->status=(uint32_t)status;
    if(!status){const struct smb2_query_directory_reply *value=reply;
        if(!value||!value->output_buffer||!value->output_buffer_length||value->output_buffer_length>BW_SOURCE_DIRECTORY_PAGE)op->bad=1;
        else {op->size=value->output_buffer_length;memcpy(op->page,value->output_buffer,op->size);}
    }
    op->done=1;
}
static void bs_closed(struct smb2_context *smb,int status,void *reply,void *opaque){
    (void)smb;(void)reply;struct bs_op *op=opaque;op->status=(uint32_t)status;op->done=1;
}
static void bs_read(struct smb2_context *smb,int status,void *reply,void *opaque){
    (void)smb;struct bs_op *op=opaque;op->status=(uint32_t)status;
    if(!status){const struct smb2_read_reply *value=reply;
        if(!value||value->data_length>op->capacity)op->bad=1;
        else op->size=value->data_length;
    }
    op->done=1;
}
static int bs_close(struct bw_source_smb *p,struct bs_directory *directory){
    if(!directory->opened)return 0;
    directory->opened=0;
    if(!p->smb)return -1;
    struct smb2_close_request request={0};struct bs_op op={0};memcpy(request.file_id,directory->id,SMB2_FD_SIZE);
    if(bs_wait(p,smb2_cmd_close_async(p->smb,&request,bs_closed,&op),&op)||op.status)return bs_poison(p,"source_directory_close_failed");
    return 0;
}
static int bs_info(struct bw_source_smb *p,const struct bs_directory *directory,struct bw_source_info *out){
    struct smb2_query_info_request request={0};struct bs_op op={0};
    request.info_type=SMB2_0_INFO_FILE;request.file_info_class=SMB2_FILE_ALL_INFORMATION;request.output_buffer_length=4096;
    memcpy(request.file_id,directory->id,SMB2_FD_SIZE);
    if(bs_wait(p,smb2_cmd_query_info_async(p->smb,&request,bs_queried,&op),&op)||op.status)return bs_fail(p,"source_directory_stat_failed");
    *out=op.info;return 0;
}
static int bs_stat(struct bw_source_smb *p,const struct bs_directory *directory,struct bw_source_info *out){
    if(bs_info(p,directory,out))return -1;
    return !(out->attributes&BS_ATTR_DIRECTORY)||(out->attributes&(BS_ATTR_REPARSE|BS_ATTR_DEVICE))?bs_fail(p,"source_directory_is_linked_or_special"):0;
}
static int bs_open(struct bw_source_smb *p,const char *path,struct bs_directory *directory){
    struct smb2_create_request request={0};struct bs_op op={0};memset(directory,0,sizeof *directory);
    request.impersonation_level=SMB2_IMPERSONATION_IMPERSONATION;
    request.desired_access=SMB2_FILE_LIST_DIRECTORY|SMB2_FILE_READ_ATTRIBUTES|SMB2_FILE_READ_EA;
    request.share_access=SMB2_FILE_SHARE_READ;
    request.create_disposition=SMB2_FILE_OPEN;
    request.create_options=SMB2_FILE_DIRECTORY_FILE|SMB2_FILE_OPEN_REPARSE_POINT;
    request.name=path;
    if(bs_wait(p,smb2_cmd_create_async(p->smb,&request,bs_created,&op),&op)||op.status)return bs_fail(p,"source_directory_open_failed");
    memcpy(directory->id,op.id,SMB2_FD_SIZE);directory->opened=1;
    if(!(op.info.attributes&BS_ATTR_DIRECTORY)||(op.info.attributes&(BS_ATTR_REPARSE|BS_ATTR_DEVICE))||bs_stat(p,directory,&directory->before)){
        bs_fail(p,"source_directory_is_linked_special_or_changed");bs_close(p,directory);return -1;
    }
    return 0;
}
static int bs_same(const struct bw_source_info *a,const struct bw_source_info *b){
    return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;
}
static int bs_walk_directory(struct bs_walk *walk);
static int bs_entry(void *opaque,const char *name,const struct bw_source_info *info){
    struct bs_walk *walk=opaque;struct bw_source_smb *p=walk->source;
    if(!strcmp(name,".")||!strcmp(name,".."))return 0;
    if(++walk->limits->entries>BS_MAX_ENTRIES||p->entries==UINT32_MAX||bs_live(p))return bs_fail(p,"source_entry_or_time_limit");
    p->entries++;
    if(bw_source_excluded(name))return 0;
    size_t length=strlen(name);
    if(!length||length>255||strchr(name,'/')||bw_manifest_validate_path(name,length))return bs_fail(p,"invalid_source_component");
    if(info->attributes&(BS_ATTR_REPARSE|BS_ATTR_DEVICE))return bs_fail(p,"source_contains_link_or_device");
    char relative[1024];int n=snprintf(relative,sizeof relative,"%s%s%s",walk->relative,*walk->relative?"/":"",name);
    if(n<0||(size_t)n>=sizeof relative)return bs_fail(p,"source_relative_path_limit");
    if(bw_manifest_validate_path(relative,(size_t)n))return bs_fail(p,"invalid_source_relative_path");
    if(info->attributes&BS_ATTR_DIRECTORY){struct bs_walk child={p,walk->plan,relative,walk->depth+1,walk->snapshot,walk->limits,walk->remote,walk->prefix_depth};return bs_walk_directory(&child);}
    if(walk->snapshot&&bw_source_snapshot_add(walk->snapshot,relative,info))return bs_fail(p,walk->snapshot->error);
    if(bw_source_plan_add(walk->plan,relative,info->size,bw_source_unix_mtime(info->mtime_ticks))<0)return bs_fail(p,walk->plan->error?walk->plan->error:"invalid_source_plan");
    return 0;
}
static int bs_walk_directory(struct bs_walk *walk){
    struct bw_source_smb *p=walk->source;struct bs_directory directory;char path[2*BW_MANIFEST_MAX_PATH+2];int result=-1;
    if(walk->prefix_depth>BS_MAX_DEPTH||walk->depth>BS_MAX_DEPTH-walk->prefix_depth||++walk->limits->directories>BS_MAX_ENTRIES||p->directories==UINT32_MAX||bs_live(p))return bs_fail(p,"source_directory_or_time_limit");
    p->directories++;
    int n=snprintf(path,sizeof path,"%s%s%s",walk->remote,*walk->relative?"/":"",walk->relative);
    if(n<0||(size_t)n>=sizeof path)return bs_fail(p,"source_remote_path_limit");
    if(bs_open(p,path,&directory))return -1;
    uint8_t *page=malloc(BW_SOURCE_DIRECTORY_PAGE);
    if(!page){bs_fail(p,"source_directory_page_allocation_failed");goto done;}
    if(walk->depth+1>p->peak_held_pages)p->peak_held_pages=walk->depth+1;
    for(unsigned count=0;count<=BS_MAX_ENTRIES;count++){
        struct smb2_query_directory_request request={0};struct bs_op op={0};op.page=page;
        request.file_information_class=SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;request.flags=count?0:SMB2_RESTART_SCANS;
        memcpy(request.file_id,directory.id,SMB2_FD_SIZE);request.output_buffer_length=BW_SOURCE_DIRECTORY_PAGE;request.name="*";
        if(bs_wait(p,smb2_cmd_query_directory_async(p->smb,&request,bs_page,&op),&op))goto done;
        if(op.status==SMB2_STATUS_NO_MORE_FILES){result=0;break;}
        if(op.status){bs_fail(p,"source_directory_page_failed");goto done;}
        p->pages++;p->metadata_bytes+=op.size;
        if(bw_source_directory_page(page,op.size,bs_entry,walk)){bs_fail(p,"source_directory_page_invalid");goto done;}
    }
    if(!result){struct bw_source_info after;
        if(bs_stat(p,&directory,&after)||!bs_same(&directory.before,&after)){bs_fail(p,"source_directory_changed");result=-1;}
        else if(walk->snapshot&&bw_source_snapshot_add(walk->snapshot,walk->relative,&after))result=bs_fail(p,walk->snapshot->error);}
done:
    free(page);
    if(bs_close(p,&directory))result=-1;
    return result;
}
static int bs_plan(struct bw_source_smb *p,const char *game,const char *remote,struct bw_source_map *map,struct bw_source_snapshot *snapshot,int bound){
    if(!p||!map)return -1;
    memset(map,0,sizeof *map);
    if(p->error||bs_live(p))return -1;
    struct bw_source_plan plan;
    if(bw_source_plan_init(&plan,game)){bs_fail(p,plan.error);bw_source_plan_destroy(&plan);return -1;}
    struct bs_directory parents[BS_MAX_DEPTH+1];memset(parents,0,sizeof parents);unsigned count=0;int result=-1;
    if(bound){
        if(!snapshot||bs_catalog_parents(p,remote,0,parents,&count))goto done;
        snapshot->parent_count=count;for(unsigned i=0;i<count;i++)snapshot->parents[i]=parents[i].before;
    }
    struct bs_limits limits={0};struct bs_walk walk={p,&plan,"",0,snapshot,&limits,remote,count};result=bs_walk_directory(&walk);
    if(bs_catalog_close(p,parents,count))result=-1;
    count=0;
    if(!result&&bw_source_plan_finish(&plan,map))result=bs_fail(p,plan.error?plan.error:"source_manifest_failed");
    if(!result&&snapshot&&bw_source_snapshot_finish(snapshot))result=bs_fail(p,snapshot->error);
done:
    if(count&&bs_catalog_close(p,parents,count))result=-1;
    if(result)bw_source_map_destroy(map);
    bw_source_plan_destroy(&plan);
    return result;
}
int bw_source_smb_plan(struct bw_source_smb *p,const char *game,struct bw_source_map *map){return bs_plan(p,game,game,map,NULL,0);}
int bw_source_smb_snapshot(struct bw_source_smb *p,const char *game,struct bw_source_map *map,struct bw_source_snapshot *snapshot){
    if(!p||!map||!snapshot)return -1;
    memset(map,0,sizeof *map);
    if(bw_source_snapshot_init(snapshot,game))return bs_fail(p,snapshot->error);
    return bs_plan(p,game,game,map,snapshot,0);
}
int bw_source_smb_snapshot_bound(struct bw_source_smb *p,const char *remote,const char *game,struct bw_source_map *map,struct bw_source_snapshot *snapshot){
    if(!p||!map||!snapshot)return -1;
    memset(map,0,sizeof *map);
    if(bw_source_snapshot_init_bound(snapshot,remote,game))return bs_fail(p,snapshot->error);
    return bs_plan(p,game,remote,map,snapshot,1);
}
/* QUERY_INFO exposes times at microsecond precision. Directory rows retain
 * their original 100ns FILETIME in the full-tree fingerprint. */
static int bs_expected(const struct bw_source_info *a,const struct bw_source_info *b){
    return a&&b&&a->size==b->size&&a->inode==b->inode&&a->mtime_ticks/10==b->mtime_ticks/10&&a->ctime_ticks/10==b->ctime_ticks/10&&a->attributes==b->attributes;
}
static int bs_file_open(struct bw_source_smb *p,const char *path,const struct bw_source_info *expected,struct bs_directory *file){
    struct smb2_create_request request={0};struct bs_op op={0};memset(file,0,sizeof *file);
    request.impersonation_level=SMB2_IMPERSONATION_IMPERSONATION;
    request.desired_access=SMB2_FILE_READ_DATA|SMB2_FILE_READ_ATTRIBUTES|SMB2_FILE_READ_EA;
    request.share_access=SMB2_FILE_SHARE_READ;request.create_disposition=SMB2_FILE_OPEN;
    request.create_options=SMB2_FILE_NON_DIRECTORY_FILE|SMB2_FILE_OPEN_REPARSE_POINT;request.name=path;
    if(bs_wait(p,smb2_cmd_create_async(p->smb,&request,bs_created,&op),&op)||op.status)return bs_fail(p,"source_file_open_failed");
    memcpy(file->id,op.id,SMB2_FD_SIZE);file->opened=1;
    if((op.info.attributes&(BS_ATTR_DIRECTORY|BS_ATTR_REPARSE|BS_ATTR_DEVICE))||bs_info(p,file,&file->before)||
        (expected&&!bs_expected(expected,&file->before)))return bs_fail(p,"source_file_is_linked_special_or_changed");
    return 0;
}
int bw_source_smb_read(struct bw_source_smb *p,const struct bw_source_snapshot *snapshot,const char *path,bw_source_consume_fn consume,void *opaque){
    if(!p||!snapshot||!path||!consume||p->error||bs_live(p))return -1;
    size_t length=strlen(path);
    const struct bw_source_info *expected=bw_source_snapshot_find(snapshot,path);
    if(!expected||(expected->attributes&(BS_ATTR_DIRECTORY|BS_ATTR_REPARSE|BS_ATTR_DEVICE))||bw_manifest_validate_path(path,length))return bs_fail(p,"source_file_not_in_snapshot");
    struct bs_directory parents[BS_MAX_DEPTH+1],prefix[BS_MAX_DEPTH+1],file={0};memset(parents,0,sizeof parents);memset(prefix,0,sizeof prefix);
    unsigned count=0,prefix_count=0;char remote[2*BW_MANIFEST_MAX_PATH+2],relative[1024];uint8_t *buffer=NULL;int result=-1;
    if(snapshot->parent_count){
        if(bs_catalog_parents(p,snapshot->source,0,prefix,&prefix_count)||prefix_count!=snapshot->parent_count)goto done;
        for(unsigned i=0;i<prefix_count;i++)if(!bs_expected(&snapshot->parents[i],&prefix[i].before)){bs_fail(p,"source_collection_parent_changed");goto done;}
    }
    memcpy(relative,path,length+1);
    for(size_t at=0;;){
        char *slash=count?strchr(relative+at,'/'):relative;
        if(count&&!slash)break;
        if(count+prefix_count>=BS_MAX_DEPTH+1){bs_fail(p,"source_file_parent_limit");goto done;}
        if(slash)*slash=0;
        int n=snprintf(remote,sizeof remote,"%s%s%s",snapshot->source,count?"/":"",count?relative:"");
        const struct bw_source_info *wanted=bw_source_snapshot_find(snapshot,count?relative:"");
        if(n<0||(size_t)n>=sizeof remote||!wanted||!(wanted->attributes&BS_ATTR_DIRECTORY)){bs_fail(p,"source_parent_not_in_snapshot");goto done;}
        unsigned index=count++;
        if(bs_open(p,remote,&parents[index])||!bs_expected(wanted,&parents[index].before)){bs_fail(p,"source_file_parent_changed");goto done;}
        if(slash){*slash=path[slash-relative];at=(size_t)(slash-relative)+1;}
        if(index==0)at=0;
    }
    int n=snprintf(remote,sizeof remote,"%s/%s",snapshot->source,path);
    if(n<0||(size_t)n>=sizeof remote||bs_file_open(p,remote,expected,&file))goto done;
    buffer=malloc(65536);if(!buffer){bs_fail(p,"source_read_buffer_allocation_failed");goto done;}
    for(uint64_t at=0;at<expected->size;){
        struct smb2_read_request request={0};struct bs_op op={0};
        request.length=expected->size-at<65536?(uint32_t)(expected->size-at):65536;request.offset=at;request.buf=buffer;
        op.capacity=request.length;memcpy(request.file_id,file.id,SMB2_FD_SIZE);
        if(bs_wait(p,smb2_cmd_read_async(p->smb,&request,bs_read,&op),&op)||op.status||!op.size){bs_fail(p,"source_file_read_failed_or_truncated");goto done;}
        p->content_bytes+=op.size;
        if(consume(opaque,buffer,op.size)){bs_fail(p,"source_file_consumer_failed");goto done;}
        at+=op.size;
    }
    {struct bw_source_info after;if(bs_info(p,&file,&after)||!bs_same(&file.before,&after)){bs_fail(p,"source_file_changed_while_reading");goto done;}}
    result=0;
done:
    free(buffer);if(bs_close(p,&file))result=-1;
    while(count){struct bs_directory *parent=&parents[--count];
        if(parent->opened&&p->smb){struct bw_source_info after;if(bs_stat(p,parent,&after)||!bs_same(&parent->before,&after)){bs_fail(p,"source_file_parent_changed_while_reading");result=-1;}}
        if(bs_close(p,parent))result=-1;
    }
    if(bs_catalog_close(p,prefix,prefix_count))result=-1;
    return result;
}
/* Catalog lookups use the same owned connection synchronously. The temporary
 * view only separates expected per-item absence from a fatal scan failure.
 * If a nested request destroys the context, publish smb=NULL to the owner
 * before returning; no outer callback may retain a freed context. */
static int bs_lookup_result(struct bw_source_smb *owner,struct bw_source_smb *view,int result){
    int recover=0;
    if(view->smb&&view->error){
        if((!strcmp(view->error,"source_file_open_failed")||!strcmp(view->error,"source_directory_open_failed"))&&
            (view->last_status==UINT32_C(0xc0000034)||view->last_status==UINT32_C(0xc000003a)))recover=BW_SOURCE_MISSING;
        else if((!strcmp(view->error,"source_file_open_failed")||!strcmp(view->error,"source_directory_open_failed"))&&
            (view->last_status==UINT32_C(0xc0000022)||view->last_status==UINT32_C(0xc0000043)||view->last_status==UINT32_C(0xc0000103)||view->last_status==UINT32_C(0xc00000ba)))recover=BW_SOURCE_UNAVAILABLE;
        else if(!strcmp(view->error,"source_file_is_linked_special_or_changed")||!strcmp(view->error,"source_directory_is_linked_special_or_changed")||
            !strcmp(view->error,"source_directory_is_linked_or_special")||!strcmp(view->error,"catalog_source_file_changed")||!strcmp(view->error,"catalog_source_parent_changed"))recover=BW_SOURCE_UNAVAILABLE;
    }
    if(view->error&&!recover){*owner=*view;return -1;}
    if(recover){view->error=owner->error;view->last_status=owner->last_status;result=recover;}
    *owner=*view;return result;
}
static int bs_catalog_parents(struct bw_source_smb *p,const char *path,int include_leaf,struct bs_directory *parents,unsigned *count){
    char relative[1024];size_t size=strlen(path);*count=0;
    if(size>=sizeof relative||(size&&bw_manifest_validate_path(path,size)))return bs_fail(p,"invalid_catalog_source_path");
    memcpy(relative,path,size+1);
    if(p->directories==UINT32_MAX||bs_live(p))return bs_fail(p,"catalog_source_directory_limit");
    unsigned index=(*count)++;
    if(bs_open(p,"",&parents[index]))return -1;
    p->directories++;
    for(char *part=relative;*part;){
        char *slash=strchr(part,'/');if(!slash&&!include_leaf)break;
        if(*count>=BS_MAX_DEPTH+1)return bs_fail(p,"catalog_source_parent_limit");
        if(p->directories==UINT32_MAX||bs_live(p))return bs_fail(p,"catalog_source_directory_limit");
        if(slash)*slash=0;
        index=(*count)++;
        int result=bs_open(p,relative,&parents[index]);if(slash)*slash='/';
        if(result)return -1;
        p->directories++;
        if(!slash)break;
        part=slash+1;
    }
    return 0;
}
static int bs_catalog_close(struct bw_source_smb *p,struct bs_directory *parents,unsigned count){
    int bad=0;
    while(count){struct bs_directory *parent=&parents[--count];
        if(parent->opened&&p->smb){struct bw_source_info after;
            if(bs_stat(p,parent,&after)||!bs_same(&parent->before,&after)){if(p->smb)p->error="catalog_source_parent_changed";bad=1;}}
        if(bs_close(p,parent))bad=1;
    }
    return bad?-1:0;
}
static int bs_catalog_file(struct bw_source_smb *owner,const char *path,int contents,size_t maximum,void **bytes,size_t *size,struct bw_source_info *info){
    if(bytes)*bytes=NULL;
    if(size)*size=0;
    if(info)memset(info,0,sizeof *info);
    if(!owner||!path||!*path||!info||owner->error||bs_live(owner))return -1;
    if(contents&&(!bytes||!size||!maximum||maximum>BW_SOURCE_SMALL_FILE_LIMIT))return bs_fail(owner,"invalid_catalog_file_limit");
    struct bw_source_smb p=*owner;struct bs_directory parents[BS_MAX_DEPTH+1],file={0};memset(parents,0,sizeof parents);
    unsigned count=0;uint8_t *output=NULL;int result=-1;
    if(bs_catalog_parents(&p,path,0,parents,&count)||bs_file_open(&p,path,NULL,&file))goto done;
    if(contents){
        if(file.before.size>maximum){result=BW_SOURCE_TOO_LARGE;goto done;}
        output=malloc(file.before.size?(size_t)file.before.size:1);
        if(!output){bs_fail(&p,"catalog_file_allocation_failed");goto done;}
        for(uint64_t at=0;at<file.before.size;){
            struct smb2_read_request request={0};struct bs_op op={0};
            request.length=file.before.size-at<65536?(uint32_t)(file.before.size-at):65536;request.offset=at;request.buf=output+(size_t)at;
            op.capacity=request.length;memcpy(request.file_id,file.id,SMB2_FD_SIZE);
            if(bs_wait(&p,smb2_cmd_read_async(p.smb,&request,bs_read,&op),&op)||op.status||!op.size){bs_fail(&p,"catalog_file_read_failed_or_truncated");goto done;}
            p.content_bytes+=op.size;at+=op.size;
        }
    }
    {struct bw_source_info after;if(bs_info(&p,&file,&after)||!bs_same(&file.before,&after)){bs_fail(&p,"catalog_source_file_changed");goto done;}}
    *info=file.before;result=BW_SOURCE_FOUND;
done:
    if(bs_close(&p,&file))result=-1;
    if(bs_catalog_close(&p,parents,count))result=-1;
    result=bs_lookup_result(owner,&p,result);
    if(result==BW_SOURCE_FOUND&&contents){*bytes=output;*size=(size_t)info->size;output=NULL;}
    if(result!=BW_SOURCE_FOUND)memset(info,0,sizeof *info);
    free(output);return result;
}
int bw_source_smb_file_info(struct bw_source_smb *p,const char *path,struct bw_source_info *info){return bs_catalog_file(p,path,0,0,NULL,NULL,info);}
int bw_source_smb_small_file(struct bw_source_smb *p,const char *path,size_t maximum,void **bytes,size_t *size,struct bw_source_info *info){return bs_catalog_file(p,path,1,maximum,bytes,size,info);}
struct bs_listing {struct bw_source_smb *source;bw_source_visit_fn visit;void *opaque;uint32_t count;};
static int bs_catalog_entry(void *opaque,const char *name,const struct bw_source_info *info){
    struct bs_listing *list=opaque;
    if(!strcmp(name,".")||!strcmp(name,".."))return 0;
    if(++list->count>BS_MAX_ENTRIES||list->source->entries==UINT32_MAX||bs_live(list->source))return bs_fail(list->source,"catalog_directory_entry_limit");
    list->source->entries++;
    return list->visit(list->opaque,name,info);
}
int bw_source_smb_list(struct bw_source_smb *p,const char *path,bw_source_visit_fn visit,void *opaque){
    if(!p||!path||!visit||p->error||bs_live(p))return -1;
    struct bs_directory parents[BS_MAX_DEPTH+1];memset(parents,0,sizeof parents);unsigned count=0;uint8_t *page=NULL;int result=-1;
    if(bs_catalog_parents(p,path,1,parents,&count))goto done;
    page=malloc(BW_SOURCE_DIRECTORY_PAGE);if(!page){bs_fail(p,"catalog_directory_page_allocation_failed");goto done;}
    struct bs_listing list={p,visit,opaque,0};
    for(unsigned number=0;number<=BS_MAX_ENTRIES;number++){
        struct smb2_query_directory_request request={0};struct bs_op op={0};op.page=page;
        request.file_information_class=SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;request.flags=number?0:SMB2_RESTART_SCANS;
        memcpy(request.file_id,parents[count-1].id,SMB2_FD_SIZE);request.output_buffer_length=BW_SOURCE_DIRECTORY_PAGE;request.name="*";
        if(bs_wait(p,smb2_cmd_query_directory_async(p->smb,&request,bs_page,&op),&op))goto done;
        if(op.status==SMB2_STATUS_NO_MORE_FILES){result=0;break;}
        if(op.status){bs_fail(p,"catalog_directory_page_failed");goto done;}
        p->pages++;p->metadata_bytes+=op.size;
        if(bw_source_directory_page(page,op.size,bs_catalog_entry,&list)){bs_fail(p,"catalog_directory_page_or_consumer_failed");goto done;}
    }
done:
    free(page);if(bs_catalog_close(p,parents,count))result=-1;return result;
}

/* Exact-range companion only. The preceding admitted implementation is retained
 * verbatim; no whole-file/catalog/scan behavior is changed. */
#include <float.h>
static int bs_range_finite(double value){return value>=-DBL_MAX&&value<=DBL_MAX;}
static int bs_range_current(struct bw_source_smb *p,bw_source_range_current_fn current,void *job){
    double now=bs_now();
    if(p->error)return -1;
    if(!p->smb||!bs_range_finite(p->deadline)||!bs_range_finite(now)||now<0||now>=p->deadline)return bs_fail(p,"source_range_expired");
    if(current(job))return bs_fail(p,"source_range_job_not_current");
    now=bs_now();
    if(p->error)return -1;
    if(!p->smb||!bs_range_finite(p->deadline)||!bs_range_finite(now)||now<0||now>=p->deadline)return bs_fail(p,"source_range_expired");
    return 0;
}
static int bs_range_wait(struct bw_source_smb *p,struct smb2_pdu *pdu,struct bs_op *op,bw_source_range_current_fn current,void *job){
    if(!pdu)return bs_fail(p,"source_request_allocation_failed");
    if(bs_range_current(p,current,job)){smb2_free_pdu(p->smb,pdu);return -1;}
    p->requests++;bs_progress(p);smb2_queue_pdu(p->smb,pdu);
    while(!op->done){
        if(bs_range_current(p,current,job))return bs_poison(p,"source_range_pending_read_refused");
        struct pollfd fd={smb2_get_fd(p->smb),(short)smb2_which_events(p->smb),0};
        if(fd.fd<0)return bs_poison(p,"source_transport_unavailable");
        int result=poll(&fd,1,250);
        if(result<0&&errno==EINTR)continue;
        if(result<0||smb2_service(p->smb,result?fd.revents:0)<0)return bs_poison(p,"source_transport_failed");
        bs_progress(p);
    }
    if(op->status&&op->status!=SMB2_STATUS_NO_MORE_FILES)p->last_status=op->status;
    bs_progress(p);
    return op->bad?bs_poison(p,"source_reply_bounds_or_fields_invalid"):0;
}

struct bs_range_guard {
    struct bw_source_smb *source;bw_source_range_current_fn current;void *job;
    bw_source_observer_fn previous;void *previous_opaque;double deadline;int inside,refused;
};
static void bs_range_observe(void *opaque,const struct bw_source_progress *progress){
    struct bs_range_guard *guard=opaque;
    if(guard->inside){guard->refused=1;bs_fail(guard->source,"source_range_guard_reentered");guard->source->deadline=-1;return;}
    guard->inside=1;
    if(bs_range_current(guard->source,guard->current,guard->job))guard->refused=1;
    if(!guard->refused&&guard->previous)guard->previous(guard->previous_opaque,progress);
    if(bs_range_current(guard->source,guard->current,guard->job))guard->refused=1;
    /* Do not destroy a context from an observer: bs_wait must drain its own
     * queued callback while its stack operation and buffer are still alive. */
    if(guard->refused)guard->source->deadline=-1;
    guard->inside=0;
}

int bw_source_smb_read_range(struct bw_source_smb *p,const struct bw_source_snapshot *snapshot,const char *path,
    uint64_t offset,uint64_t length,bw_source_range_current_fn current,void *job,bw_source_consume_fn consume,void *opaque){
    if(!p)return -1;
    if(p->observe==bs_range_observe)return bs_fail(p,"source_range_nested_operation");
    if(!snapshot||!path||!consume||!current||p->error)return bs_fail(p,"source_range_invalid_arguments");
    if(bs_range_current(p,current,job))return -1;
    if(bw_source_snapshot_validate(snapshot)||!snapshot->parent_count)return bs_fail(p,"source_range_invalid_or_unbound_snapshot");
    if(bs_range_current(p,current,job))return -1;
    uint8_t seal[32];memcpy(seal,snapshot->sha256,sizeof seal);
    size_t path_length=strnlen(path,BW_MANIFEST_MAX_PATH+1);
    if(!path_length||path_length>BW_MANIFEST_MAX_PATH||bw_manifest_validate_path(path,path_length))return bs_fail(p,"source_range_invalid_path");
    if(p->requests>UINT32_MAX-8192u)return bs_fail(p,"source_range_request_counter_limit");
    const struct bw_source_info *expected=bw_source_snapshot_find(snapshot,path);
    if(!expected||(expected->attributes&(BS_ATTR_DIRECTORY|BS_ATTR_REPARSE|BS_ATTR_DEVICE))||bw_manifest_validate_path(path,path_length))return bs_fail(p,"source_file_not_in_snapshot");
    if(length>BW_SOURCE_RANGE_LIMIT||offset>expected->size||length>expected->size-offset)return bs_fail(p,"source_range_bounds");
    struct bs_directory parents[BS_MAX_DEPTH+1],prefix[BS_MAX_DEPTH+1],file={0};memset(parents,0,sizeof parents);memset(prefix,0,sizeof prefix);
    unsigned count=0,prefix_count=0;char remote[2*BW_MANIFEST_MAX_PATH+2],relative[1024];uint8_t *buffer=NULL;int result=-1;
    struct bs_range_guard guard={p,current,job,p->observe,p->observe_opaque,p->deadline,0,0};
    p->observe=bs_range_observe;p->observe_opaque=&guard;
    if(snapshot->parent_count){
        if(bs_catalog_parents(p,snapshot->source,0,prefix,&prefix_count)||prefix_count!=snapshot->parent_count)goto done;
        for(unsigned i=0;i<prefix_count;i++)if(!bs_expected(&snapshot->parents[i],&prefix[i].before)){bs_fail(p,"source_collection_parent_changed");goto done;}
    }
    memcpy(relative,path,path_length+1);
    for(size_t at=0;;){
        char *slash=count?strchr(relative+at,'/'):relative;
        if(count&&!slash)break;
        if(count+prefix_count>=BS_MAX_DEPTH+1){bs_fail(p,"source_file_parent_limit");goto done;}
        if(slash)*slash=0;
        int n=snprintf(remote,sizeof remote,"%s%s%s",snapshot->source,count?"/":"",count?relative:"");
        const struct bw_source_info *wanted=bw_source_snapshot_find(snapshot,count?relative:"");
        if(n<0||(size_t)n>=sizeof remote||!wanted||!(wanted->attributes&BS_ATTR_DIRECTORY)){bs_fail(p,"source_parent_not_in_snapshot");goto done;}
        unsigned index=count++;
        if(bs_open(p,remote,&parents[index])||!bs_expected(wanted,&parents[index].before)){bs_fail(p,"source_file_parent_changed");goto done;}
        if(slash){*slash=path[slash-relative];at=(size_t)(slash-relative)+1;}
        if(index==0)at=0;
    }
    int n=snprintf(remote,sizeof remote,"%s/%s",snapshot->source,path);
    if(n<0||(size_t)n>=sizeof remote||bs_file_open(p,remote,expected,&file))goto done;
    if(length)buffer=malloc(65536);if(length&&!buffer){bs_fail(p,"source_read_buffer_allocation_failed");goto done;}
    unsigned read_calls=0;
    for(uint64_t at=0;at<length;){
        if(bs_range_current(p,current,job)||read_calls++==BW_SOURCE_RANGE_MAX_READ_CALLS){bs_fail(p,"source_range_read_call_limit");goto done;}
        struct smb2_read_request request={0};struct bs_op op={0};
        request.length=length-at<65536?(uint32_t)(length-at):65536;request.offset=offset+at;request.buf=buffer;
        op.capacity=request.length;memcpy(request.file_id,file.id,SMB2_FD_SIZE);
        if(bs_range_wait(p,smb2_cmd_read_async(p->smb,&request,bs_read,&op),&op,current,job)||op.status||!op.size){bs_fail(p,"source_file_read_failed_or_truncated");goto done;}
        if(bs_range_current(p,current,job))goto done;
        if(op.size>UINT64_MAX-p->content_bytes){bs_fail(p,"source_range_content_counter_overflow");goto done;}
        p->content_bytes+=op.size;
        if(consume(opaque,buffer,op.size)){bs_fail(p,"source_file_consumer_failed");goto done;}
        if(bs_range_current(p,current,job))goto done;
        at+=op.size;
    }
    {struct bw_source_info after;if(bs_info(p,&file,&after)||!bs_same(&file.before,&after)){bs_fail(p,"source_file_changed_while_reading");goto done;}}
    result=0;
done:
    free(buffer);if(bs_close(p,&file))result=-1;
    while(count){struct bs_directory *parent=&parents[--count];
        if(parent->opened&&p->smb){struct bw_source_info after;if(bs_stat(p,parent,&after)||!bs_same(&parent->before,&after)){bs_fail(p,"source_file_parent_changed_while_reading");result=-1;}}
        if(bs_close(p,parent))result=-1;
    }
    if(bs_catalog_close(p,prefix,prefix_count))result=-1;
    if(!result&&(bs_range_current(p,current,job)||bw_source_snapshot_validate(snapshot)||memcmp(seal,snapshot->sha256,sizeof seal))){bs_fail(p,"source_range_final_snapshot_or_job_changed");result=-1;}
    if(!result&&bs_range_current(p,current,job))result=-1;
    p->observe=guard.previous;p->observe_opaque=guard.previous_opaque;p->deadline=guard.deadline;
    return result;
}
