/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual early reader, decoder and cache; local-file SMB transport model.
 * Optional original game files are read only. No console or live SMB server. */
#define _POSIX_C_SOURCE 200809L
#include "reader/preload/source.h"
#include "library/reader_selection.h"
#include "streamreader/container_source.h"
#include "streamreader/container_manifest.h"
#include "streamreader/container_smb.h"
#include "streamreader/manifest.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

static unsigned checks,files,ranges,contexts,destroys,preads,nstats,hstats,pending_drained,cancellation_cases;
static int canceled,changed,fail_read,fail_named,fail_held,fail_close,cancel_during_read,cancel_after_stat,cancel_on_io_error;
static const char *live_image;
struct smb2fh {int fd;};
struct metadata_request {int active;const char *path;struct smb2fh *fh;struct smb2_stat_64 *out;smb2_command_cb cb;void *opaque;};
struct smb2_context {struct smb2fh *fh;uint8_t *pending;struct metadata_request metadata[2];};
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"compressed-reader line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void stat_convert(const struct stat *s,struct smb2_stat_64 *out){
    memset(out,0,sizeof *out);out->smb2_type=SMB2_TYPE_FILE;out->smb2_size=(uint64_t)s->st_size;
    out->smb2_ino=(uint64_t)s->st_ino;out->smb2_mtime=(uint64_t)s->st_mtim.tv_sec+(changed?1u:0u);
    out->smb2_mtime_nsec=(uint64_t)s->st_mtim.tv_nsec;out->smb2_ctime=(uint64_t)s->st_ctim.tv_sec;out->smb2_ctime_nsec=(uint64_t)s->st_ctim.tv_nsec;
}
struct smb2_context *smb2_init_context(void){contexts++;return calloc(1,sizeof(struct smb2_context));}
void smb2_destroy_context(struct smb2_context *c){CHECK(c);if(c->pending){memset(c->pending,0xee,16);pending_drained++;}
    for(unsigned i=0;i<2;i++)if(c->metadata[i].active){struct metadata_request *r=&c->metadata[i];r->active=0;
        memset(r->out,0xee,sizeof *r->out);r->cb(c,-ESHUTDOWN,NULL,r->opaque);}
    if(c->fh){close(c->fh->fd);free(c->fh);}destroys++;free(c);}
void smb2_set_timeout(struct smb2_context *c,int n){CHECK(c&&n==10);}
void smb2_set_user(struct smb2_context *c,const char *s){CHECK(c&&s&&!strcmp(s,"fixture"));}
void smb2_set_password(struct smb2_context *c,const char *s){CHECK(c&&s&&!s[0]);}
void smb2_set_sign(struct smb2_context *c,int n){CHECK(c&&n==1);}
void smb2_set_security_mode(struct smb2_context *c,uint16_t n){CHECK(c&&n==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED));}
int smb2_connect_share(struct smb2_context *c,const char *server,const char *share,const char *user){
    CHECK(c&&!strcmp(server,"192.0.2.1")&&!strcmp(share,"fixture")&&!strcmp(user,"fixture"));return 0;}
int smb2_stat(struct smb2_context *c,const char *path,struct smb2_stat_64 *out){
    CHECK(c&&!strcmp(path,"fixture.ffpfsc"));struct stat st;nstats++;if(fail_named){fail_named=0;c->pending=(uint8_t *)out;return -EIO;}
    if(stat(live_image,&st))return -EIO;stat_convert(&st,out);
    if(cancel_after_stat&&!--cancel_after_stat)canceled=1;return 0;}
int smb2_fstat(struct smb2_context *c,struct smb2fh *fh,struct smb2_stat_64 *out){
    CHECK(c&&fh==c->fh);struct stat st;hstats++;if(fail_held){fail_held=0;c->pending=(uint8_t *)out;return -EIO;}
    if(fstat(fh->fd,&st))return -EIO;stat_convert(&st,out);return 0;}
int smb2_stat_async(struct smb2_context *c,const char *path,struct smb2_stat_64 *out,smb2_command_cb cb,void *opaque){
    CHECK(c&&out&&cb&&!c->metadata[0].active);c->metadata[0]=(struct metadata_request){1,path,NULL,out,cb,opaque};return 0;}
int smb2_fstat_async(struct smb2_context *c,struct smb2fh *fh,struct smb2_stat_64 *out,smb2_command_cb cb,void *opaque){
    CHECK(c&&out&&cb&&fh&&!c->metadata[1].active);c->metadata[1]=(struct metadata_request){1,NULL,fh,out,cb,opaque};return 0;}
int smb2_get_fd(struct smb2_context *c){CHECK(c);return 42;}
int smb2_which_events(struct smb2_context *c){CHECK(c);return POLLIN|POLLOUT;}
int poll(struct pollfd *fd,nfds_t count,int timeout){CHECK(fd&&count==1&&fd->fd==42&&timeout==250);fd->revents=fd->events;return 1;}
int smb2_service(struct smb2_context *c,int events){
    CHECK(c&&events==(POLLIN|POLLOUT));
    for(unsigned i=0;i<2;i++)if(c->metadata[i].active){struct metadata_request *r=&c->metadata[i];r->active=0;
        int raw=i?smb2_fstat(c,r->fh,r->out):smb2_stat(c,r->path,r->out);r->cb(c,raw,NULL,r->opaque);}
    return 0;}
struct smb2fh *smb2_open(struct smb2_context *c,const char *path,int flags){
    CHECK(c&&!c->fh&&!strcmp(path,"fixture.ffpfsc")&&flags==O_RDONLY);int fd=open(live_image,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return NULL;c->fh=calloc(1,sizeof *c->fh);CHECK(c->fh);c->fh->fd=fd;return c->fh;}
int smb2_close(struct smb2_context *c,struct smb2fh *fh){CHECK(c&&fh==c->fh);int rc=close(fh->fd);free(fh);c->fh=NULL;if(fail_close){fail_close=0;return -EIO;}return rc;}
uint32_t smb2_get_max_read_size(struct smb2_context *c){CHECK(c);return 262144;}
int smb2_pread(struct smb2_context *c,struct smb2fh *fh,uint8_t *out,uint32_t count,uint64_t at){
    CHECK(c&&fh==c->fh&&out&&count&&count<=262144);preads++;
    if(fail_read){fail_read=0;c->pending=out;if(cancel_on_io_error){cancel_on_io_error=0;canceled=1;}return -EIO;}
    if(count>8191)count=8191;ssize_t got=pread(fh->fd,out,count,(off_t)at);
    if(got>0&&cancel_during_read){cancel_during_read=0;canceled=1;}
    return got<0?-errno:(int)got;
}
int smb2_get_nterror(struct smb2_context *c){CHECK(c);return 0;}
static int guard(void *opaque){CHECK(opaque==&canceled);return canceled;}
static int exact(int fd,uint64_t at,void *out,size_t size){
    for(size_t n=0;n<size;){ssize_t got=pread(fd,(char *)out+n,size-n,(off_t)(at+n));if(got<=0)return -1;n+=(size_t)got;}return 0;}
struct original {char *path;uint64_t size;};
struct table {struct original *items;uint32_t count;size_t path_bytes;};
static int collect(void *opaque,const char *path,const struct bw_pfs_inode *node){
    struct table *p=opaque;CHECK(p->count<BW_MANIFEST_MAX_FILES&&!(node->flags&1u));
    struct original *next=realloc(p->items,(p->count+1u)*sizeof *next);CHECK(next);p->items=next;
    p->items[p->count++]=(struct original){strdup(path),node->logical_size};p->path_bytes+=strlen(path)+1;return 0;}
static int raw(void *opaque,uint64_t at,void *out,size_t size){return exact(*(int *)opaque,at,out,size);}
static int unchanged(void *opaque){(void)opaque;return 0;}
/* Model cancellation first observed at the final generation check, after
 * a complete local callback. A simultaneous real decode error must win. */
struct final_cancel {int fd;unsigned fences;};
static int final_raw(void *opaque,uint64_t at,void *out,size_t size){
    struct final_cancel *p=opaque;return exact(p->fd,at,out,size);}
static int final_generation(void *opaque){
    struct final_cancel *p=opaque;return ++p->fences>1?BW_CONTAINER_CANCELED:0;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static struct bp_source *open_source(const void *map,size_t map_size,const char *selection,size_t selection_size){
    static const char share[]="server=192.0.2.1\nshare=fixture\nuser=fixture\npassword=\n";
    struct bp_source *p=NULL;CHECK(!bp_source_open(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));unsigned before=preads+nstats+hstats;CHECK(bp_source_worker_priority(p,384)==700);CHECK(before==preads+nstats+hstats);return p;}
int main(int argc,char **argv){
    CHECK(argc==4||argc==5);int refused=argc==5;live_image=argv[2];
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);struct stat st,live;CHECK(!fstat(fd,&st)&&!stat(live_image,&live));
    struct bw_container *image=calloc(1,sizeof *image);CHECK(image);struct bw_container_ops ops={raw,unchanged};
    CHECK(!bw_container_open(image,&ops,&fd,(uint64_t)st.st_size));struct table table={0};CHECK(!bw_container_walk(image,collect,&table,NULL));
    close(fd);free(image);CHECK(table.count>1);
    size_t pool=40u+(size_t)table.count*32u,map_size=pool+table.path_bytes,offset=0;uint8_t *map=calloc(1,map_size);CHECK(map);
    memcpy(map,BW_MANIFEST_MAGIC,8);put32(map+8,2);put32(map+12,32);put32(map+16,table.count);put64(map+24,table.path_bytes);
    struct bw_reader_selection selected={0};strcpy(selected.source,"fixture.ffpfsc");strcpy(selected.title,"PPSA09999");
    static const char share[]="server=192.0.2.1\nshare=fixture\nuser=fixture\npassword=\n";struct bw_share_config config;
    CHECK(!bw_share_parse(&config,share,sizeof share-1));struct bw_provider_binding binding;
    CHECK(!bw_provider_binding_init(&binding,&config,selected.source,selected.title,BW_PROVIDER_CONTAINER));
    strcpy(selected.game,binding.binding.game);strcpy(selected.binding_hex,binding.binding.game+10);memset(selected.run,'1',32);memset(selected.prepared_record_hex,'2',64);
    selected.source_kind=BW_PROVIDER_CONTAINER;selected.source_policy=BW_PROVIDER_POLICY_V1;selected.manifest_count=table.count;
    uint32_t stream_id=UINT32_MAX;
    for(uint32_t i=0;i<table.count;i++){
        /* Deliberately reverse the PFS traversal order: map IDs are not inodes. */
        const struct original *row=&table.items[table.count-i-1u];size_t n=strlen(row->path);uint8_t *bytes=map+40u+(size_t)i*32u;
        int local=!strcmp(row->path,"eboot.bin");put32(bytes,(uint32_t)offset);put32(bytes+4,(uint32_t)n);put32(bytes+8,local?1:2);
        put64(bytes+16,row->size);put64(bytes+24,(uint64_t)live.st_mtim.tv_sec);memcpy(map+pool+offset,row->path,n+1);offset+=n+1;
        if(local)selected.eboot_size=row->size;else if(row->size>262144)stream_id=i;
    }
    selected.manifest_hash=bw_manifest_fnv1a64(map+40,map_size-40);put64(map+32,selected.manifest_hash);
    char selection[BW_READER_SELECTION_BYTES+1];size_t selection_size=0;CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));
    struct bp_source *source=open_source(map,map_size,selection,selection_size);struct bw_manifest manifest;CHECK(!bw_manifest_open(&manifest,map,map_size));
    uint8_t *left=malloc(BW_PFSC_MAX_READ),*right=malloc(BW_PFSC_MAX_READ);CHECK(left&&right);
    if(refused){
        CHECK(stream_id!=UINT32_MAX);CHECK(bp_source_read(source,stream_id,0,left,1)==-EIO);
        struct bp_source_progress failure;bp_source_progress(source,&failure);
        CHECK(failure.poisoned&&failure.first_poison.valid);
        unsigned prior_reads=preads,prior_named=nstats,prior_held=hstats;
        CHECK(bp_source_read(source,stream_id,0,left,1)==-EIO);
        CHECK(preads==prior_reads&&nstats==prior_named&&hstats==prior_held);
        CHECK(!bp_source_close(source));
        if(strcmp(argv[4],"refuse")){
            struct final_cancel probe={open(live_image,O_RDONLY|O_NOFOLLOW),0};CHECK(probe.fd>=0);
            struct bw_container_ops final_ops={final_raw,final_generation};
            image=calloc(1,sizeof *image);CHECK(image);
            int decoded=bw_container_open(image,&final_ops,&probe,(uint64_t)live.st_size);
            int expected=!strcmp(argv[4],"refuse-codec-cancel")?BW_CONTAINER_CODEC:!strcmp(argv[4],"refuse-format-cancel")?BW_PFS_FORMAT:BW_PFS_UNSUPPORTED;
            fprintf(stderr,"final cancellation precedence: decoded=%d expected=%d\n",decoded,expected);
            CHECK(decoded==expected&&probe.fences>1);
            if(expected==BW_CONTAINER_CODEC)CHECK(image->failed);
            CHECK(!close(probe.fd));free(image);
        }
    }else{
        /* A canceled AIO request must not kill the immutable source session.
         * Interrupt an uncached multi-block read after a positive SMB reply,
         * then verify the next request on that SAME connection against the
         * independent original file. No reconnect or re-admission may hide it. */
        CHECK(stream_id!=UINT32_MAX);CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        unsigned cancel_contexts=contexts,cancel_destroys=destroys;
        cancel_during_read=1;struct bp_read_witness cancel_witness={0};
        int64_t canceled_result=bp_source_read_observed(source,stream_id,131072,left,131072,&cancel_witness);
        CHECK(canceled_result==-ECANCELED&&!cancel_witness.valid&&canceled&&!cancel_during_read);
        canceled=0;
        int64_t retried_result=bp_source_read(source,stream_id,131072,left,131072);
        fprintf(stderr,"cancellation recovery: canceled=%" PRId64 " next=%" PRId64 "\n",canceled_result,retried_result);
        CHECK(retried_result==131072&&contexts==cancel_contexts&&destroys==cancel_destroys);
        struct bw_manifest_record cancel_row;CHECK(!bw_manifest_get(&manifest,stream_id,&cancel_row));
        char cancel_path[4096];int cancel_path_size=snprintf(cancel_path,sizeof cancel_path,"%s/%s",argv[3],cancel_row.path);
        CHECK(cancel_path_size>0&&(size_t)cancel_path_size<sizeof cancel_path);
        int cancel_original=open(cancel_path,O_RDONLY|O_NOFOLLOW);CHECK(cancel_original>=0);
        CHECK(!exact(cancel_original,131072,right,131072)&&!memcmp(left,right,131072));CHECK(!close(cancel_original));
        struct bp_source_performance cancel_perf;bp_source_performance(source,&cancel_perf);
        CHECK(cancel_perf.canceled_reads==1&&cancel_perf.failed_reads==1);cancellation_cases++;
        for(uint32_t i=0;i<manifest.count;i++){
            struct bw_manifest_record row;CHECK(!bw_manifest_get(&manifest,i,&row));if(row.flags!=BW_MANIFEST_STREAM)continue;
            char path[4096],app[2048];int n=snprintf(path,sizeof path,"%s/%s",argv[3],row.path);CHECK(n>0&&(size_t)n<sizeof path);
            n=snprintf(app,sizeof app,"/app0/%s",row.path);CHECK(n>0&&(size_t)n<sizeof app);uint32_t id=UINT32_MAX;
            CHECK(bp_source_match(source,app,strlen(app),row.size,&id)==1&&id==i);
            int original=open(path,O_RDONLY|O_NOFOLLOW);CHECK(original>=0);CHECK(!fstat(original,&st)&&(uint64_t)st.st_size==row.size);
            for(unsigned span=0;span<3&&row.size;span++){
                size_t size=row.size<65536?(size_t)row.size:65536;uint64_t at=span==0?0:span==1?(row.size-size)/2:row.size-size;
                CHECK(bp_source_read(source,id,at,left,size)==(int64_t)size);CHECK(!exact(original,at,right,size)&&!memcmp(left,right,size));ranges++;
            }
            CHECK(!close(original));files++;
        }
        CHECK(stream_id!=UINT32_MAX);CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        unsigned before=preads,ns=nstats,hs=hstats;CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        CHECK(preads==before&&nstats==ns+2&&hstats==hs+2); /* Cached reads retain strict image fences. */
        cancel_contexts=contexts;cancel_destroys=destroys;cancel_after_stat=2;
        CHECK(bp_source_read_observed(source,stream_id,0,left,8,&cancel_witness)==-ECANCELED);
        CHECK(canceled&&!cancel_after_stat&&!cancel_witness.valid);
        canceled=0;CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        CHECK(contexts==cancel_contexts&&destroys==cancel_destroys);cancellation_cases++;
        canceled=1;CHECK(bp_source_read(source,stream_id,0,left,8)==-ECANCELED);canceled=0;
        changed=1;CHECK(bp_source_read(source,stream_id,0,left,8)==-EIO);changed=0;CHECK(!bp_source_close(source));
        source=open_source(map,map_size,selection,selection_size);fail_read=1;
        CHECK(bp_source_read(source,stream_id,0,left,8)==-EIO);CHECK(pending_drained==1);CHECK(!bp_source_close(source));
        for(unsigned fault=0;fault<4;fault++){
            struct bw_container_smb *owned=calloc(1,sizeof *owned);CHECK(owned);struct smb2_context *transport=smb2_init_context();CHECK(transport);
            fail_read=fault==0;fail_named=fault==1;fail_held=fault==2;
            int opened=bw_container_smb_open_owned(owned,&transport,"fixture.ffpfsc");
            if(fault<3){CHECK(opened<0&&!transport&&!owned->smb&&!owned->handle);CHECK(!bw_container_smb_close(owned));}
            else{CHECK(!opened&&transport&&owned->handle);fail_close=1;CHECK(bw_container_smb_close(owned)<0&&!transport&&!owned->smb&&!owned->handle);}
            free(owned);
        }
        CHECK(pending_drained==4);
        /* Cancel before lazy decoder admission and during its first payload.
         * The same SMB context must recover; incomplete handles must close. */
        for(unsigned point=0;point<2;point++){
            source=open_source(map,map_size,selection,selection_size);
            cancel_contexts=contexts;cancel_destroys=destroys;
            cancel_after_stat=point==0;cancel_during_read=point==1;
            CHECK(bp_source_read_observed(source,stream_id,0,left,8,&cancel_witness)==-ECANCELED);
            CHECK(!cancel_witness.valid&&canceled&&!cancel_after_stat&&!cancel_during_read);
            CHECK(contexts==cancel_contexts&&destroys==cancel_destroys);
            canceled=0;CHECK(bp_source_read(source,stream_id,131072,left,131072)==131072);
            CHECK(contexts==cancel_contexts&&destroys==cancel_destroys);
            cancel_original=open(cancel_path,O_RDONLY|O_NOFOLLOW);CHECK(cancel_original>=0);
            CHECK(!exact(cancel_original,131072,right,131072)&&!memcmp(left,right,131072));CHECK(!close(cancel_original));
            CHECK(!bp_source_close(source));cancellation_cases++;
        }
        /* A simultaneous real transport failure remains permanent and drains
         * the pending decoder destination before that workspace can be freed. */
        source=open_source(map,map_size,selection,selection_size);
        CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        unsigned drained_before=pending_drained;
        fail_read=cancel_on_io_error=1;
        CHECK(bp_source_read_observed(source,stream_id,131072,left,131072,&cancel_witness)==-EIO);
        CHECK(!cancel_witness.valid&&canceled&&!cancel_on_io_error&&pending_drained==drained_before+1);
        canceled=0;before=preads;CHECK(bp_source_read(source,stream_id,0,left,8)==-EIO&&preads==before);
        CHECK(!bp_source_close(source));cancellation_cases++;
        /* Source replacement is still fatal when cancellation arrives on its
         * entry stat; clearing the request flag cannot revive stale cached data. */
        source=open_source(map,map_size,selection,selection_size);
        CHECK(bp_source_read(source,stream_id,0,left,8)==8);
        changed=cancel_after_stat=1;
        CHECK(bp_source_read(source,stream_id,131072,left,131072)==-EIO&&canceled&&!cancel_after_stat);
        canceled=changed=0;before=preads;CHECK(bp_source_read(source,stream_id,0,left,8)==-EIO&&preads==before);
        CHECK(!bp_source_close(source));cancellation_cases++;
        /* Failed cleanup of a canceled first-open is a real transport error;
         * the retained session must not be reused after that uncertainty. */
        source=open_source(map,map_size,selection,selection_size);
        cancel_during_read=fail_close=1;
        CHECK(bp_source_read_observed(source,stream_id,0,left,8,&cancel_witness)==-EIO);
        CHECK(canceled&&!cancel_during_read&&!fail_close&&!cancel_witness.valid);
        canceled=0;before=preads;CHECK(bp_source_read(source,stream_id,0,left,8)==-EIO&&preads==before);
        CHECK(!bp_source_close(source));cancellation_cases++;
    }
    CHECK(contexts==destroys);
    printf("{\"passed\":true,\"checks\":%u,\"filesCompared\":%u,\"rangesCompared\":%u,\"transportModelOnly\":true,\"strictCachedImageFences\":true,\"contextsDestroyed\":%u,\"pendingDestinationsDrained\":%u,\"cancellationRecoveryCases\":%u}\n",checks,files,ranges,destroys,pending_drained,cancellation_cases);
    for(uint32_t i=0;i<table.count;i++)free(table.items[i].path);free(table.items);free(map);free(left);free(right);return 0;
}
