#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "source_snapshot.h"
#include "source_plan.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>
struct bw_source_snapshot_entry {size_t offset;const char *path;struct bw_source_info info;};
static int fail(struct bw_source_snapshot *p,const char *error){if(!p->error)p->error=error;return -1;}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int path_compare(const char *a,const char *b){
    while(*a&&*b){unsigned x=fold((uint8_t)*a++),y=fold((uint8_t)*b++);if(x!=y)return x<y?-1:1;}
    return *a?1:*b?-1:0;
}
static int compare(const void *a,const void *b){return path_compare(((const struct bw_source_snapshot_entry *)a)->path,((const struct bw_source_snapshot_entry *)b)->path);}
int bw_source_snapshot_init(struct bw_source_snapshot *p,const char *game){
    if(!p)return -1;
    memset(p,0,sizeof *p);struct bw_source_plan plan;
    int bad=bw_source_plan_init(&plan,game);if(bad)fail(p,"invalid_snapshot_game");else{memcpy(p->game,plan.game,sizeof p->game);memcpy(p->source,game,strlen(game)+1);}
    bw_source_plan_destroy(&plan);return bad;
}
int bw_source_snapshot_init_bound(struct bw_source_snapshot *p,const char *source,const char *game){
    if(bw_source_snapshot_init(p,game))return -1;
    if(!source||strnlen(source,BW_MANIFEST_MAX_PATH+1)>BW_MANIFEST_MAX_PATH||bw_manifest_validate_path(source,strlen(source)))return fail(p,"invalid_snapshot_source");
    unsigned depth=1;for(const char *at=source;*at;at++)if(*at=='/'&&++depth>=BW_SOURCE_SNAPSHOT_MAX_PARENTS)return fail(p,"snapshot_source_parent_limit");
    memcpy(p->source,source,strlen(source)+1);return 0;
}
int bw_source_snapshot_add(struct bw_source_snapshot *p,const char *path,const struct bw_source_info *info){
    if(!p||!path||!info)return -1;
    if(p->error||p->sealed)return fail(p,"source_snapshot_is_not_mutable");
    size_t n=strlen(path);
    if((n&&bw_manifest_validate_path(path,n))||(!n&&!(info->attributes&0x10u))||(info->attributes&0x440u)||
        p->count>=BW_SOURCE_SNAPSHOT_MAX_ENTRIES||n+1>BW_SOURCE_SNAPSHOT_MAX_PATH_BYTES-p->path_size)return fail(p,"source_snapshot_invalid_entry_or_limit");
    if(p->count==p->capacity){size_t size=p->capacity?p->capacity*2:128;if(size>BW_SOURCE_SNAPSHOT_MAX_ENTRIES)size=BW_SOURCE_SNAPSHOT_MAX_ENTRIES;
        void *entries=realloc(p->entries,size*sizeof *p->entries);if(!entries)return fail(p,"source_snapshot_allocation_failed");p->entries=entries;p->capacity=size;}
    if(n+1>p->path_capacity-p->path_size){size_t size=p->path_capacity?p->path_capacity*2:4096;
        while(size<p->path_size+n+1)size*=2;
        if(size>BW_SOURCE_SNAPSHOT_MAX_PATH_BYTES)size=BW_SOURCE_SNAPSHOT_MAX_PATH_BYTES;
        void *paths=realloc(p->paths,size);if(!paths)return fail(p,"source_snapshot_allocation_failed");p->paths=paths;p->path_capacity=size;}
    p->entries[p->count++]=(struct bw_source_snapshot_entry){p->path_size,NULL,*info};memcpy(p->paths+p->path_size,path,n+1);p->path_size+=n+1;return 0;
}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
int bw_source_snapshot_finish(struct bw_source_snapshot *p){
    if(!p||p->error||p->sealed)return -1;
    if(!p->count)return fail(p,"source_snapshot_has_no_root");
    if(p->parent_count>BW_SOURCE_SNAPSHOT_MAX_PARENTS)return fail(p,"source_snapshot_parent_limit");
    for(size_t i=0;i<p->count;i++)p->entries[i].path=p->paths+p->entries[i].offset;
    qsort(p->entries,p->count,sizeof *p->entries,compare);
    if(*p->entries[0].path)return fail(p,"source_snapshot_has_no_root");
    struct cp_sha hash;cp_sha_init(&hash);uint8_t header[20]={0};memcpy(header,"BWSRC001",8);put32(header+8,1);put32(header+12,(uint32_t)p->count);put32(header+16,(uint32_t)strlen(p->game));
    cp_sha_update(&hash,header,sizeof header);cp_sha_update(&hash,p->game,strlen(p->game));
    if(p->parent_count||strcmp(p->source,p->game)){
        uint8_t bound[16]={0};memcpy(bound,"BWBOUND1",8);put32(bound+8,(uint32_t)strlen(p->source));put32(bound+12,p->parent_count);
        cp_sha_update(&hash,bound,sizeof bound);cp_sha_update(&hash,p->source,strlen(p->source));
        for(unsigned i=0;i<p->parent_count;i++){
            const struct bw_source_info *info=&p->parents[i];if(!(info->attributes&0x10u)||(info->attributes&0x440u))return fail(p,"source_snapshot_invalid_parent");
            uint8_t row[36];put32(row,info->attributes);put64(row+4,info->size);put64(row+12,info->inode);put64(row+20,info->mtime_ticks);put64(row+28,info->ctime_ticks);cp_sha_update(&hash,row,sizeof row);
        }
    }
    for(size_t i=0;i<p->count;i++){
        const struct bw_source_snapshot_entry *e=&p->entries[i];
        if(i&&!path_compare(e[-1].path,e->path))return fail(p,"source_snapshot_path_collision");
        /* A file cannot also be an ancestor, even if that directory is empty. */
        char ancestor[1024];size_t n=strlen(e->path);memcpy(ancestor,e->path,n+1);
        for(char *slash=strrchr(ancestor,'/');slash;slash=strrchr(ancestor,'/')){
            *slash=0;struct bw_source_snapshot_entry key={0};key.path=ancestor;
            const struct bw_source_snapshot_entry *found=bsearch(&key,p->entries,p->count,sizeof *p->entries,compare);
            if(!found||!(found->info.attributes&0x10u))return fail(p,"source_snapshot_missing_or_file_ancestor");
        }
        uint8_t row[40];put32(row,(uint32_t)n);put32(row+4,e->info.attributes);put64(row+8,e->info.size);put64(row+16,e->info.inode);put64(row+24,e->info.mtime_ticks);put64(row+32,e->info.ctime_ticks);
        cp_sha_update(&hash,row,sizeof row);cp_sha_update(&hash,e->path,n);
    }
    cp_sha_final(&hash,p->sha256);p->sealed=1;return 0;
}
const struct bw_source_info *bw_source_snapshot_find(const struct bw_source_snapshot *p,const char *path){
    if(!p||!p->sealed||p->error||!path)return NULL;
    struct bw_source_snapshot_entry key={0};key.path=path;
    const struct bw_source_snapshot_entry *found=bsearch(&key,p->entries,p->count,sizeof *p->entries,compare);
    return found&&!strcmp(found->path,path)?&found->info:NULL;
}
int bw_source_snapshot_visit(const struct bw_source_snapshot *p,bw_source_visit_fn visit,void *opaque){
    if(!visit||bw_source_snapshot_validate(p))return -1;
    for(size_t i=0;i<p->count;i++)if(visit(opaque,p->entries[i].path,&p->entries[i].info))return -1;
    return bw_source_snapshot_validate(p);
}
int bw_source_snapshot_equal(const struct bw_source_snapshot *a,const struct bw_source_snapshot *b){return a&&b&&a->sealed&&b->sealed&&!a->error&&!b->error&&a->count==b->count&&a->parent_count==b->parent_count&&!strcmp(a->game,b->game)&&!strcmp(a->source,b->source)&&!memcmp(a->sha256,b->sha256,32);}
void bw_source_snapshot_destroy(struct bw_source_snapshot *p){if(p){free(p->entries);free(p->paths);memset(p,0,sizeof *p);}}

int bw_source_snapshot_validate(const struct bw_source_snapshot *p){
    if(!p||p->sealed!=1||p->error||!p->entries||!p->paths||!p->count||
       p->count>p->capacity||p->capacity>BW_SOURCE_SNAPSHOT_MAX_ENTRIES||
       !p->path_size||p->path_size>p->path_capacity||p->path_capacity>BW_SOURCE_SNAPSHOT_MAX_PATH_BYTES||
       p->parent_count>BW_SOURCE_SNAPSHOT_MAX_PARENTS)return -1;
    size_t game=strnlen(p->game,sizeof p->game),source=strnlen(p->source,sizeof p->source);
    if(!game||game==sizeof p->game||strchr(p->game,'/')||bw_manifest_validate_path(p->game,game)||
       !source||source==sizeof p->source||bw_manifest_validate_path(p->source,source))return -1;
    unsigned components=1;for(size_t i=0;i<source;i++)if(p->source[i]=='/')components++;
    if(components>=BW_SOURCE_SNAPSHOT_MAX_PARENTS||
       (p->parent_count?p->parent_count!=components:strcmp(p->source,p->game)!=0))return -1;
    for(unsigned i=0;i<p->parent_count;i++)if(!(p->parents[i].attributes&0x10u)||(p->parents[i].attributes&0x440u))return -1;
    size_t covered=0;
    for(size_t i=0;i<p->count;i++){
        const struct bw_source_snapshot_entry *e=&p->entries[i];
        if(e->offset>=p->path_size||e->path!=p->paths+e->offset||
           (e->offset&&p->paths[e->offset-1]))return -1;
        size_t room=p->path_size-e->offset,limit=room<BW_MANIFEST_MAX_PATH+1?room:BW_MANIFEST_MAX_PATH+1;
        size_t n=strnlen(e->path,limit);
        if(n==limit||(n&&bw_manifest_validate_path(e->path,n))||
           (!n&&!(e->info.attributes&0x10u))||(e->info.attributes&0x440u)||n+1>p->path_size-covered)return -1;
        unsigned depth=0;for(size_t j=0;j<n;j++)if(e->path[j]=='/')depth++;
        if(n)depth++;
        if(p->parent_count+depth>BW_SOURCE_SNAPSHOT_MAX_PARENTS-((e->info.attributes&0x10u)?1u:0u))return -1;
        if((i&&path_compare(e[-1].path,e->path)>=0)||(!i&&n))return -1;
        covered+=n+1;
    }
    if(covered!=p->path_size)return -1;
    struct cp_sha hash;cp_sha_init(&hash);uint8_t header[20]={0};
    memcpy(header,"BWSRC001",8);put32(header+8,1);put32(header+12,(uint32_t)p->count);put32(header+16,(uint32_t)game);
    cp_sha_update(&hash,header,sizeof header);cp_sha_update(&hash,p->game,game);
    if(p->parent_count||strcmp(p->source,p->game)){
        uint8_t bound[16]={0};memcpy(bound,"BWBOUND1",8);put32(bound+8,(uint32_t)source);put32(bound+12,p->parent_count);
        cp_sha_update(&hash,bound,sizeof bound);cp_sha_update(&hash,p->source,source);
        for(unsigned i=0;i<p->parent_count;i++){
            const struct bw_source_info *info=&p->parents[i];uint8_t row[36];
            put32(row,info->attributes);put64(row+4,info->size);put64(row+12,info->inode);put64(row+20,info->mtime_ticks);put64(row+28,info->ctime_ticks);cp_sha_update(&hash,row,sizeof row);
        }
    }
    for(size_t i=0;i<p->count;i++){
        const struct bw_source_snapshot_entry *e=&p->entries[i];size_t n=strlen(e->path);char ancestor[BW_MANIFEST_MAX_PATH+1];
        memcpy(ancestor,e->path,n+1);
        for(char *slash=strrchr(ancestor,'/');slash;slash=strrchr(ancestor,'/')){
            *slash=0;struct bw_source_snapshot_entry key={0};key.path=ancestor;
            const struct bw_source_snapshot_entry *found=bsearch(&key,p->entries,p->count,sizeof *p->entries,compare);
            if(!found||strcmp(found->path,ancestor)||!(found->info.attributes&0x10u))return -1;
        }
        uint8_t row[40];put32(row,(uint32_t)n);put32(row+4,e->info.attributes);put64(row+8,e->info.size);put64(row+16,e->info.inode);put64(row+24,e->info.mtime_ticks);put64(row+32,e->info.ctime_ticks);
        cp_sha_update(&hash,row,sizeof row);cp_sha_update(&hash,e->path,n);
    }
    uint8_t actual[32];cp_sha_final(&hash,actual);return memcmp(actual,p->sha256,32)?-1:0;
}
