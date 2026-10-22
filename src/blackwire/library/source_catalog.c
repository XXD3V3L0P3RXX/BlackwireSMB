#include "source_catalog.h"
#include "source_param.h"
#include "../streamreader/copy_sha256.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int bw_source_catalog_available(const struct bw_catalog_item *p){return p&&(p->state==BW_CATALOG_FOLDER||p->state==BW_CATALOG_CONTAINER_READY);}
enum bw_provider_kind bw_source_catalog_kind(const struct bw_catalog_item *p){return p&&(p->state==BW_CATALOG_CONTAINER||p->state==BW_CATALOG_CONTAINER_READY)?BW_PROVIDER_CONTAINER:BW_PROVIDER_FOLDER;}
uint64_t bw_source_catalog_boot_bytes(const struct bw_catalog_item *p){return !p?0:p->state==BW_CATALOG_CONTAINER_READY?p->logical_boot_size:p->boot.size;}
const char *bw_source_catalog_status(const struct bw_catalog_item *p){
    static const char *states[]={"available","unavailable","invalid_metadata","container_unverified","available"};
    if(!p||(unsigned)p->state>BW_CATALOG_CONTAINER_READY)return "unavailable";
    if(p->state==BW_CATALOG_CONTAINER&&p->container_error==BW_PFS_UNSUPPORTED)return "unsupported_format";
    return states[p->state];
}
const char *bw_source_catalog_format(const struct bw_catalog_item *p){
    if(!p)return "";
    switch(p->container_kind){case BW_CONTAINER_PFS:return "pfs";case BW_CONTAINER_PFSC_PFS:return "pfsc_pfs";case BW_CONTAINER_WRAPPED_PFS:return "nested_pfs";default:return "";}
}
int bw_source_catalog_image_same(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b){
    return a&&b&&a->smb2_type==SMB2_TYPE_FILE&&b->smb2_type==SMB2_TYPE_FILE&&a->smb2_ino==b->smb2_ino&&a->smb2_size==b->smb2_size&&
        a->smb2_mtime==b->smb2_mtime&&a->smb2_mtime_nsec==b->smb2_mtime_nsec&&a->smb2_ctime==b->smb2_ctime&&a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
void bw_source_catalog_image_hash(const struct smb2_stat_64 *p,uint8_t out[32]){
    static const char domain[]="Blackwire catalog image generation v1";
    const uint64_t fields[]={p->smb2_type,p->smb2_ino,p->smb2_size,p->smb2_mtime,p->smb2_mtime_nsec,p->smb2_ctime,p->smb2_ctime_nsec};
    struct cp_sha sha;cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
    for(unsigned i=0;i<7;i++){uint8_t bytes[8];for(unsigned j=0;j<8;j++)bytes[j]=(uint8_t)(fields[i]>>(8*j));cp_sha_update(&sha,bytes,8);}
    cp_sha_final(&sha,out);
}
static int live(const struct bw_source_smb *p){struct timespec at;return !p->smb||clock_gettime(CLOCK_MONOTONIC,&at)||((double)at.tv_sec+(double)at.tv_nsec/1e9)>=p->deadline?-1:0;}
static int listed_time(uint64_t ticks,uint64_t seconds,uint64_t nsec){
    const uint64_t epoch=UINT64_C(116444736000000000);
    return ticks>=epoch&&(ticks-epoch)/10000000==seconds&&((ticks-epoch)%10000000)/10==nsec/1000;
}
static int listed_image(const struct bw_source_info *listed,const struct smb2_stat_64 *actual){
    return !(listed->attributes&0x450u)&&actual->smb2_type==SMB2_TYPE_FILE&&listed->inode==actual->smb2_ino&&listed->size==actual->smb2_size&&
        listed_time(listed->mtime_ticks,actual->smb2_mtime,actual->smb2_mtime_nsec)&&listed_time(listed->ctime_ticks,actual->smb2_ctime,actual->smb2_ctime_nsec);
}
static int fail(struct bw_source_catalog *p,const char *error){if(!p->error)p->error=error;return -1;}
struct candidate {char source[1024],name[241];struct bw_source_info info;unsigned depth;};
struct scan {struct bw_source_smb *source;struct bw_source_catalog *catalog;struct candidate *pending;size_t count,capacity;const char *relative;unsigned depth;};
struct logical_metadata {struct bw_source_smb *source;struct bw_pfs_inode boot,param;size_t path_bytes;uint32_t files;int error;};
static int logical(void *opaque,const char *path,const struct bw_pfs_inode *node){
    struct logical_metadata *p=opaque;
    if(live(p->source)){p->error=BW_CONTAINER_STALE;return -1;}
    if(node->flags&1u){p->error=BW_PFS_UNSUPPORTED;return -1;}
    if(bw_source_excluded(path))return 0;
    size_t n=strlen(path)+1;
    if(p->files==4096u||n>1024u*1024u-p->path_bytes){p->error=BW_PFS_LIMIT;return -1;}
    p->files++;p->path_bytes+=n;
    if(!strcmp(path,"eboot.bin"))p->boot=*node;
    if(!strcmp(path,"sce_sys/param.json"))p->param=*node;
    return 0;
}
static int inspect_image(struct scan *scan,const struct candidate *candidate,struct bw_catalog_item *item){
    struct bw_source_smb *source=scan->source;struct bw_container_smb *image=calloc(1,sizeof *image);uint8_t *json=NULL;int result=-1,bad=0;
    if(!image)return fail(scan->catalog,"catalog_image_allocation_failed");
    if(live(source)){bad=fail(scan->catalog,"catalog_image_scan_expired");goto done;}
    if(candidate->info.size<48||candidate->info.size>BW_PFS_MAX_VOLUME){result=BW_PFS_UNSUPPORTED;goto done;}
    result=bw_container_smb_open_owned(image,&source->smb,candidate->source);
    if(result==BW_PFS_IO||result==BW_CONTAINER_STALE){bad=fail(scan->catalog,"catalog_image_generation_or_io_failed");goto done;}
    if(!listed_image(&candidate->info,&image->admitted)){bad=fail(scan->catalog,"catalog_image_listing_changed");goto done;}
    item->image=image->admitted;bw_source_catalog_image_hash(&item->image,item->image_sha256);
    item->container_kind=image->container.kind;
    if(result)goto done;
    struct logical_metadata metadata={.source=source};result=bw_container_walk(&image->container,logical,&metadata,NULL);
    if(metadata.error)result=metadata.error;
    if(result)goto done;
    if(!metadata.boot.id||!metadata.boot.logical_size||metadata.boot.logical_size>UINT64_C(2147483648)||
       !metadata.param.id||!metadata.param.logical_size||metadata.param.logical_size>BW_SOURCE_PARAM_LIMIT){result=BW_PFS_FORMAT;goto done;}
    json=malloc((size_t)metadata.param.logical_size);if(!json){bad=fail(scan->catalog,"catalog_image_param_allocation_failed");goto done;}
    result=bw_container_read(&image->container,"sce_sys/param.json",0,json,(size_t)metadata.param.logical_size);
    if(result)goto done;
    if(bw_source_param_metadata(json,(size_t)metadata.param.logical_size,item->title_id,item->title,sizeof item->title)){
        result=BW_PFS_FORMAT;goto done;
    }
    if(live(source)){bad=fail(scan->catalog,"catalog_image_scan_expired");goto done;}
    result=bw_container_validate(&image->container);if(result)goto done;
    item->logical_boot_size=metadata.boot.logical_size;item->state=BW_CATALOG_CONTAINER_READY;
done:
    item->container_error=result;
    uint64_t requests=image->read_calls+2*image->generation_checks+(image->smb?3u:0u);
    if(image->read_bytes>UINT64_MAX-source->content_bytes||requests>UINT32_MAX-source->requests)bad=fail(scan->catalog,"catalog_image_counter_limit");
    else{source->content_bytes+=image->read_bytes;source->requests+=(uint32_t)requests;}
    if(image->handle&&bw_container_smb_close(image))bad=fail(scan->catalog,"catalog_image_close_failed");
    /* open() closes a refused format itself; retain any close/transport error
     * from that path too, and discard this whole partially scanned library. */
    if(image->last_transport_error)bad=fail(scan->catalog,"catalog_image_transport_failed");
    if(result==BW_PFS_IO||result==BW_CONTAINER_STALE)bad=fail(scan->catalog,"catalog_image_generation_or_io_failed");
    if(result){memset(item->title_id,0,sizeof item->title_id);memset(item->title,0,sizeof item->title);item->logical_boot_size=0;}
    free(json);free(image);return bad;
}
static int component(const char *name){size_t n=strlen(name);return n&&n<=240&&!strchr(name,'/')&&!bw_manifest_validate_path(name,n);}
static int image(const char *name){
    char extension[16];const char *dot=strrchr(name,'.');if(!dot||strlen(dot)>=sizeof extension)return 0;
    for(size_t i=0;i<=strlen(dot);i++){unsigned char c=(unsigned char)dot[i];extension[i]=(char)(c>='A'&&c<='Z'?c+32:c);}
    return !strcmp(extension,".ffpfsc")||!strcmp(extension,".pfsc")||!strcmp(extension,".pfs");
}
static struct bw_catalog_item *item(struct bw_source_catalog *p,const struct candidate *c){
    if(p->count>=BW_CATALOG_MAX_ITEMS){fail(p,"catalog_game_limit");return NULL;}
    if(p->count==p->capacity){size_t n=p->capacity?p->capacity*2:32;void *items=realloc(p->items,n*sizeof *p->items);
        if(!items){fail(p,"catalog_allocation_failed");return NULL;}p->items=items;p->capacity=n;}
    struct bw_catalog_item *out=&p->items[p->count++];memset(out,0,sizeof *out);memcpy(out->source,c->source,strlen(c->source)+1);memcpy(out->name,c->name,strlen(c->name)+1);return out;
}
static int queue(struct scan *p,const struct candidate *c){
    if(p->count>=BW_CATALOG_MAX_COLLECTIONS)return fail(p->catalog,"catalog_collection_limit");
    if(p->count==p->capacity){size_t n=p->capacity?p->capacity*2:32;void *next=realloc(p->pending,n*sizeof *p->pending);
        if(!next)return fail(p->catalog,"catalog_allocation_failed");p->pending=next;p->capacity=n;}
    p->pending[p->count++]=*c;return 0;
}
static int visit(void *opaque,const char *name,const struct bw_source_info *info){
    struct scan *p=opaque;struct bw_source_catalog *out=p->catalog;
    if(++out->entries>16384)return fail(out,"catalog_library_entry_limit");
    if(bw_source_excluded(name)||!component(name)){out->skipped++;return 0;}
    struct candidate c={0};c.info=*info;c.depth=p->depth;
    int n=snprintf(c.source,sizeof c.source,"%s%s%s",p->relative,*p->relative?"/":"",name);
    if(n<0||(size_t)n>=sizeof c.source||bw_manifest_validate_path(c.source,(size_t)n))return fail(out,"catalog_source_path_limit");
    memcpy(c.name,name,strlen(name)+1);
    if(info->attributes&0x440u){struct bw_catalog_item *e=item(out,&c);if(!e)return -1;e->state=BW_CATALOG_BLOCKED;return 0;}
    if(!(info->attributes&0x10u)){
        if(image(name)){struct bw_catalog_item *e=item(out,&c);if(!e)return -1;e->state=BW_CATALOG_CONTAINER;e->container_bytes=info->size;return inspect_image(p,&c,e);}
        else out->skipped++;
        return 0;
    }
    char path[1280];struct bw_source_info boot;
    n=snprintf(path,sizeof path,"%s/eboot.bin",c.source);if(n<0||(size_t)n>=sizeof path)return fail(out,"catalog_source_path_limit");
    int status=bw_source_smb_file_info(p->source,path,&boot);
    if(status<0)return fail(out,p->source->error?p->source->error:"catalog_source_failed");
    if(status==BW_SOURCE_MISSING){
        if(c.depth>=BW_CATALOG_MAX_DEPTH){out->unsearched++;return 0;}
        return queue(p,&c);
    }
    struct bw_catalog_item *e=item(out,&c);if(!e)return -1;
    if(status||!boot.size||boot.size>UINT64_C(2147483648)){e->state=BW_CATALOG_BLOCKED;return 0;}
    e->boot=boot;
    n=snprintf(path,sizeof path,"%s/sce_sys/param.json",c.source);if(n<0||(size_t)n>=sizeof path)return fail(out,"catalog_source_path_limit");
    void *json=NULL;size_t size=0;
    status=bw_source_smb_small_file(p->source,path,BW_SOURCE_PARAM_LIMIT,&json,&size,&e->param);
    if(status<0){free(json);return fail(out,p->source->error?p->source->error:"catalog_source_failed");}
    if(status||bw_source_param_metadata(json,size,e->title_id,e->title,sizeof e->title)){e->state=BW_CATALOG_BAD_PARAM;free(json);return 0;}
    free(json);e->state=BW_CATALOG_FOLDER;
    n=snprintf(path,sizeof path,"%s/sce_sys/icon0.png",c.source);if(n<0||(size_t)n>=sizeof path)return fail(out,"catalog_source_path_limit");
    status=bw_source_smb_file_info(p->source,path,&e->icon);
    if(status<0)return fail(out,p->source->error?p->source->error:"catalog_source_failed");
    e->cover=!status&&e->icon.size&&e->icon.size<=BW_SOURCE_SMALL_FILE_LIMIT;
    return 0;
}
static int compare(const void *a,const void *b){
    const unsigned char *x=(const unsigned char *)((const struct bw_catalog_item *)a)->source,*y=(const unsigned char *)((const struct bw_catalog_item *)b)->source;
    while(*x&&*y){unsigned l=*x++,r=*y++;if(l>='A'&&l<='Z')l+=32;if(r>='A'&&r<='Z')r+=32;if(l!=r)return l<r?-1:1;}
    return *x?1:*y?-1:0;
}
int bw_source_catalog_scan(struct bw_source_smb *source,struct bw_source_catalog *out){
    if(!source||!out)return -1;
    memset(out,0,sizeof *out);struct scan scan={.source=source,.catalog=out,.relative=""};
    int bad=bw_source_smb_list(source,"",visit,&scan);out->collections=1;
    for(size_t i=0;!bad&&i<scan.count;i++){
        /* visit() may grow pending; do not retain a pointer into that array. */
        struct candidate c=scan.pending[i];scan.relative=c.source;scan.depth=c.depth+1;
        if(++out->collections>BW_CATALOG_MAX_COLLECTIONS){bad=fail(out,"catalog_collection_limit");break;}
        bad=bw_source_smb_list(source,c.source,visit,&scan);
    }
    free(scan.pending);
    if(bad){if(!out->error)fail(out,source->error?source->error:"catalog_source_failed");return -1;}
    if(out->count>1)qsort(out->items,out->count,sizeof *out->items,compare);
    for(size_t i=1;i<out->count;i++)if(!compare(&out->items[i-1],&out->items[i]))return fail(out,"catalog_source_case_collision");
    return 0;
}
void bw_source_catalog_destroy(struct bw_source_catalog *p){if(p){free(p->items);memset(p,0,sizeof *p);}}
struct json {char *bytes;size_t size,capacity;int bad;};
static void append(struct json *p,const char *bytes,size_t n){
    if(p->bad)return;
    if(n>16u*1024u*1024u-p->size-1){p->bad=1;return;}
    if(p->size+n+1>p->capacity){size_t next=p->capacity?p->capacity*2:4096;while(next<p->size+n+1)next*=2;
        void *data=realloc(p->bytes,next);if(!data){p->bad=1;return;}p->bytes=data;p->capacity=next;}
    memcpy(p->bytes+p->size,bytes,n);p->size+=n;p->bytes[p->size]=0;
}
static void text(struct json *p,const char *s){append(p,s,strlen(s));}
static void number(struct json *p,uint64_t value){char b[32];int n=snprintf(b,sizeof b,"%llu",(unsigned long long)value);if(n<0||(size_t)n>=sizeof b)p->bad=1;else append(p,b,(size_t)n);}
static void string(struct json *p,const char *s){
    text(p,"\"");for(;*s;s++){unsigned char c=(unsigned char)*s;
        if(c=='"'||c=='\\'){char b[2]={'\\',(char)c};append(p,b,2);}
        else if(c<32){char b[7];snprintf(b,sizeof b,"\\u%04x",c);text(p,b);}
        else append(p,s,1);}
    text(p,"\"");
}
int bw_source_catalog_json(const struct bw_source_catalog *p,char **bytes,size_t *size){
    if(bytes)*bytes=NULL;if(size)*size=0;
    if(!p||!bytes||!size||p->error||p->count>BW_CATALOG_MAX_ITEMS||p->count>p->capacity||(p->count&&!p->items))return -1;
    struct json json={0};text(&json,"{\"items\":[");
    for(size_t i=0;i<p->count;i++){
        const struct bw_catalog_item *e=&p->items[i];if((unsigned)e->state>BW_CATALOG_CONTAINER_READY){json.bad=1;break;}
        if(i)text(&json,",");text(&json,"{\"source\":");string(&json,e->source);text(&json,",\"name\":");string(&json,e->source);text(&json,",\"folderName\":");string(&json,e->name);
        text(&json,",\"title\":");string(&json,*e->title?e->title:e->name);text(&json,",\"titleId\":");string(&json,e->title_id);
        text(&json,",\"sourceKind\":");string(&json,bw_source_catalog_kind(e)==BW_PROVIDER_CONTAINER?"container":"folder");text(&json,",\"status\":");string(&json,bw_source_catalog_status(e));
        text(&json,",\"sourcePolicy\":");number(&json,bw_source_catalog_available(e)?BW_PROVIDER_POLICY_V1:0);
        text(&json,",\"formatValidated\":");text(&json,e->state==BW_CATALOG_CONTAINER_READY?"true":"false");text(&json,",\"containerFormat\":");string(&json,bw_source_catalog_format(e));
        char generation[65]={0};if(e->state==BW_CATALOG_CONTAINER_READY)for(unsigned j=0;j<32;j++)snprintf(generation+2*j,3,"%02x",e->image_sha256[j]);
        text(&json,",\"imageGenerationSha256\":");string(&json,generation);
        text(&json,",\"bootBytes\":");number(&json,bw_source_catalog_boot_bytes(e));text(&json,",\"containerBytes\":");number(&json,e->container_bytes);text(&json,",\"cover\":");text(&json,e->cover?"true":"false");text(&json,"}");
    }
    text(&json,"],\"collections\":");number(&json,p->collections);text(&json,",\"entries\":");number(&json,p->entries);text(&json,",\"skipped\":");number(&json,p->skipped);text(&json,",\"unsearched\":");number(&json,p->unsearched);text(&json,"}");
    if(json.bad){free(json.bytes);return -1;}*bytes=json.bytes;*size=json.size;return 0;
}
