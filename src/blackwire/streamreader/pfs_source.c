/* Read-only unsigned contiguous PFS namespace/range adapter. Format references
 * and admitted variants are pinned in docs/COMPRESSED-SOURCES.md. */
#include "pfs_source.h"
#include <stdlib.h>
#include <string.h>

#define FILE_TYPE 0x8000u
#define DIR_TYPE 0x4000u
#define TYPE_MASK 0xf000u
#define COMPRESSED 1u
#define MISSING UINT32_MAX
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|(uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p)|(uint64_t)le32(p+4)<<32; }
static int span(uint64_t at,uint64_t size,uint64_t limit) { return at<=limit && size<=limit-at; }
static unsigned fold(unsigned c) { return c>='A'&&c<='Z'?c+32:c; }
static int name_equal(const char *a,const char *b) {
    while (*a && *b) { if (fold((uint8_t)*a++)!=fold((uint8_t)*b++)) return 0; }
    return *a==*b;
}
static int metadata(struct bw_pfs *p,uint64_t at,void *out,size_t size) {
    uint8_t *dest=out;
    if (!span(at,size,p->size)) return BW_PFS_RANGE;
    while (size) {
        uint64_t page=at/sizeof p->cache*sizeof p->cache;
        size_t within=(size_t)(at-page),take;
        if (!p->cache_valid || p->cached_page!=page) {
            uint64_t left=p->size-page;
            p->cache_valid=0;
            p->cached_bytes=left<sizeof p->cache?(size_t)left:sizeof p->cache;
            if (p->pread(p->source,page,p->cache,p->cached_bytes)) return BW_PFS_IO;
            p->cached_page=page;p->cache_valid=1;
        }
        if (within>=p->cached_bytes) return BW_PFS_RANGE;
        take=p->cached_bytes-within;if (take>size) take=size;
        memcpy(dest,p->cache+within,take);dest+=take;at+=take;size-=take;
    }
    return 0;
}
static int inode_read(struct bw_pfs *p,uint32_t id,struct bw_pfs_inode *out) {
    uint8_t bytes[BW_PFS_INODE_BYTES];
    if (!out || id>=p->inode_count) return BW_PFS_ARGUMENT;
    uint64_t per=p->block_size/BW_PFS_INODE_BYTES;
    uint64_t at=(1+id/per)*p->block_size+(id%per)*BW_PFS_INODE_BYTES;
    int result=metadata(p,at,bytes,sizeof bytes);if (result) return result;
    memset(out,0,sizeof *out);
    out->id=id;out->mode=le16(bytes);out->links=le16(bytes+2);out->flags=le32(bytes+4);
    uint64_t size=le64(bytes+8),compressed=le64(bytes+16);
    out->stored_size=out->flags&COMPRESSED?size:compressed;
    out->logical_size=out->flags&COMPRESSED?compressed:size;
    out->blocks=le32(bytes+0x60);
    uint32_t start=le32(bytes+0x64);
    unsigned type=out->mode&TYPE_MASK;
    if ((type!=FILE_TYPE && type!=DIR_TYPE) || !out->links ||
        out->stored_size>BW_PFS_MAX_VOLUME || out->logical_size>BW_PFS_MAX_VOLUME ||
        (!(out->flags&COMPRESSED) && size!=compressed)) return BW_PFS_FORMAT;
    if (type==DIR_TYPE && (out->flags&COMPRESSED)) return BW_PFS_UNSUPPORTED;
    if (!out->blocks) return out->logical_size || out->stored_size?BW_PFS_FORMAT:0;
    if (start>INT32_MAX || start<1+p->inode_blocks || start>=p->blocks ||
        out->blocks>p->blocks-start || out->stored_size>(uint64_t)out->blocks*p->block_size)
        return BW_PFS_FORMAT;
    for (size_t offset=0x68;offset<BW_PFS_INODE_BYTES;offset+=4) {
        uint32_t pointer=le32(bytes+offset);
        if (pointer && pointer!=UINT32_MAX) return BW_PFS_UNSUPPORTED;
    }
    out->base=(uint64_t)start*p->block_size;
    return span(out->base,(uint64_t)out->blocks*p->block_size,p->size)?0:BW_PFS_RANGE;
}
int bw_pfs_inode_get(struct bw_pfs *p,uint32_t id,struct bw_pfs_inode *out) {
    return !p || !p->ready?BW_PFS_ARGUMENT:inode_read(p,id,out);
}
struct dirent { uint32_t inode,type,size; char name[256]; };
static int entry(struct bw_pfs *p,uint64_t base,uint64_t length,uint64_t at,struct dirent *out) {
    uint8_t head[16],name[256];
    memset(out,0,sizeof *out);
    if (at==length) return 1;
    if (!span(at,sizeof head,length)) return BW_PFS_FORMAT;
    int result=metadata(p,base+at,head,sizeof head);if (result) return result;
    uint32_t id=le32(head),type=le32(head+4),name_size=le32(head+8),size=le32(head+12);
    if (!(id|type|name_size|size)) return 1;
    if (id>=p->inode_count || size<24 || size%8 || !span(at,size,length) ||
        !name_size || name_size>255 || name_size>=size-16) return BW_PFS_FORMAT;
    result=metadata(p,base+at+16,name,name_size+1);if (result) return result;
    if (name[name_size] || memchr(name,0,name_size) || memchr(name,'/',name_size)) return BW_PFS_PATH;
    memcpy(out->name,name,name_size);out->name[name_size]=0;
    out->inode=id;out->type=type;out->size=size;
    if (strcmp(out->name,".") && strcmp(out->name,"..") &&
        bw_manifest_validate_path(out->name,name_size)!=BW_MANIFEST_OK) return BW_PFS_PATH;
    return 0;
}
struct extent { uint64_t first,last; };
static int compare_extents(const void *left,const void *right) {
    const struct extent *a=left,*b=right;
    return a->first<b->first?-1:a->first>b->first?1:0;
}
int bw_pfs_open(struct bw_pfs *p,bw_pfs_pread_fn read_source,void *context,uint64_t size) {
    uint8_t head[0x400]; struct extent *extents=NULL;
    uint32_t count=0;int result=BW_PFS_FORMAT;
    if (!p) return BW_PFS_ARGUMENT;
    memset(p,0,sizeof *p);
    if (!read_source || size<0x400 || size>BW_PFS_MAX_VOLUME) return BW_PFS_ARGUMENT;
    p->pread=read_source;p->source=context;p->size=size;
    result=metadata(p,0,head,sizeof head);if (result) return result;
    if (le64(head)!=2 || le64(head+8)!=20130315) return BW_PFS_FORMAT;
    uint16_t mode=le16(head+0x1c);
    if (mode&~8u) return BW_PFS_UNSUPPORTED;
    p->case_insensitive=!!(mode&8u);
    p->block_size=le32(head+0x20);
    uint64_t inodes=le64(head+0x30),blocks=le64(head+0x38),inode_blocks=le64(head+0x40);
    if (p->block_size<4096 || p->block_size>65536 || (p->block_size&(p->block_size-1)) ||
        !inodes || inodes>BW_PFS_MAX_INODES || !blocks || blocks>size/p->block_size ||
        !inode_blocks || inode_blocks>=blocks) return BW_PFS_FORMAT;
    uint64_t per=p->block_size/BW_PFS_INODE_BYTES;
    if (inode_blocks!=(inodes+per-1)/per || inode_blocks+2>blocks) return BW_PFS_FORMAT;
    p->inode_count=(uint32_t)inodes;p->inode_blocks=inode_blocks;p->blocks=blocks;
    extents=calloc(p->inode_count,sizeof *extents);if (!extents) return BW_PFS_MEMORY;
    for (uint32_t id=0;id<p->inode_count;id++) {
        struct bw_pfs_inode node;
        result=inode_read(p,id,&node);if (result) goto done;
        if ((node.mode&TYPE_MASK)==DIR_TYPE && node.logical_size>BW_PFS_MAX_DIRECTORY) { result=BW_PFS_LIMIT;goto done; }
        if (node.blocks) { extents[count].first=node.base;extents[count++].last=node.base+(uint64_t)node.blocks*p->block_size; }
    }
    qsort(extents,count,sizeof *extents,compare_extents);
    for (uint32_t i=1;i<count;i++) if (extents[i].first<extents[i-1].last) { result=BW_PFS_FORMAT;goto done; }
    struct bw_pfs_inode super;
    result=inode_read(p,0,&super);if (result) goto done;
    if ((super.mode&TYPE_MASK)!=DIR_TYPE || super.base!=(1+inode_blocks)*p->block_size || super.logical_size!=p->block_size) { result=BW_PFS_FORMAT;goto done; }
    p->root=p->flat_table=p->collisions=MISSING;
    for (uint64_t at=0;at<super.logical_size;) {
        struct dirent item;
        result=entry(p,super.base,super.logical_size,at,&item);
        if (result==1) { result=0;break; }
        if (result) goto done;
        uint32_t *target;
        if (!strcmp(item.name,"uroot") && item.type==3) target=&p->root;
        else if (!strcmp(item.name,"flat_path_table") && item.type==2) target=&p->flat_table;
        else if (!strcmp(item.name,"collision_resolver") && item.type==2) target=&p->collisions;
        else { result=BW_PFS_UNSUPPORTED;goto done; }
        if (*target!=MISSING || item.inode==0) { result=BW_PFS_FORMAT;goto done; }
        *target=item.inode;at+=item.size;
    }
    if (p->root==MISSING || p->flat_table==MISSING || p->root==p->flat_table ||
        p->root==p->collisions || p->flat_table==p->collisions) { result=BW_PFS_FORMAT;goto done; }
    struct bw_pfs_inode root;
    result=inode_read(p,p->root,&root);if (result) goto done;
    if ((root.mode&TYPE_MASK)!=DIR_TYPE) { result=BW_PFS_FORMAT;goto done; }
    p->ready=1;result=0;
done:
    free(extents);if (result) { p->ready=0;p->cache_valid=0; }
    return result;
}
struct name_slot { uint64_t hash; uint32_t parent_plus_one,at; };
struct frame { struct bw_pfs_inode inode; uint64_t at; uint32_t parent;size_t prefix;unsigned dots; };
static uint64_t name_hash(uint32_t parent,const char *name) {
    uint64_t hash=UINT64_C(14695981039346656037)^parent;
    while (*name) hash=(hash^fold((uint8_t)*name++))*UINT64_C(1099511628211);
    return hash;
}
static int unique_name(struct bw_pfs *p,struct name_slot *slots,size_t mask,
                       uint32_t parent,uint32_t at,const char *name) {
    uint64_t hash=name_hash(parent,name);size_t slot=(size_t)hash&mask;
    for (size_t tries=0;tries<=mask;tries++,slot=(slot+1)&mask) {
        struct name_slot *old=slots+slot;
        if (!old->parent_plus_one) { old->hash=hash;old->parent_plus_one=parent+1;old->at=at;return 0; }
        if (old->hash==hash && old->parent_plus_one==parent+1) {
            struct bw_pfs_inode directory;struct dirent item;
            int result=inode_read(p,parent,&directory);if (result) return result;
            result=entry(p,directory.base,directory.logical_size,old->at,&item);
            if (result) return result==1?BW_PFS_FORMAT:result;
            if (name_equal(name,item.name)) return BW_PFS_PATH;
        }
    }
    return BW_PFS_LIMIT;
}
int bw_pfs_walk(struct bw_pfs *p,bw_pfs_visit_fn visit,void *context,uint32_t *file_count) {
    struct frame frames[BW_PFS_MAX_DEPTH+1];char path[BW_MANIFEST_MAX_PATH+1];
    uint8_t *visited=NULL;struct name_slot *names=NULL;
    size_t capacity=1;unsigned depth=0;uint32_t files=0,steps=0;
    int result=BW_PFS_ARGUMENT;
    if (file_count) *file_count=0;
    if (!p || !p->ready) return result;
    p->namespace_validated=0;
    while (capacity<(size_t)p->inode_count*2) capacity*=2;
    names=calloc(capacity,sizeof *names);visited=calloc((p->inode_count+7)/8,1);
    if (!names || !visited) { result=BW_PFS_MEMORY;goto done; }
    memset(frames,0,sizeof frames);path[0]=0;
    result=inode_read(p,p->root,&frames[0].inode);if (result) goto done;
    frames[0].parent=p->root;visited[p->root/8]|=(uint8_t)(1u<<(p->root%8));
    for (;;) {
        struct frame *frame=frames+depth;struct dirent item;
        uint64_t at=frame->at;
        result=entry(p,frame->inode.base,frame->inode.logical_size,at,&item);
        if (result==1) {
            if (frame->dots!=3) { result=BW_PFS_FORMAT;goto done; }
            if (!depth) break;
            depth--;path[frames[depth].prefix]=0;continue;
        }
        if (result) goto done;
        if (++steps>p->inode_count*3u+64u) { result=BW_PFS_LIMIT;goto done; }
        frame->at+=item.size;
        if (!strcmp(item.name,".") || !strcmp(item.name,"..")) {
            unsigned bit=item.name[1]?2:1;
            if ((frame->dots&bit) || item.type!=(bit==1?4u:5u) ||
                item.inode!=(bit==1?frame->inode.id:frame->parent)) { result=BW_PFS_FORMAT;goto done; }
            frame->dots|=bit;continue;
        }
        if (item.type!=2 && item.type!=3) { result=BW_PFS_UNSUPPORTED;goto done; }
        if (!item.inode || item.inode==p->flat_table || item.inode==p->collisions ||
            (visited[item.inode/8]&(1u<<(item.inode%8)))) { result=BW_PFS_FORMAT;goto done; }
        visited[item.inode/8]|=(uint8_t)(1u<<(item.inode%8));
        result=unique_name(p,names,capacity-1,frame->inode.id,(uint32_t)at,item.name);if (result) goto done;
        size_t length=strlen(item.name),prefix=frame->prefix;
        if (prefix+!!prefix+length>BW_MANIFEST_MAX_PATH) { result=BW_PFS_PATH;goto done; }
        if (prefix) path[prefix++]='/';
        memcpy(path+prefix,item.name,length+1);
        struct bw_pfs_inode node;
        result=inode_read(p,item.inode,&node);if (result) goto done;
        if ((item.type==2 && (node.mode&TYPE_MASK)!=FILE_TYPE) ||
            (item.type==3 && (node.mode&TYPE_MASK)!=DIR_TYPE)) { result=BW_PFS_FORMAT;goto done; }
        if (item.type==3) {
            if (depth>=BW_PFS_MAX_DEPTH) { result=BW_PFS_LIMIT;goto done; }
            struct frame *next=frames+(++depth);memset(next,0,sizeof *next);
            next->inode=node;next->parent=frame->inode.id;next->prefix=prefix+length;
        } else {
            if (node.links!=1) { result=BW_PFS_UNSUPPORTED;goto done; }
            files++;
            if (visit && visit(context,path,&node)) { result=BW_PFS_CALLBACK;goto done; }
            path[frame->prefix]=0;
        }
    }
    p->namespace_validated=1;result=0;if (file_count) *file_count=files;
done:
    free(names);free(visited);return result;
}
int bw_pfs_find(struct bw_pfs *p,const char *path,struct bw_pfs_inode *out) {
    if (!p || !p->ready || !path || !out) return BW_PFS_ARGUMENT;
    size_t total=0;while (total<=BW_MANIFEST_MAX_PATH && path[total]) total++;
    if (bw_manifest_validate_path(path,total)!=BW_MANIFEST_OK) return BW_PFS_PATH;
    int result;
    if (!p->namespace_validated && (result=bw_pfs_walk(p,NULL,NULL,NULL))) return result;
    struct bw_pfs_inode directory;
    result=inode_read(p,p->root,&directory);if (result) return result;
    const char *component=path;
    for (;;) {
        const char *slash=strchr(component,'/');
        size_t length=slash?(size_t)(slash-component):strlen(component);
        char name[256];memcpy(name,component,length);name[length]=0;
        uint32_t found=MISSING;
        for (uint64_t at=0;at<directory.logical_size;) {
            struct dirent item;result=entry(p,directory.base,directory.logical_size,at,&item);
            if (result==1) break;
            if (result) return result;
            at+=item.size;
            if (p->case_insensitive?name_equal(name,item.name):!strcmp(name,item.name)) {
                if (found!=MISSING) return BW_PFS_PATH;
                found=item.inode;
            }
        }
        if (found==MISSING) return BW_PFS_NOT_FOUND;
        result=inode_read(p,found,out);if (result) return result;
        if (!slash) return (out->mode&TYPE_MASK)==FILE_TYPE?0:BW_PFS_NOT_FOUND;
        if ((out->mode&TYPE_MASK)!=DIR_TYPE) return BW_PFS_NOT_FOUND;
        directory=*out;component=slash+1;
    }
}
int bw_pfs_read_file(struct bw_pfs *p,uint32_t id,uint64_t at,void *output,size_t size) {
    struct bw_pfs_inode node;
    if (!p || !p->ready || (!output && size) || size>1024u*1024u) return BW_PFS_ARGUMENT;
    int result=inode_read(p,id,&node);if (result) return result;
    if ((node.mode&TYPE_MASK)!=FILE_TYPE) return BW_PFS_ARGUMENT;
    if (node.flags&COMPRESSED) return BW_PFS_UNSUPPORTED;
    if (!span(at,size,node.logical_size)) return BW_PFS_RANGE;
    return size && p->pread(p->source,node.base+at,output,size)?BW_PFS_IO:0;
}
const char *bw_pfs_error(int error) {
    switch (error) {
    case BW_PFS_OK:return "success";
    case BW_PFS_ARGUMENT:return "invalid argument";
    case BW_PFS_FORMAT:return "invalid PFS metadata or overlapping extents";
    case BW_PFS_IO:return "source read failed";
    case BW_PFS_UNSUPPORTED:return "unsupported PFS variant or inode layout";
    case BW_PFS_RANGE:return "range exceeds PFS source";
    case BW_PFS_LIMIT:return "PFS metadata limit exceeded";
    case BW_PFS_MEMORY:return "PFS metadata allocation failed";
    case BW_PFS_PATH:return "invalid or ambiguous PFS path";
    case BW_PFS_NOT_FOUND:return "file not found in PFS";
    case BW_PFS_CALLBACK:return "PFS visitor stopped";
    default:return "unknown PFS error";
    }
}
