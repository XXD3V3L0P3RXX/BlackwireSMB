#include "container_smb.h"
#include "manifest.h"
#include "metadata_read_window.h"
#include "../library/send_diagnostic.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#if defined(BW_SEND_DIAGNOSTIC) && BW_SEND_DIAGNOSTIC
/* Private first-failure evidence only. Use captured observations, never an
 * extra network request; the ordinary build does not evaluate these inputs. */
static const char *diagnostic_difference(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b) {
    if(!b)return "not_captured";
    if(a->smb2_type!=b->smb2_type)return "type";
    if(a->smb2_size!=b->smb2_size)return "size";
    if(a->smb2_ino!=b->smb2_ino)return "inode";
    if(a->smb2_mtime!=b->smb2_mtime)return "mtime_seconds";
    if(a->smb2_mtime_nsec!=b->smb2_mtime_nsec)return "mtime_nanoseconds";
    if(a->smb2_ctime!=b->smb2_ctime)return "ctime_seconds";
    if(a->smb2_ctime_nsec!=b->smb2_ctime_nsec)return "ctime_nanoseconds";
    return "none";
}
static void diagnostic_failure(const struct bw_container_smb *p,const char *operation,int raw,uint64_t at,uint64_t size,
                               const struct smb2_stat_64 *held,const struct smb2_stat_64 *named) {
    if(p->failed)return;
    static const struct smb2_stat_64 absent={0};
    const char *held_difference=diagnostic_difference(&p->admitted,held),*named_difference=diagnostic_difference(&p->admitted,named);
    if(!held)held=&absent;if(!named)named=&absent;
    bw_send_log("container-image-failure",
        "path=%.1023s operation=%.40s raw=%d errnoSnapshot=%d offset=%llu requestedBytes=%llu heldDifference=%s namedDifference=%s "
        "admitted={type:%u,ino:%llu,size:%llu,mtime:%llu.%09llu,ctime:%llu.%09llu} "
        "held={type:%u,ino:%llu,size:%llu,mtime:%llu.%09llu,ctime:%llu.%09llu} "
        "named={type:%u,ino:%llu,size:%llu,mtime:%llu.%09llu,ctime:%llu.%09llu}",
        p->path,operation,raw,errno,(unsigned long long)at,(unsigned long long)size,held_difference,named_difference,
        (unsigned)p->admitted.smb2_type,(unsigned long long)p->admitted.smb2_ino,(unsigned long long)p->admitted.smb2_size,
        (unsigned long long)p->admitted.smb2_mtime,(unsigned long long)p->admitted.smb2_mtime_nsec,
        (unsigned long long)p->admitted.smb2_ctime,(unsigned long long)p->admitted.smb2_ctime_nsec,
        (unsigned)held->smb2_type,(unsigned long long)held->smb2_ino,(unsigned long long)held->smb2_size,
        (unsigned long long)held->smb2_mtime,(unsigned long long)held->smb2_mtime_nsec,
        (unsigned long long)held->smb2_ctime,(unsigned long long)held->smb2_ctime_nsec,
        (unsigned)named->smb2_type,(unsigned long long)named->smb2_ino,(unsigned long long)named->smb2_size,
        (unsigned long long)named->smb2_mtime,(unsigned long long)named->smb2_mtime_nsec,
        (unsigned long long)named->smb2_ctime,(unsigned long long)named->smb2_ctime_nsec);
}
#define BW_IMAGE_FAILURE(...) diagnostic_failure(__VA_ARGS__)
#else
#define BW_IMAGE_FAILURE(...) ((void)0)
#endif

static int same(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b) {
    return a->smb2_type==SMB2_TYPE_FILE && b->smb2_type==SMB2_TYPE_FILE &&
        a->smb2_size==b->smb2_size && a->smb2_ino==b->smb2_ino &&
        a->smb2_mtime==b->smb2_mtime && a->smb2_mtime_nsec==b->smb2_mtime_nsec &&
        a->smb2_ctime==b->smb2_ctime && a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
_Static_assert(BW_METADATA_WINDOW_MAX_READ==BW_PFSC_MAX_READ,"metadata window range contract");
static void window_release(struct bw_container_smb *p){
    if(!p->metadata_window_active){free(p->metadata_window);p->metadata_window=NULL;}
}
static int failure(struct bw_container_smb *p,int transport,enum bw_container_smb_failure_operation operation,unsigned integer_result) {
    if(!p->first_failure_seen){p->first_failure_seen=1;p->first_failure_result=transport;
        p->first_failure_operation=operation;p->first_failure_result_known=integer_result;}
    p->failed=1;p->last_transport_error=transport;
    bw_metadata_window_reset(p->metadata_window);
    if(transport&&p->transport_owner&&p->smb){
        struct smb2_context *smb=p->smb;
        /* Drain while caller/decoder destinations are still alive. This
         * joined worker exclusively owns both pointers for this operation. */
        smb2_destroy_context(smb);
        if(*p->transport_owner==smb)*p->transport_owner=NULL;
        p->smb=NULL;p->handle=NULL;
    }
    /* A synchronous failed fetch may still retain its destination until the
     * drain above completes. Its helper stack also borrows the window, so the
     * exact-read wrapper releases it only after that helper returns. */
    window_release(p);
    return -1;
}
static int generation(void *context) {
    struct bw_container_smb *p=context;
    struct smb2_stat_64 held,named;
    bw_metadata_window_reset(p->metadata_window);
    if (p->failed || !p->handle) {window_release(p);return -1;}
    p->generation_checks++;
    int result=smb2_fstat(p->smb,p->handle,&held);
    if (result) { BW_IMAGE_FAILURE(p,"generation-fstat",result,0,0,NULL,NULL);return failure(p,result,BW_IMAGE_FAILURE_FSTAT,1); }
    result=smb2_stat(p->smb,p->path,&named);
    if (result) { BW_IMAGE_FAILURE(p,"generation-stat",result,0,0,&held,NULL);return failure(p,result,BW_IMAGE_FAILURE_GENERATION_STAT,1); }
    if (!same(&p->admitted,&held) || !same(&p->admitted,&named)) {
        BW_IMAGE_FAILURE(p,!same(&p->admitted,&held)?"generation-held-mismatch":"generation-named-mismatch",0,0,0,&held,&named);
        return failure(p,0,BW_IMAGE_FAILURE_GENERATION_METADATA,0);
    }
    return 0;
}
static int direct_read(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container_smb *p=context;
    if (p->failed || !p->handle || (!out && size) || size>BW_PFSC_MAX_READ ||
        at>p->admitted.smb2_size || size>p->admitted.smb2_size-at) {
        BW_IMAGE_FAILURE(p,!p->handle?"pread-state":"pread-bounds",0,at,size,NULL,NULL);return failure(p,0,BW_IMAGE_FAILURE_READ_BOUNDS,0);
    }
    for (size_t used=0;used<size;) {
        uint32_t want=(uint32_t)(size-used);
        uint32_t maximum=smb2_get_max_read_size(p->smb);
        if(!maximum){BW_IMAGE_FAILURE(p,"pread-max-read",0,at+used,want,NULL,NULL);return failure(p,0,BW_IMAGE_FAILURE_MAX_READ,0);}
        if(want>maximum)want=maximum;
        p->read_calls++;
        int got=smb2_pread(p->smb,p->handle,(uint8_t *)out+used,want,at+used);
        if (got<=0 || (uint32_t)got>want) {BW_IMAGE_FAILURE(p,"pread-return",got,at+used,want,NULL,NULL);return failure(p,got,BW_IMAGE_FAILURE_PREAD,1);}
        used+=(size_t)got;p->read_bytes+=(uint64_t)got;
    }
    return 0;
}
static int exact_read(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container_smb *p=context;
    if(!p->metadata_window)return direct_read(p,at,out,size);
    if(p->failed||!p->handle||(!out&&size)||size>BW_PFSC_MAX_READ||at>p->admitted.smb2_size||size>p->admitted.smb2_size-at)
        return direct_read(p,at,out,size);
    p->metadata_window_active=1;
    int result=bw_metadata_window_read(p->metadata_window,p->admitted.smb2_size,at,out,size,direct_read,p);
    p->metadata_window_active=0;
    if(p->failed)window_release(p);
    return result;
}
static int open_image(struct bw_container_smb *p,struct smb2_context *smb,const char *path,struct smb2_context **owner,unsigned metadata) {
    if (!p) return BW_PFS_ARGUMENT;
    memset(p,0,sizeof *p);
    if (!smb || !path) return BW_PFS_ARGUMENT;
    size_t length=strnlen(path,sizeof p->path);
    if (!length || length>=sizeof p->path || bw_manifest_validate_path(path,length)) return BW_PFS_ARGUMENT;
    memcpy(p->path,path,length+1);p->smb=smb;p->transport_owner=owner;
    int result=smb2_stat(smb,path,&p->admitted);
    if (result || p->admitted.smb2_type!=SMB2_TYPE_FILE || p->admitted.smb2_size<48 ||
        p->admitted.smb2_size>BW_PFS_MAX_VOLUME) {
        BW_IMAGE_FAILURE(p,result?"admit-stat":"admit-metadata",result,0,0,NULL,NULL);failure(p,result,result?BW_IMAGE_FAILURE_ADMIT_STAT:BW_IMAGE_FAILURE_ADMIT_METADATA,result!=0);return BW_PFS_IO;
    }
    p->handle=smb2_open(smb,path,O_RDONLY);
    if (!p->handle) { BW_IMAGE_FAILURE(p,"open-readonly",-1,0,0,NULL,NULL);failure(p,-1,BW_IMAGE_FAILURE_OPEN,0);return BW_PFS_IO; }
    if(metadata){
        int saved=errno;
        p->metadata_window=malloc(sizeof *p->metadata_window);
        bw_metadata_window_reset(p->metadata_window);errno=saved;
    }
    struct bw_container_ops ops={exact_read,generation};
    result=bw_container_open(&p->container,&ops,p,p->admitted.smb2_size);
    if (result) { bw_container_smb_close(p);return result; }
    return 0;
}
int bw_container_smb_open(struct bw_container_smb *p,struct smb2_context *smb,const char *path){return open_image(p,smb,path,NULL,0);}
int bw_container_smb_open_owned(struct bw_container_smb *p,struct smb2_context **owner,const char *path){
    return open_image(p,owner?*owner:NULL,path,owner,0);
}
int bw_container_smb_open_owned_metadata(struct bw_container_smb *p,struct smb2_context **owner,const char *path){
    return open_image(p,owner?*owner:NULL,path,owner,1);
}
int bw_container_smb_close(struct bw_container_smb *p) {
    if (!p) return BW_PFS_ARGUMENT;
    int result=0;
    if (p->handle) result=smb2_close(p->smb,p->handle);
    if(result)BW_IMAGE_FAILURE(p,"close",result,0,0,NULL,NULL);
    p->handle=NULL;p->container.ready=0;p->failed=1;
    if (result) failure(p,result,BW_IMAGE_FAILURE_CLOSE,1);
    window_release(p);
    return result;
}
