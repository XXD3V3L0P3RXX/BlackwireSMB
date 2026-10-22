#include "container_smb.h"
#include "manifest.h"
#include <fcntl.h>
#include <string.h>

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
    if (result) return failure(p,result);
    result=smb2_stat(p->smb,p->path,&named);
    if (result) return failure(p,result);
    if (!same(&p->admitted,&held) || !same(&p->admitted,&named)) return failure(p,0);
    return 0;
}
static int exact_read(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container_smb *p=context;
    if (p->failed || !p->handle || (!out && size) || size>BW_PFSC_MAX_READ ||
        at>p->admitted.smb2_size || size>p->admitted.smb2_size-at) return failure(p,0);
    for (size_t used=0;used<size;) {
        uint32_t want=(uint32_t)(size-used);
        uint32_t maximum=smb2_get_max_read_size(p->smb);
        if(!maximum)return failure(p,0);
        if(want>maximum)want=maximum;
        p->read_calls++;
        int got=smb2_pread(p->smb,p->handle,(uint8_t *)out+used,want,at+used);
        if (got<=0 || (uint32_t)got>want) return failure(p,got);
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
        p->admitted.smb2_size>BW_PFS_MAX_VOLUME) { failure(p,result);return BW_PFS_IO; }
    p->handle=smb2_open(smb,path,O_RDONLY);
    if (!p->handle) { failure(p,-1);return BW_PFS_IO; }
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
    p->handle=NULL;p->container.ready=0;p->failed=1;
    if (result) failure(p,result);
    return result;
}
