#include "container_smb.h"
#include "manifest.h"
#include "../library/send_diagnostic.h"
#include <fcntl.h>
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
static int failure(struct bw_container_smb *p,int transport) {
    p->failed=1;p->last_transport_error=transport;
    if(transport&&p->transport_owner&&p->smb){
        struct smb2_context *smb=p->smb;
        /* Drain while caller/decoder destinations are still alive. This
         * joined worker exclusively owns both pointers for this operation. */
        smb2_destroy_context(smb);
        if(*p->transport_owner==smb)*p->transport_owner=NULL;
        p->smb=NULL;p->handle=NULL;
    }
    return -1;
}
static int generation(void *context) {
    struct bw_container_smb *p=context;
    struct smb2_stat_64 held,named;
    if (p->failed || !p->handle) return -1;
    p->generation_checks++;
    int result=smb2_fstat(p->smb,p->handle,&held);
    if (result) { BW_IMAGE_FAILURE(p,"generation-fstat",result,0,0,NULL,NULL);return failure(p,result); }
    result=smb2_stat(p->smb,p->path,&named);
    if (result) { BW_IMAGE_FAILURE(p,"generation-stat",result,0,0,&held,NULL);return failure(p,result); }
    if (!same(&p->admitted,&held) || !same(&p->admitted,&named)) {
        BW_IMAGE_FAILURE(p,!same(&p->admitted,&held)?"generation-held-mismatch":"generation-named-mismatch",0,0,0,&held,&named);
        return failure(p,0);
    }
    return 0;
}
static int exact_read(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container_smb *p=context;
    if (p->failed || !p->handle || (!out && size) || size>BW_PFSC_MAX_READ ||
        at>p->admitted.smb2_size || size>p->admitted.smb2_size-at) {
        BW_IMAGE_FAILURE(p,!p->handle?"pread-state":"pread-bounds",0,at,size,NULL,NULL);return failure(p,0);
    }
    for (size_t used=0;used<size;) {
        uint32_t want=(uint32_t)(size-used);
        uint32_t maximum=smb2_get_max_read_size(p->smb);
        if(!maximum){BW_IMAGE_FAILURE(p,"pread-max-read",0,at+used,want,NULL,NULL);return failure(p,0);}
        if(want>maximum)want=maximum;
        p->read_calls++;
        int got=smb2_pread(p->smb,p->handle,(uint8_t *)out+used,want,at+used);
        if (got<=0 || (uint32_t)got>want) {BW_IMAGE_FAILURE(p,"pread-return",got,at+used,want,NULL,NULL);return failure(p,got);}
        used+=(size_t)got;p->read_bytes+=(uint64_t)got;
    }
    return 0;
}
static int open_image(struct bw_container_smb *p,struct smb2_context *smb,const char *path,struct smb2_context **owner) {
    if (!p) return BW_PFS_ARGUMENT;
    memset(p,0,sizeof *p);
    if (!smb || !path) return BW_PFS_ARGUMENT;
    size_t length=strnlen(path,sizeof p->path);
    if (!length || length>=sizeof p->path || bw_manifest_validate_path(path,length)) return BW_PFS_ARGUMENT;
    memcpy(p->path,path,length+1);p->smb=smb;p->transport_owner=owner;
    int result=smb2_stat(smb,path,&p->admitted);
    if (result || p->admitted.smb2_type!=SMB2_TYPE_FILE || p->admitted.smb2_size<48 ||
        p->admitted.smb2_size>BW_PFS_MAX_VOLUME) {
        BW_IMAGE_FAILURE(p,result?"admit-stat":"admit-metadata",result,0,0,NULL,NULL);failure(p,result);return BW_PFS_IO;
    }
    p->handle=smb2_open(smb,path,O_RDONLY);
    if (!p->handle) { BW_IMAGE_FAILURE(p,"open-readonly",-1,0,0,NULL,NULL);failure(p,-1);return BW_PFS_IO; }
    struct bw_container_ops ops={exact_read,generation};
    result=bw_container_open(&p->container,&ops,p,p->admitted.smb2_size);
    if (result) { bw_container_smb_close(p);return result; }
    return 0;
}
int bw_container_smb_open(struct bw_container_smb *p,struct smb2_context *smb,const char *path){return open_image(p,smb,path,NULL);}
int bw_container_smb_open_owned(struct bw_container_smb *p,struct smb2_context **owner,const char *path){
    return open_image(p,owner?*owner:NULL,path,owner);
}
int bw_container_smb_close(struct bw_container_smb *p) {
    if (!p) return BW_PFS_ARGUMENT;
    int result=0;
    if (p->handle) result=smb2_close(p->smb,p->handle);
    if(result)BW_IMAGE_FAILURE(p,"close",result,0,0,NULL,NULL);
    p->handle=NULL;p->container.ready=0;p->failed=1;
    if (result) failure(p,result);
    return result;
}
