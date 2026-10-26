#include "container_source.h"
#include <stdatomic.h>
#include <string.h>
#include "exfat_internal.h"

static int exfat_kind(const struct bw_container *p){return p->kind>=BW_CONTAINER_EXFAT&&p->kind<=BW_CONTAINER_WRAPPED_EXFAT;}

static _Atomic uint64_t next_session=1;
static int assign_session(struct bw_container *p) {
    uint64_t value=atomic_load_explicit(&next_session,memory_order_relaxed);
    do {
        if (value==UINT64_MAX) return BW_PFS_LIMIT;
    } while (!atomic_compare_exchange_weak_explicit(&next_session,&value,value+1,memory_order_relaxed,memory_order_relaxed));
    p->session=value;return 0;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p)|(uint64_t)le32(p+4)<<32; }
static int is_pfs(const uint8_t *p) { return le64(p)==2 && le64(p+8)==20130315; }
static int raw(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container *p=context;
    if (at>p->image_size || size>p->image_size-at || size>BW_PFSC_MAX_READ) return -1;
    int result=p->ops.pread(p->source,at,out,size);
    if (result==BW_CONTAINER_CANCELED) p->interrupted=1;
    return result;
}
static int inner(void *context,uint64_t at,void *out,size_t size) {
    struct bw_container *p=context;
    if (at>p->inner_size || size>p->inner_size-at || size>BW_PFSC_MAX_READ) return -1;
    if (!p->has_pfsc) return raw(p,p->inner_base+at,out,size);
    int result=bw_pfsc_read(&p->pfsc,at,out,size);
    if (result) p->decoder_error=result;
    return result;
}
static int finish(struct bw_container *p,int result) {
    int fence=p->ops.generation(p->source);
    if (fence && fence!=BW_CONTAINER_CANCELED) { p->failed=1;return BW_CONTAINER_STALE; }
    /* PFSC reports callback failures as IO. Only an explicit canceled read
     * may undo that transient IO result. Genuine IO/codec/format failures
     * stay fatal even if cancellation also arrives at the final fence. */
    if (p->decoder_error) {
        if (p->decoder_error!=BW_PFSC_IO || !p->interrupted) {
            p->failed=1;return p->decoder_error==BW_PFSC_CODEC?BW_CONTAINER_CODEC:BW_PFS_IO;
        }
        p->decoder_error=0;
    }
    if (result && !(result==BW_PFS_IO && p->interrupted)) {
        if (result==BW_PFS_FORMAT || result==BW_PFS_IO) p->failed=1;
        return result;
    }
    if (p->interrupted || fence==BW_CONTAINER_CANCELED) { p->interrupted=0;return BW_CONTAINER_CANCELED; }
    return result;
}
static int begin(struct bw_container *p) {
    if (!p || !p->ready) return BW_PFS_ARGUMENT;
    if (p->failed) return BW_CONTAINER_STALE;
    int fence=p->ops.generation(p->source);
    if (fence==BW_CONTAINER_CANCELED) return fence;
    if (fence) { p->failed=1;return BW_CONTAINER_STALE; }
    p->interrupted=0;return 0;
}
struct pick { uint32_t count;struct bw_pfs_inode first; };
static int pick_file(void *context,const char *path,const struct bw_pfs_inode *node) {
    struct pick *pick=context;(void)path;
    if (!pick->count++) pick->first=*node;
    return 0;
}
int bw_container_open(struct bw_container *p,const struct bw_container_ops *ops,void *source,uint64_t size) {
    uint8_t head[48];int result;
    if (!p) return BW_PFS_ARGUMENT;
    memset(p,0,sizeof *p);
    if (!ops || !ops->pread || !ops->generation || size<sizeof head || size>BW_PFS_MAX_VOLUME) return BW_PFS_ARGUMENT;
    if (assign_session(p)) return BW_PFS_LIMIT;
    p->ops=*ops;p->source=source;p->image_size=size;
    int fence=p->ops.generation(source);
    if (fence==BW_CONTAINER_CANCELED) return fence;
    if (fence) { p->failed=1;return BW_CONTAINER_STALE; }
    if (raw(p,0,head,sizeof head)) return finish(p,BW_PFS_IO);
    if (le32(head)==0x43534650u) {
        p->inner_size=le64(head+40);p->has_pfsc=1;
        result=bw_pfsc_open(&p->pfsc,raw,p,bw_pfsc_inflate_miniz,NULL,size,0,size,p->inner_size);
        if (result) return finish(p,result==BW_PFSC_IO?BW_PFS_IO:BW_PFS_FORMAT);
    } else if (is_pfs(head)) {
        result=bw_pfs_open(&p->outer,raw,p,size);if (result) return finish(p,result);
        struct pick pick={0};
        result=bw_pfs_walk(&p->outer,pick_file,&pick,NULL);if (result) return finish(p,result);
        if (pick.count!=1) { p->filesystem=&p->outer;p->kind=BW_CONTAINER_PFS;p->ready=1;return finish(p,0); }
        struct bw_pfs_inode node=pick.first;
        if (node.flags&1u) {
            p->inner_size=node.logical_size;p->has_pfsc=1;
            result=bw_pfsc_open(&p->pfsc,raw,p,bw_pfsc_inflate_miniz,NULL,size,node.base,node.stored_size,node.logical_size);
            if (result) return finish(p,result==BW_PFSC_IO?BW_PFS_IO:BW_PFS_FORMAT);
        } else {
            if (node.logical_size<sizeof head) {
                p->filesystem=&p->outer;p->kind=BW_CONTAINER_PFS;p->ready=1;return finish(p,0);
            }
            if (raw(p,node.base,head,sizeof head)) return finish(p,BW_PFS_IO);
            if (!is_pfs(head) && memcmp(head+3,"EXFAT   ",8)) {
                p->filesystem=&p->outer;p->kind=BW_CONTAINER_PFS;p->ready=1;return finish(p,0);
            }
            p->inner_base=node.base;p->inner_size=node.logical_size;
        }
    } else if(!memcmp(head+3,"EXFAT   ",8))p->inner_size=size;
    else return finish(p,BW_PFS_UNSUPPORTED);
    if (inner(p,0,head,sizeof head)) return finish(p,BW_PFS_IO);
    if(!memcmp(head+3,"EXFAT   ",8)){
        result=ex_open(&p->exfat,inner,p,p->inner_size);if(result)return finish(p,result);
        p->kind=p->has_pfsc?BW_CONTAINER_PFSC_EXFAT:p->inner_base?BW_CONTAINER_WRAPPED_EXFAT:BW_CONTAINER_EXFAT;
        /* Shared token bounds use the cluster ID in place of a PFS inode ID.
         * No PFS operation is dispatched to this metadata-only bounds view. */
        p->filesystem=&p->inner;p->inner.inode_count=p->exfat.clusters+2;p->ready=1;return finish(p,0);
    }
    if (!is_pfs(head)) return finish(p,BW_PFS_UNSUPPORTED);
    result=bw_pfs_open(&p->inner,inner,p,p->inner_size);if (result) return finish(p,result);
    p->filesystem=&p->inner;p->kind=p->has_pfsc?BW_CONTAINER_PFSC_PFS:BW_CONTAINER_WRAPPED_PFS;p->ready=1;
    return finish(p,0);
}
int bw_container_walk(struct bw_container *p,bw_pfs_visit_fn visit,void *context,uint32_t *count) {
    int result=begin(p);if (result) return result;
    return finish(p,exfat_kind(p)?ex_walk(&p->exfat,visit,context,count):bw_pfs_walk(p->filesystem,visit,context,count));
}
int bw_container_walk_nodes(struct bw_container *p,bw_pfs_visit_fn visit,void *context,uint32_t *count){
    int result=begin(p);if(result)return result;
    return finish(p,exfat_kind(p)?ex_walk_nodes(&p->exfat,visit,context,count,1):bw_pfs_walk_nodes(p->filesystem,visit,context,count));
}
int bw_container_find(struct bw_container *p,const char *path,struct bw_pfs_inode *out) {
    int result=begin(p);if (result) return result;
    return finish(p,exfat_kind(p)?ex_find(&p->exfat,path,out):bw_pfs_find(p->filesystem,path,out));
}
int bw_container_read(struct bw_container *p,const char *path,uint64_t at,void *out,size_t size) {
    int result=begin(p);if (result) return result;
    struct bw_pfs_inode node;
    result=exfat_kind(p)?ex_find(&p->exfat,path,&node):bw_pfs_find(p->filesystem,path,&node);
    if (!result) result=exfat_kind(p)?ex_file_read(&p->exfat,node.id,node.logical_size,at,out,size):bw_pfs_read_file(p->filesystem,node.id,at,out,size);
    return finish(p,result);
}
int bw_container_validate(struct bw_container *p) { return begin(p); }
int bw_container_file_open(struct bw_container *p,const char *path,struct bw_container_file *out) {
    if (!out) return BW_PFS_ARGUMENT;
    memset(out,0,sizeof *out);
    struct bw_pfs_inode node;
    int result=bw_container_find(p,path,&node);if (result) return result;
    if (node.flags&1u) return BW_PFS_UNSUPPORTED;
    out->owner=p;out->session=p->session;out->size=node.logical_size;out->inode=node.id;
    return 0;
}
int bw_container_file_read(const struct bw_container_file *file,uint64_t at,void *out,size_t size) {
    if (!file || !file->owner || !file->session || file->owner->session!=file->session ||
        (!out && size) || size>BW_PFSC_MAX_READ) return BW_PFS_ARGUMENT;
    if (at>file->size || size>file->size-at) return BW_PFS_RANGE;
    struct bw_container *p=file->owner;
    int result=begin(p);if (result) return result;
    return finish(p,exfat_kind(p)?ex_file_read(&p->exfat,file->inode,file->size,at,out,size):bw_pfs_read_file(p->filesystem,file->inode,at,out,size));
}
const char *bw_container_error(int error) {
    if (error==BW_CONTAINER_CANCELED) return "compressed read operation canceled";
    if (error==BW_CONTAINER_STALE) return "compressed source generation changed or previously failed";
    if (error==BW_CONTAINER_CODEC) return "compressed block checksum, length or zlib stream failed";
    if (error==BW_PFS_UNSUPPORTED) return "unsupported container filesystem, allocation or inode layout";
    return bw_pfs_error(error);
}
