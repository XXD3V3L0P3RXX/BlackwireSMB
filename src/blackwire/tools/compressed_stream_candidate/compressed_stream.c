#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "compressed_stream.h"
#include "../../streamreader/copy_sha256.h"
#include <string.h>

_Static_assert(sizeof(struct bw_compressed_stream)<=4096,"Bounded binding metadata, no second data cache");

static int latch(struct bw_compressed_stream *p,int error) {
    p->failed=1;p->error=error?error:BW_CSTREAM_STATE;return p->error;
}
static int same_stat(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b) {
    return a->smb2_type==SMB2_TYPE_FILE && b->smb2_type==SMB2_TYPE_FILE &&
        a->smb2_size==b->smb2_size && a->smb2_ino==b->smb2_ino &&
        a->smb2_mtime==b->smb2_mtime && a->smb2_mtime_nsec==b->smb2_mtime_nsec &&
        a->smb2_ctime==b->smb2_ctime && a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
static int binding_valid(const struct bw_source_binding *b) {
    if(!b || strnlen(b->source,sizeof b->source)==sizeof b->source ||
       bw_manifest_validate_path(b->source,strlen(b->source)) ||
       strnlen(b->title,sizeof b->title)!=9 || memcmp(b->title,"PPSA",4) ||
       strnlen(b->game,sizeof b->game)!=74 || memcmp(b->game,b->title,9) || b->game[9]!='_')return 0;
    for(unsigned i=4;i<9;i++)if(b->title[i]<'0'||b->title[i]>'9')return 0;
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(b->game[10+i*2]!=hex[b->sha256[i]>>4]||b->game[11+i*2]!=hex[b->sha256[i]&15])return 0;
    return 1;
}
static int stable(struct bw_compressed_stream *p) {
    const struct bw_container_smb *s=p->image;
    const struct bw_container *c=&s->container;
    const struct bw_source_binding *b=p->live_binding;
    if(!binding_valid(b)||memcmp(b->sha256,p->binding.sha256,32)||strcmp(b->source,p->binding.source)||
       strcmp(b->game,p->binding.game)||strcmp(b->title,p->binding.title))return latch(p,BW_CSTREAM_IDENTITY);
    if(s->smb!=p->smb||s->handle!=p->handle||!s->handle||s->failed||!same_stat(&s->admitted,&p->admitted)||
       strnlen(s->path,sizeof s->path)==sizeof s->path||strcmp(s->path,p->binding.source)||
       c->session!=p->session||!c->ready||c->failed||c->source!=s||c->image_size!=p->admitted.smb2_size||
       c->kind!=p->kind||c->has_pfsc!=p->has_pfsc||c->ops.pread!=p->ops.pread||c->ops.generation!=p->ops.generation)
        return latch(p,BW_CONTAINER_STALE);
    if((c->kind==BW_CONTAINER_PFS&&c->filesystem!=&c->outer)||
       ((c->kind==BW_CONTAINER_PFSC_PFS||c->kind==BW_CONTAINER_WRAPPED_PFS)&&c->filesystem!=&c->inner))return latch(p,BW_CONTAINER_STALE);
    if(p->files->files!=p->tokens||p->files->source!=&s->container||p->files->session!=p->session||
       p->files->failed||p->files->count!=p->map.count||p->files->manifest_checksum!=p->map.checksum||
       memcmp(p->map_bytes,p->map_header,sizeof p->map_header))return latch(p,BW_CSTREAM_MAP);
    return 0;
}
static int current(struct bw_compressed_stream *p) {
    if(p->failed)return p->error;
    int result=stable(p);if(result)return result;
    result=p->current(p->opaque);
    if(result)return latch(p,result>0?BW_CSTREAM_CANCELLED:BW_CSTREAM_IDENTITY);
    result=stable(p);if(result)return result;
    result=bw_container_validate(&p->image->container);
    if(result)return latch(p,result);
    return stable(p);
}
static int enter(struct bw_compressed_stream *p) {
    if(!p||!p->ready)return BW_CSTREAM_STATE;
    if(p->busy)return BW_CSTREAM_BUSY;
    if(p->failed)return p->error;
    p->busy=1;return 0;
}
static int row(struct bw_compressed_stream *p,uint32_t id,struct bw_manifest_record *r,struct bw_container_file *token) {
    if(id>=p->map.count)return BW_PFS_RANGE;
    if(bw_manifest_get(&p->map,id,r))return latch(p,BW_CSTREAM_MAP);
    if(r->flags!=BW_MANIFEST_STREAM)return r->flags==BW_MANIFEST_LOCAL?BW_CSTREAM_NOT_STREAM:latch(p,BW_CSTREAM_MAP);
    const struct bw_container_file *f=&p->tokens[id];
    if(f->owner!=&p->image->container||f->session!=p->session||f->size!=r->size||f->inode>=p->image->container.filesystem->inode_count)
        return latch(p,BW_CSTREAM_MAP);
    *token=*f;return 0;
}
static int row_same(struct bw_compressed_stream *p,uint32_t id,const struct bw_manifest_record *before,const struct bw_container_file *token,uint64_t path_hash) {
    struct bw_manifest_record r;struct bw_container_file f;int result=row(p,id,&r,&f);
    if(result)return result==BW_CSTREAM_NOT_STREAM?latch(p,BW_CSTREAM_MAP):result;
    if(r.size!=before->size||r.path_length!=before->path_length||r.mtime_sec!=before->mtime_sec||
       bw_manifest_fnv1a64(r.path,r.path_length)!=path_hash||f.owner!=token->owner||f.session!=token->session||f.size!=token->size||f.inode!=token->inode)
        return latch(p,BW_CSTREAM_MAP);
    return 0;
}
static int read_exact(struct bw_compressed_stream *p,uint32_t id,uint64_t at,void *out,size_t size) {
    int result=current(p);if(result)return result;
    struct bw_manifest_record r;struct bw_container_file token;
    result=row(p,id,&r,&token);if(result)return result;
    if(at>r.size||size>r.size-at)return BW_PFS_RANGE;
    uint64_t path_hash=bw_manifest_fnv1a64(r.path,r.path_length);
    result=bw_container_file_read(&token,at,out,size);
    if(result)return latch(p,result);
    result=current(p);if(result)return result;
    return row_same(p,id,&r,&token,path_hash);
}
static int load(void *opaque,uint32_t id,uint64_t block,uint8_t *out,size_t capacity,size_t *length) {
    struct bw_compressed_stream *p=opaque;*length=0;
    if(!out||capacity!=BW_CACHE_BLOCK)return BW_PFS_ARGUMENT;
    if(block>UINT64_MAX/BW_CACHE_BLOCK)return BW_PFS_RANGE;
    int result=current(p);if(result)return result;
    struct bw_manifest_record r;struct bw_container_file token;
    result=row(p,id,&r,&token);if(result)return result;
    uint64_t at=block*BW_CACHE_BLOCK;
    if(at>=r.size)return BW_PFS_RANGE;
    uint64_t left=r.size-at;size_t take=left<capacity?(size_t)left:capacity;
    result=read_exact(p,id,at,out,take);if(result)return result;
    *length=take;return 0;
}
int bw_compressed_stream_bind(struct bw_compressed_stream *p,const struct bw_cstream_admission *a) {
    if(!p||p->ready||p->image||p->failed)return BW_CSTREAM_STATE;
    if(!a||!a->image||!a->files||!a->binding||!a->current||!a->map_bytes||!a->expected_binding_sha256||!a->expected_map_sha256)
        return latch(p,BW_CSTREAM_STATE);
    struct bw_container_smb *s=a->image;struct bw_container *c=&s->container;
    if(!binding_valid(a->binding)||memcmp(a->binding->sha256,a->expected_binding_sha256,32)||
       strnlen(s->path,sizeof s->path)==sizeof s->path||strcmp(s->path,a->binding->source))return latch(p,BW_CSTREAM_IDENTITY);
    if(!s->smb||!s->handle||s->failed||!c->ready||c->failed||!c->session||c->source!=s||
       c->image_size!=s->admitted.smb2_size||!c->ops.pread||!c->ops.generation)return latch(p,BW_CONTAINER_STALE);
    if((c->kind!=BW_CONTAINER_PFS&&c->kind!=BW_CONTAINER_PFSC_PFS&&c->kind!=BW_CONTAINER_WRAPPED_PFS)||
       (c->kind==BW_CONTAINER_PFSC_PFS?!c->has_pfsc:c->has_pfsc))return latch(p,BW_PFS_UNSUPPORTED);
    if(a->map_size<BW_MANIFEST_HEADER_SIZE||a->map_size>BW_MANIFEST_HEADER_SIZE+(size_t)BW_MANIFEST_MAX_FILES*BW_MANIFEST_RECORD_SIZE+BW_MANIFEST_MAX_PATH_BYTES||
       bw_manifest_open(&p->map,a->map_bytes,a->map_size))return latch(p,BW_CSTREAM_MAP);
    uint8_t digest[32];cp_sha_bytes(a->map_bytes,a->map_size,digest);
    if(memcmp(digest,a->expected_map_sha256,32))return latch(p,BW_CSTREAM_MAP);
    if(!a->files->files||a->files->failed||a->files->source!=c||a->files->session!=c->session||
       a->files->count!=p->map.count||a->files->manifest_checksum!=p->map.checksum)return latch(p,BW_CSTREAM_MAP);
    if(a->cache&&(!a->cache->_entries||!a->cache->_storage||a->cache->resident_count||a->cache->hits||a->cache->misses||a->cache->evictions||a->cache->loaded_bytes))
        return latch(p,BW_CSTREAM_CACHE);
    for(uint32_t i=0;i<p->map.count;i++){
        struct bw_manifest_record r;const struct bw_container_file *f=&a->files->files[i];
        if(bw_manifest_get(&p->map,i,&r)||f->owner!=c||f->session!=c->session||f->size!=r.size)return latch(p,BW_CSTREAM_MAP);
    }
    p->image=s;p->files=a->files;p->tokens=a->files->files;p->live_binding=a->binding;p->binding=*a->binding;
    p->map_bytes=a->map_bytes;p->map_size=a->map_size;memcpy(p->map_header,a->map_bytes,sizeof p->map_header);memcpy(p->map_sha256,digest,32);
    p->smb=s->smb;p->handle=s->handle;p->admitted=s->admitted;p->session=c->session;p->kind=c->kind;p->has_pfsc=c->has_pfsc;p->ops=c->ops;
    p->current=a->current;p->opaque=a->opaque;p->cache=a->cache;p->ready=1;p->busy=1;
    int result=current(p);p->busy=0;return result;
}
int bw_compressed_stream_current(struct bw_compressed_stream *p) {
    int result=enter(p);if(result)return result;result=current(p);p->busy=0;return result;
}
int bw_compressed_stream_read(struct bw_compressed_stream *p,uint32_t id,uint64_t at,void *out,size_t size) {
    if((!out&&size)||size>BW_PFSC_MAX_READ)return BW_PFS_ARGUMENT;
    int result=enter(p);if(result)return result;result=read_exact(p,id,at,out,size);p->busy=0;return result;
}
int bw_compressed_stream_load_block(void *opaque,uint32_t id,uint64_t block,uint8_t *out,size_t capacity,size_t *length) {
    if(length)*length=0;if(!length)return BW_PFS_ARGUMENT;
    struct bw_compressed_stream *p=opaque;int result=enter(p);if(result)return result;
    result=load(p,id,block,out,capacity,length);p->busy=0;return result;
}
static int cached_get(struct bw_compressed_stream *p,uint32_t id,uint64_t block,const uint8_t **out,size_t *length) {
    int result=current(p);if(result)return result;
    struct bw_manifest_record r;struct bw_container_file token;
    result=row(p,id,&r,&token);if(result)return result;
    if(block>UINT64_MAX/BW_CACHE_BLOCK||block*BW_CACHE_BLOCK>=r.size)return BW_PFS_RANGE;
    uint64_t path_hash=bw_manifest_fnv1a64(r.path,r.path_length);
    if(bw_cache_get(p->cache,id,block,load,p,out,length))return p->failed?p->error:latch(p,BW_CSTREAM_CACHE);
    uint64_t left=r.size-block*BW_CACHE_BLOCK;
    size_t expected=left<BW_CACHE_BLOCK?(size_t)left:BW_CACHE_BLOCK;
    if(!*out||*length!=expected)return latch(p,BW_CSTREAM_CACHE);
    result=current(p);if(result)return result;
    return row_same(p,id,&r,&token,path_hash);
}
int bw_compressed_stream_cached_get(struct bw_compressed_stream *p,uint32_t id,uint64_t block,const uint8_t **out,size_t *length) {
    if(out)*out=NULL;if(length)*length=0;if(!out||!length)return BW_PFS_ARGUMENT;
    int result=enter(p);if(result)return result;
    if(!p->cache)result=BW_CSTREAM_CACHE;else result=cached_get(p,id,block,out,length);
    if(result){*out=NULL;*length=0;}p->busy=0;return result;
}
int bw_compressed_stream_cached_read(struct bw_compressed_stream *p,uint32_t id,uint64_t at,void *out,size_t size) {
    if((!out&&size)||size>BW_PFSC_MAX_READ)return BW_PFS_ARGUMENT;
    int result=enter(p);if(result)return result;
    if(!p->cache){result=BW_CSTREAM_CACHE;goto done;}
    result=current(p);if(result)goto done;
    struct bw_manifest_record r;struct bw_container_file token;
    result=row(p,id,&r,&token);if(result)goto done;
    if(at>r.size||size>r.size-at){result=BW_PFS_RANGE;goto done;}
    uint64_t path_hash=bw_manifest_fnv1a64(r.path,r.path_length);
    for(size_t used=0;used<size;){
        const uint8_t *data=NULL;size_t available=0;uint64_t position=at+used;
        size_t within=(size_t)(position%BW_CACHE_BLOCK);
        result=cached_get(p,id,position/BW_CACHE_BLOCK,&data,&available);if(result)goto done;
        if(within>=available){result=latch(p,BW_CSTREAM_CACHE);goto done;}
        size_t take=available-within;if(take>size-used)take=size-used;
        memcpy((uint8_t *)out+used,data+within,take);used+=take;
    }
    result=current(p);if(!result)result=row_same(p,id,&r,&token,path_hash);
done:
    p->busy=0;return result;
}
void bw_compressed_stream_destroy(struct bw_compressed_stream *p) {
    if(p&&!p->busy)memset(p,0,sizeof *p);
}
