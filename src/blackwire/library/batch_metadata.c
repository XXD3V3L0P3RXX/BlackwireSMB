#include "batch_metadata.h"
#include "derived_batch_internal.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>

static int bm_bad(const char **error,const char *message){if(error)*error=message;return -1;}
static void bm_u32(struct cp_sha *h,uint32_t x){uint8_t b[4];for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(x>>(8u*i));cp_sha_update(h,b,sizeof b);}
static void bm_u64(struct cp_sha *h,uint64_t x){uint8_t b[8];for(unsigned i=0;i<8;i++)b[i]=(uint8_t)(x>>(8u*i));cp_sha_update(h,b,sizeof b);}
static void bm_text(struct cp_sha *h,const char *p){size_t n=strlen(p);bm_u32(h,(uint32_t)n);cp_sha_update(h,p,n);}
static int bm_order(const void *left,const void *right){
    const struct bw_batch_metadata_range *a=left,*b=right;
    if(a->archive_index!=b->archive_index)return a->archive_index<b->archive_index?-1:1;
    if(a->offset!=b->offset)return a->offset<b->offset?-1:1;
    return a->size<b->size?-1:a->size>b->size;
}
/* Every input range is nonempty and subtraction-bounds-checked beforehand. */
static uint32_t bm_merge(struct bw_batch_metadata_range *r,uint32_t count){
    qsort(r,count,sizeof *r,bm_order);uint32_t out=0;
    for(uint32_t i=0;i<count;i++){
        if(out&&r[out-1].archive_index==r[i].archive_index&&r[i].offset<=r[out-1].offset+r[out-1].size){
            uint64_t end=r[i].offset+r[i].size,prior=r[out-1].offset+r[out-1].size;
            if(end>prior)r[out-1].size=end-r[out-1].offset;
        }else r[out++]=r[i];
    }
    return out;
}
static uint32_t bm_original(const struct bw_manifest *base,const struct bw_pbb_archive *a){
    for(uint32_t i=0;i<base->count;i++){
        struct bw_manifest_record r;if(bw_manifest_get(base,i,&r))return UINT32_MAX;
        if(!strcmp(r.path,a->path))return r.flags==BW_MANIFEST_STREAM&&r.size==a->info.size&&r.mtime_sec==bw_source_unix_mtime(a->info.mtime_ticks)?i:UINT32_MAX;
    }
    return UINT32_MAX;
}
static void bm_hash(struct bw_batch_metadata_plan *p){
    struct cp_sha h;cp_sha_init(&h);bm_text(&h,"Blackwire native packed startup metadata seed plan v1");
    bm_u32(&h,p->version);bm_u32(&h,p->allocation_unit);cp_sha_update(&h,p->original.binding.sha256,32);
    cp_sha_update(&h,p->prepared.commitment_sha256,32);cp_sha_update(&h,p->native_snapshot_sha256,32);
    cp_sha_update(&h,p->facts_sha256,32);cp_sha_update(&h,p->base_map_sha256,32);cp_sha_update(&h,p->map_sha256,32);
    bm_u32(&h,p->archive_count);bm_u32(&h,p->span_count);bm_u32(&h,p->range_count);
    bm_u64(&h,p->declared_bytes);bm_u64(&h,p->unique_bytes);bm_u64(&h,p->allocation_bytes);bm_u64(&h,p->blocks512);bm_u64(&h,p->startup_bytes);bm_u64(&h,p->bootstrap_bytes);
    for(uint32_t i=0;i<p->archive_count;i++){
        const struct bw_batch_metadata_archive *a=&p->archives[i];bm_u32(&h,a->original_file_id);bm_text(&h,a->path);
        bm_u64(&h,a->info.size);bm_u64(&h,a->info.inode);bm_u64(&h,a->info.mtime_ticks);bm_u64(&h,a->info.ctime_ticks);bm_u32(&h,a->info.attributes);
        bm_u64(&h,a->declared_bytes);bm_u64(&h,a->unique_bytes);bm_u64(&h,a->allocation_bytes);bm_u64(&h,a->blocks512);
    }
    for(uint32_t i=0;i<p->span_count;i++){
        const struct bw_batch_metadata_span *s=&p->spans[i];bm_u32(&h,s->archive_index);bm_u32(&h,s->kind);bm_u64(&h,s->span.offset);bm_u64(&h,s->span.size);cp_sha_update(&h,s->span.sha1,20);cp_sha_update(&h,s->span.sha256,32);
    }
    for(uint32_t i=0;i<p->range_count;i++){bm_u32(&h,p->ranges[i].archive_index);bm_u64(&h,p->ranges[i].offset);bm_u64(&h,p->ranges[i].size);}
    cp_sha_final(&h,p->seed_sha256);
}
int bw_batch_metadata_build(const struct bw_batch_facts *f,uint32_t unit,struct bw_batch_metadata_plan *out,const char **error){
    if(error)*error=NULL;if(!out)return bm_bad(error,"metadata_output_required");memset(out,0,sizeof *out);
    if(!f||unit<BW_BATCH_METADATA_MIN_UNIT||unit>BW_BATCH_METADATA_MAX_UNIT||(unit&(unit-1u)))return bm_bad(error,"metadata_arguments");
    const char *check_error=NULL;if(db_check(f,&check_error))return bm_bad(error,"metadata_facts_invalid");
    if(bw_pbb_prefix_mode(f->bootstrap.prefix)!=5)return bm_bad(error,"metadata_mode5_required");
    struct bw_manifest base;if(bw_manifest_open(&base,f->base_map,f->base_map_size))return bm_bad(error,"metadata_base_map_invalid");
    struct bw_batch_metadata_plan *p=calloc(1,sizeof *p);
    struct bw_batch_metadata_range *rounded=calloc(BW_BATCH_METADATA_MAX_SPANS,sizeof *rounded);
    if(!p||!rounded){free(p);free(rounded);return bm_bad(error,"metadata_no_memory");}
    int rc=-1;p->version=BW_BATCH_METADATA_PLAN_VERSION;p->allocation_unit=unit;p->original=f->original;p->prepared=f->prepared;p->archive_count=f->bootstrap.archive_count;
    memcpy(p->native_snapshot_sha256,f->bootstrap.native_snapshot_sha256,32);memcpy(p->facts_sha256,f->facts_sha256,32);memcpy(p->base_map_sha256,f->base_map_sha256,32);memcpy(p->map_sha256,f->map_sha256,32);
    for(uint32_t i=0;i<p->archive_count;i++){
        const struct bw_pbb_archive *a=&f->bootstrap.archives[i];struct bw_batch_metadata_archive *copy=&p->archives[i];
        copy->original_file_id=bm_original(&base,a);if(copy->original_file_id==UINT32_MAX)goto done;
        memcpy(copy->path,a->path,strlen(a->path)+1u);copy->info=a->info;
        for(unsigned kind=0;kind<BW_PBB_SPANS;kind++){
            const struct bw_pbb_span *s=&a->spans[kind];if(!s->size)continue;
            if(p->span_count>=BW_BATCH_METADATA_MAX_SPANS||s->offset>a->info.size||s->size>a->info.size-s->offset||s->size>BW_PBB_MAX_METADATA_SEED-p->declared_bytes)goto done;
            uint32_t at=p->span_count++;p->spans[at].archive_index=i;p->spans[at].kind=kind;p->spans[at].span=*s;
            p->ranges[at]=(struct bw_batch_metadata_range){.archive_index=i,.offset=s->offset,.size=s->size};p->declared_bytes+=s->size;copy->declared_bytes+=s->size;
        }
    }
    if(!p->span_count||p->declared_bytes>BW_PROVIDER_MAX_LOCAL_BYTES-f->local_bytes||p->declared_bytes>BW_BATCH_BOOTSTRAP_LIMIT-f->bootstrap_bytes)goto done;
    p->startup_bytes=f->local_bytes+p->declared_bytes;p->bootstrap_bytes=f->bootstrap_bytes+p->declared_bytes;
    p->range_count=bm_merge(p->ranges,p->span_count);
    for(uint32_t i=0;i<p->range_count;i++){
        const struct bw_batch_metadata_range *r=&p->ranges[i];
        if(r->size>BW_PBB_MAX_METADATA_SEED-p->unique_bytes)goto done;p->unique_bytes+=r->size;p->archives[r->archive_index].unique_bytes+=r->size;
        uint64_t end=r->offset+r->size,mask=(uint64_t)unit-1u;if(end>UINT64_MAX-mask)goto done;
        uint64_t first=r->offset&~mask,last=(end+mask)&~mask;
        rounded[i]=(struct bw_batch_metadata_range){.archive_index=r->archive_index,.offset=first,.size=last-first};
    }
    uint32_t blocks=bm_merge(rounded,p->range_count);
    for(uint32_t i=0;i<blocks;i++){if(rounded[i].size>UINT64_MAX-p->allocation_bytes)goto done;p->allocation_bytes+=rounded[i].size;p->archives[rounded[i].archive_index].allocation_bytes+=rounded[i].size;}
    if(p->allocation_bytes>BW_PROVIDER_MAX_LOCAL_BYTES-f->local_bytes||p->allocation_bytes>BW_BATCH_BOOTSTRAP_LIMIT-f->bootstrap_bytes)goto done;
    p->blocks512=p->allocation_bytes/512u;for(uint32_t i=0;i<p->archive_count;i++)p->archives[i].blocks512=p->archives[i].allocation_bytes/512u;bm_hash(p);*out=*p;rc=0;
done:free(rounded);free(p);return rc?bm_bad(error,"metadata_bounds_or_original_stream_required"):0;
}
