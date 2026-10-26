#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "compressed_data.h"
#include "source_param.h"
#include "source_profile.h"
#include "source_smb.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>

static int fail(struct bw_compressed_data *p,int error) {
    if (p) {p->failed=1;if (!p->error) p->error=error;}
    return error;
}
static int active(struct bw_compressed_data *p) {
    if (!p || !p->image || p->failed) return BW_COMPRESSED_DATA_STATE;
    if (!p->current || p->current(p->current_opaque)) return fail(p,BW_COMPRESSED_DATA_CANCELLED);
    if (!p->image->handle || p->image->container.session!=p->session ||
        strcmp(p->image->path,p->binding.source)) return fail(p,BW_CONTAINER_STALE);
    return 0;
}
static int live(struct bw_compressed_data *p) {
    int result=active(p);if(result)return result;
    result=bw_container_validate(&p->image->container);
    return result?fail(p,result):0;
}
static int intact(struct bw_compressed_data *p) {
    int result=live(p);if (result) return result;
    uint8_t hash[32];cp_sha_bytes(p->map.bytes,p->map.size,hash);
    if (memcmp(hash,p->map_sha256,32)) return fail(p,BW_COMPRESSED_DATA_INTEGRITY);
    cp_sha_bytes(p->digests,p->digest_size,hash);
    if (memcmp(hash,p->digests_sha256,32)) return fail(p,BW_COMPRESSED_DATA_INTEGRITY);
    const char *profile_error=NULL;
    if (bw_source_profile_hashes(&p->map,p->digests,p->digest_size,p->binding.title,p->profile_sha256,&profile_error))
        return fail(p,BW_COMPRESSED_DATA_INTEGRITY);
    return live(p);
}
static int title_valid(const char *title) {
    if (strnlen(title,10)!=9 || memcmp(title,"PPSA",4)) return 0;
    for (unsigned i=4;i<9;i++) if (title[i]<'0' || title[i]>'9') return 0;
    return 1;
}
struct scanning {struct bw_source_plan *plan;struct bw_compressed_data *owner;size_t path_bytes;uint32_t count;int error;};
static int add(void *opaque,const char *path,const struct bw_pfs_inode *node) {
    struct scanning *p=opaque;
    if (p->owner->current(p->owner->current_opaque)) {p->error=BW_COMPRESSED_DATA_CANCELLED;return -1;}
    size_t length=strnlen(path,BW_MANIFEST_MAX_PATH+1u);
    if (bw_manifest_validate_path(path,length)) {p->error=BW_PFS_PATH;return -1;}
    if (bw_source_excluded(path)) return 0;
    if (node->flags&1u) {p->error=BW_PFS_UNSUPPORTED;return -1;}
    length++;
    if (p->count==BW_COMPRESSED_DATA_MAX_FILES ||
        length>BW_COMPRESSED_DATA_MAX_PATH_BYTES-p->path_bytes) {
        p->error=BW_COMPRESSED_DATA_LIMIT;return -1;
    }
    if (bw_source_plan_add(p->plan,path,node->logical_size,p->owner->image->admitted.smb2_mtime)) {
        p->error=BW_COMPRESSED_DATA_PLAN;return -1;
    }
    p->count++;p->path_bytes+=length;return 0;
}
static void put32(uint8_t *out,uint32_t n) {for (unsigned i=0;i<4;i++) out[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *out,uint64_t n) {for (unsigned i=0;i<8;i++) out[i]=(uint8_t)(n>>(8*i));}

/* Each bounded logical input run passes the container's before/after held+
 * named generation checks before a consumer sees it. Consumer pieces remain
 * <=64 KiB, with current job/session checks between pieces. Remote generation
 * is checked again after consuming the complete run and at EOF; the caller
 * must discard the whole provisional copy on any failure. */
static int transfer(struct bw_compressed_data *p,uint32_t id,
                    bw_compressed_data_consume_fn consume,void *opaque,uint8_t digest[32]) {
    int result=live(p);if (result) return result;
    const struct bw_container_file *file=bw_container_manifest_file(&p->files,id);
    if (!file) return fail(p,BW_CONTAINER_STALE);
    if(!p->buffer)return fail(p,BW_COMPRESSED_DATA_STATE);
    struct cp_sha hash;cp_sha_init(&hash);
    for (uint64_t at=0;at<file->size;) {
        size_t size=file->size-at>BW_COMPRESSED_DATA_READ_CHUNK?BW_COMPRESSED_DATA_READ_CHUNK:(size_t)(file->size-at);
        result=bw_container_file_read(file,at,p->buffer,size);
        if (result) return fail(p,result);
        cp_sha_update(&hash,p->buffer,size);
        if (!p->ready) bw_source_smb_progress(p->progress,BW_SOURCE_HASHING,p->hashed_files,
            p->map.local_count,p->hashed_bytes+at+size,p->map.local_bytes);
        if(consume)for(size_t used=0;used<size;){
            if((result=active(p)))return result;
            size_t piece=size-used;if(piece>BW_COMPRESSED_DATA_CHUNK)piece=BW_COMPRESSED_DATA_CHUNK;
            if(consume(opaque,p->buffer+used,piece))return fail(p,BW_COMPRESSED_DATA_CONSUMER);
            if((result=active(p)))return result;
            used+=piece;
        }
        result=live(p);if (result) return result;
        at+=size;
    }
    result=live(p);if (result) return result;
    cp_sha_final(&hash,digest);return 0;
}
static void release(struct bw_compressed_data *p) {
    bw_container_manifest_destroy(&p->files);bw_source_map_destroy(&p->map);
    free(p->buffer);p->buffer=NULL;
    free(p->digests);p->digests=NULL;p->digest_size=0;memset(&p->manifest,0,sizeof p->manifest);
    p->ready=0;
}
void bw_compressed_data_destroy(struct bw_compressed_data *p) {
    if (p && p->busy) {fail(p,BW_COMPRESSED_DATA_STATE);return;}
    if (p) {release(p);memset(p,0,sizeof *p);}
}
int bw_compressed_data_prepare(struct bw_compressed_data *p,struct bw_container_smb *image,
                              const struct bw_source_binding *binding,uint64_t budget,
                              struct bw_source_smb *progress,int (*current)(void *),void *opaque) {
    if (!p || p->image || p->map.bytes || p->digests || p->buffer || p->files.files || p->ready || p->failed || p->busy)
        return BW_COMPRESSED_DATA_STATE;
    if (!image || !binding || !progress || !current || !budget || budget>BW_COMPRESSED_DATA_MAX_LOCAL_BYTES)
        return fail(p,BW_COMPRESSED_DATA_BUDGET);
    size_t source_length=strnlen(binding->source,sizeof binding->source);
    if (!source_length || source_length==sizeof binding->source ||
        bw_manifest_validate_path(binding->source,source_length) || !title_valid(binding->title) ||
        strnlen(binding->game,sizeof binding->game)==sizeof binding->game ||
        strcmp(image->path,binding->source)) return fail(p,BW_COMPRESSED_DATA_BINDING);
    p->image=image;p->session=image->container.session;p->local_budget=budget;p->binding=*binding;p->busy=1;
    p->progress=progress;p->current=current;p->current_opaque=opaque;
    bw_source_smb_progress(progress,BW_SOURCE_SCANNING,0,0,0,0);
    int result=live(p);struct bw_source_plan plan={0};
    if (result) goto done;
    if (bw_source_plan_init(&plan,binding->game)) {result=BW_COMPRESSED_DATA_BINDING;goto done;}
    struct scanning scan={.plan=&plan,.owner=p};
    result=bw_container_walk(&image->container,add,&scan,NULL);
    if (scan.error) result=scan.error;
    if (result) goto done;
    if (bw_source_plan_finish(&plan,&p->map)) {result=BW_COMPRESSED_DATA_PLAN;goto done;}
    const char *profile_error=NULL;
    if (bw_source_profile_map(&p->map,binding->title,p->profile_sha256,&profile_error)) {
        result=BW_COMPRESSED_DATA_PLAN;goto done;
    }
    if (p->map.local_bytes>budget) {result=BW_COMPRESSED_DATA_BUDGET;goto done;}
    if (bw_manifest_open(&p->manifest,p->map.bytes,p->map.size)) {result=BW_COMPRESSED_DATA_PLAN;goto done;}
    uint32_t param_id=UINT32_MAX;uint64_t boot_size=0,param_size=0;
    for (uint32_t id=0;id<p->manifest.count;id++) {
        struct bw_manifest_record record;
        if (bw_manifest_get(&p->manifest,id,&record)) {result=BW_COMPRESSED_DATA_PLAN;goto done;}
        if (!strcmp(record.path,"eboot.bin")) boot_size=record.size;
        if (!strcmp(record.path,"sce_sys/param.json")) {param_id=id;param_size=record.size;}
    }
    if (!boot_size || boot_size>UINT64_C(2147483648)) {result=BW_COMPRESSED_DATA_PLAN;goto done;}
    if (param_id==UINT32_MAX || !param_size || param_size>BW_SOURCE_PARAM_LIMIT) {
        result=BW_COMPRESSED_DATA_TITLE;goto done;
    }
    result=bw_container_manifest_bind(&p->files,&image->container,&p->manifest);if (result) goto done;
    const struct bw_container_file *param=bw_container_manifest_file(&p->files,param_id);
    if (!param) {result=BW_CONTAINER_STALE;goto done;}
    p->buffer=malloc(BW_COMPRESSED_DATA_READ_CHUNK);
    if(!p->buffer){result=BW_PFS_MEMORY;goto done;}
    result=bw_container_file_read(param,0,p->buffer,(size_t)param_size);if (result) goto done;
    char title[10];
    if (bw_source_param_title(p->buffer,(size_t)param_size,title) || strcmp(title,binding->title)) {
        result=BW_COMPRESSED_DATA_TITLE;goto done;
    }
    p->digest_size=56u+(size_t)p->manifest.count*32u;
    p->digests=calloc(1,p->digest_size);
    if (!p->digests) {result=BW_PFS_MEMORY;goto done;}
    cp_sha_bytes(p->map.bytes,p->map.size,p->map_sha256);
    memcpy(p->digests,"BWDIG001",8);put32(p->digests+8,1);put32(p->digests+12,p->manifest.count);
    put64(p->digests+16,p->manifest.checksum);memcpy(p->digests+24,p->map_sha256,32);
    bw_source_smb_progress(progress,BW_SOURCE_HASHING,0,p->map.local_count,0,p->map.local_bytes);
    for (uint32_t id=0;id<p->manifest.count;id++) {
        struct bw_manifest_record record;
        if (bw_manifest_get(&p->manifest,id,&record)) {result=BW_COMPRESSED_DATA_PLAN;goto done;}
        if (record.flags!=BW_MANIFEST_LOCAL) continue;
        result=transfer(p,id,NULL,NULL,p->digests+56u+(size_t)id*32u);if (result) goto done;
        p->hashed_bytes+=record.size;p->hashed_files++;
        bw_source_smb_progress(progress,BW_SOURCE_HASHING,p->hashed_files,p->map.local_count,p->hashed_bytes,p->map.local_bytes);
    }
    cp_sha_bytes(p->digests,p->digest_size,p->digests_sha256);
    result=intact(p);if (!result) p->ready=1;
done:
    bw_source_plan_destroy(&plan);p->busy=0;
    if (result) {fail(p,result);release(p);}
    return result;
}
int bw_compressed_data_current(struct bw_compressed_data *p) {
    if (!p || !p->ready) return BW_COMPRESSED_DATA_STATE;
    if (p->busy) return fail(p,BW_COMPRESSED_DATA_STATE);
    return intact(p);
}
int bw_compressed_data_copy_local(struct bw_compressed_data *p,uint32_t id,
                                 bw_compressed_data_consume_fn consume,void *opaque) {
    if (!p || !p->ready || !consume) return BW_COMPRESSED_DATA_STATE;
    if (p->busy) return fail(p,BW_COMPRESSED_DATA_STATE);
    int result=intact(p);if (result) return result;
    struct bw_manifest_record record;
    if (bw_manifest_get(&p->manifest,id,&record) || record.flags!=BW_MANIFEST_LOCAL)
        return BW_PFS_ARGUMENT;
    p->busy=1;uint8_t digest[32];result=transfer(p,id,consume,opaque,digest);
    if (!result && memcmp(digest,p->digests+56u+(size_t)id*32u,32))
        result=fail(p,BW_COMPRESSED_DATA_INTEGRITY);
    if (!result) result=intact(p);
    p->busy=0;return result;
}
