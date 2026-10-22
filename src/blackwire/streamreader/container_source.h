#ifndef BW_CONTAINER_SOURCE_H
#define BW_CONTAINER_SOURCE_H
#include "pfs_source.h"
#include "pfsc_range.h"

struct bw_container_ops {
    int (*pread)(void *,uint64_t,void *,size_t);
    /* Zero only when the held and named source still match the admitted
     * immutable generation. Called before and after every public operation. */
    int (*generation)(void *);
};
enum bw_container_kind { BW_CONTAINER_NONE, BW_CONTAINER_PFS, BW_CONTAINER_PFSC_PFS, BW_CONTAINER_WRAPPED_PFS };
enum { BW_CONTAINER_STALE=-32, BW_CONTAINER_CODEC=-33 };
struct bw_container {
    struct bw_container_ops ops;
    void *source;
    uint64_t image_size,inner_base,inner_size;
    uint64_t session;
    struct bw_pfs outer,inner,*filesystem;
    struct bw_pfsc pfsc;
    int kind,ready,failed,has_pfsc,decoder_error;
};
/* Keep this object at a stable address; no copying, concurrent calls or
 * callback re-entry. No retained allocation: workspace is fixed. PFS traversal
 * temporarily allocates bounded inode/name metadata. No extraction/mount/write.
 * Admits plain unsigned D32 PFS, PFSC->PFS, and one-file PFS->PFSC->PFS wrappers.
 * exFAT, signed, encrypted and other layouts fail explicitly in this revision.
 */
int bw_container_open(struct bw_container *,const struct bw_container_ops *,void *,uint64_t);
int bw_container_walk(struct bw_container *,bw_pfs_visit_fn,void *,uint32_t *);
int bw_container_find(struct bw_container *,const char *,struct bw_pfs_inode *);
int bw_container_read(struct bw_container *,const char *,uint64_t,void *,size_t);
/* Path resolution is performed once. Tokens belong to this exact open session;
 * they are invalid after reopening their owner, even at the same address. */
struct bw_container_file {
    struct bw_container *owner;
    uint64_t session,size;
    uint32_t inode;
};
int bw_container_validate(struct bw_container *);
int bw_container_file_open(struct bw_container *,const char *,struct bw_container_file *);
int bw_container_file_read(const struct bw_container_file *,uint64_t,void *,size_t);
const char *bw_container_error(int);
#endif
