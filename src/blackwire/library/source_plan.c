#include "source_plan.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct bw_source_entry { uint32_t path,flags;uint64_t size,mtime; };
struct ordered { const char *path;const struct bw_source_entry *entry; };
static int fail(struct bw_source_plan *p,const char *message) {
    if(p&&!p->error)p->error=message;
    return -1;
}
/* Python's classification uses str.lower(), while manifest ordering uses
 * ASCII bytes only. Kelvin sign is the Unicode lowercase mapping into ASCII
 * relevant to these fixed ASCII classification tokens. Never fold paths. */
static size_t lower_token(char out[256],const char *text,size_t size) {
    size_t at=0;
    for(size_t i=0;i<size;i++){
        unsigned char c=(unsigned char)text[i];
        if(i+2<size&&c==0xe2&&(unsigned char)text[i+1]==0x84&&(unsigned char)text[i+2]==0xaa){c='k';i+=2;}
        if(c>='A'&&c<='Z')c=(unsigned char)(c+32);
        out[at++]=(char)c;
    }
    out[at]=0;return at;
}
static int member(const char *name,const char *const *choices,size_t count) {
    for(size_t i=0;i<count;i++)if(!strcmp(name,choices[i]))return 1;
    return 0;
}
static int suffix(const char *s,size_t length,const char *ending) {
    size_t n=strlen(ending);return n<=length&&!memcmp(s+length-n,ending,n);
}
int bw_source_excluded(const char *path) {
    static const char *const exact[]={"eboot.bin_org","eboot.bin.tmp","blackwire.files","blackwire.nodes","ampr_emu.index","ampr_commands.bin","apr_emu.log"};
    static const char *const endings[]={".bak",".backup",".orig",".old",".esbak","~"};
    static const char *const prefixes[]={"blackwire.files.","ampr_emu.index.","blackwire.generation."};
    if(!path)return 0;
    for(const char *at=path;*at;){
        const char *end=strchr(at,'/');size_t n=end?(size_t)(end-at):strlen(at);
        if(n<=255){char lower[256];n=lower_token(lower,at,n);
            if(member(lower,exact,sizeof exact/sizeof exact[0]))return 1;
            for(size_t i=0;i<sizeof endings/sizeof endings[0];i++)if(suffix(lower,n,endings[i]))return 1;
            for(size_t i=0;i<sizeof prefixes/sizeof prefixes[0];i++)if(!strncmp(lower,prefixes[i],strlen(prefixes[i])))return 1;
        }
        if(!end)break;
        at=end+1;
    }
    return 0;
}
int bw_source_startup_metadata(const char *path,uint64_t size) {
    if(!path||size>64u*1024u*1024u)return 0;
    size_t length=0;while(length<=BW_MANIFEST_MAX_PATH&&path[length])length++;
    if(length>BW_MANIFEST_MAX_PATH||bw_manifest_validate_path(path,length)||bw_source_excluded(path))return 0;
    const char *base=strrchr(path,'/');base=base?base+1:path;
    size_t n=strlen(base);if(n>255)return 0;char leaf[256];
    for(size_t i=0;i<=n;i++)leaf[i]=base[i]>='A'&&base[i]<='Z'?(char)(base[i]+('a'-'A')):base[i];
    if(n>4&&suffix(leaf,n,".cat"))return 1;
    if(!strcmp(leaf,"initfs"))return 1;
    if(n>7&&!memcmp(leaf,"initfs_",7)){
        for(size_t i=7;i<n;i++){
            unsigned char c=(unsigned char)leaf[i];
            if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
        }
        return 1;
    }
    return 0;
}
static int grow(void **memory,size_t *capacity,size_t need,size_t maximum) {
    if(need>maximum)return -1;
    if(need<=*capacity)return 0;
    size_t next=*capacity?*capacity:4096;
    if(next>maximum)next=maximum;
    while(next<need){if(next>maximum/2){next=maximum;break;}next*=2;}
    void *fresh=realloc(*memory,next);if(!fresh)return -1;*memory=fresh;*capacity=next;return 0;
}
int bw_source_plan_init_policy(struct bw_source_plan *p,const char *game,enum bw_source_plan_policy policy) {
    if(!p)return -1;
    memset(p,0,sizeof *p);
    if(policy!=BW_SOURCE_PLAN_LEGACY&&policy!=BW_SOURCE_PLAN_COMPANIONS_V1)return fail(p,"invalid_source_plan_policy");
    if(!game||!*game||strlen(game)>240||strchr(game,'/')||bw_manifest_validate_path(game,strlen(game)))return fail(p,"invalid_game_component");
    memcpy(p->game,game,strlen(game)+1);p->policy=policy;return 0;
}
int bw_source_plan_init(struct bw_source_plan *p,const char *game){return bw_source_plan_init_policy(p,game,BW_SOURCE_PLAN_LEGACY);}
int bw_source_companion_path_namespace(const char *path){
    if(!path)return -1;const char *slash=strchr(path,'/');size_t n=slash?(size_t)(slash-path):strlen(path);
    if(n>255)return 0;char lower[256];lower_token(lower,path,n);unsigned ns=!strcmp(lower,"fakelib")?1u:!strcmp(lower,"fakelib2")?2u:0u;
    if(!ns)return 0;const char *canonical=ns==1?"fakelib":"fakelib2";
    return strlen(canonical)==n&&!memcmp(path,canonical,n)?(int)ns:-1;
}
int bw_source_plan_node(struct bw_source_plan *p,const char *path,int directory){
    if(!p||p->error||p->sealed||!path||!p->game[0])return -1;
    if(p->policy==BW_SOURCE_PLAN_LEGACY)return 0;
    int ns=bw_source_companion_path_namespace(path);
    if(ns<0)return fail(p,"source_companion_namespace_alias");
    if(ns&&!strchr(path,'/')&&!directory)return fail(p,"source_companion_root_is_not_directory");
    static const char *const reserved[]={"blackwire.companions","source.original.files","source.original.digests","blackwire-reader.selection","blackwire-reader.share","blackwire-reader.projection"};
    for(const char *at=path;*at;){const char *end=strchr(at,'/');size_t n=end?(size_t)(end-at):strlen(at);
        if(n<=255){char lower[256];lower_token(lower,at,n);if(member(lower,reserved,sizeof reserved/sizeof reserved[0]))return fail(p,"reserved_source_companion_material");}
        if(!end)break;at=end+1;
    }return 0;
}
static uint32_t classify(const char *path,uint64_t size,int *bootstrap) {
    static const char *const modules[]={".self",".prx",".sprx",".elf"};
    static const char *const sidecars[]={".args",".ini",".cfg",".json",".xml",".utoc",".toc",".sig",".rds",".locres",".locmeta",".uproject",".uplugin"};
    static const char *const archives[]={".pak",".ucas",".cas",".wad",".texpack",".audiopack",".lodpack",".fdata",".file",".xxx",".psf",".bank",".bk2",".bik",".mp4",".wem",".wav",".ogg"};
    const char *base=strrchr(path,'/');base=base?base+1:path;
    const char *dot=strrchr(base,'.');char extension[256];extension[0]=0;
    if(dot&&dot!=base&&dot[1])lower_token(extension,dot,strlen(dot));
    int required=!strcmp(path,"eboot.bin")||!strncmp(path,"sce_sys/",8)||!strncmp(path,"sce_module/",11)||member(extension,modules,sizeof modules/sizeof modules[0]);
    *bootstrap=!required&&(bw_source_startup_metadata(path,size)||(member(extension,sidecars,sizeof sidecars/sizeof sidecars[0])&&size<=64u*1024u*1024u)||
        (!strchr(path,'/')&&!member(extension,archives,sizeof archives/sizeof archives[0])&&size<32u*1024u*1024u));
    return required||*bootstrap||!size?BW_MANIFEST_LOCAL:BW_MANIFEST_STREAM;
}
int bw_source_plan_add(struct bw_source_plan *p,const char *path,uint64_t size,uint64_t mtime) {
    if(!p||p->error)return -1;
    if(p->sealed||!path||!p->game[0])return fail(p,"planner_not_open");
    size_t length=strlen(path);
    if(length>BW_MANIFEST_MAX_PATH)return fail(p,"source_path_limit");
    if(bw_source_excluded(path))return 1;
    if(bw_manifest_validate_path(path,length)||sizeof("/data/blackwire/games/")-1+strlen(p->game)+1+length>=1024)return fail(p,"invalid_source_path");
    if(size>INT64_MAX||mtime>INT64_MAX)return fail(p,"invalid_source_metadata");
    if(bw_source_plan_node(p,path,0))return -1;
    if(p->count>=BW_MANIFEST_MAX_FILES||length+1>BW_MANIFEST_MAX_PATH_BYTES-p->paths_size)return fail(p,"source_namespace_limit");
    if(grow((void **)&p->entries,&p->entries_capacity,((size_t)p->count+1)*sizeof *p->entries,BW_MANIFEST_MAX_FILES*sizeof *p->entries)||
        grow((void **)&p->paths,&p->paths_capacity,p->paths_size+length+1,BW_MANIFEST_MAX_PATH_BYTES))return fail(p,"planner_memory_limit");
    int bootstrap;uint32_t flags=classify(path,size,&bootstrap);(void)bootstrap;
    if(p->policy==BW_SOURCE_PLAN_COMPANIONS_V1&&bw_source_companion_path_namespace(path)>0)flags=BW_MANIFEST_LOCAL;
    p->entries[p->count++]=(struct bw_source_entry){(uint32_t)p->paths_size,flags,size,mtime};
    memcpy(p->paths+p->paths_size,path,length+1);p->paths_size+=length+1;return 0;
}
static unsigned fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int compare(const void *a,const void *b) {
    const unsigned char *left=(const unsigned char *)((const struct ordered *)a)->path,*right=(const unsigned char *)((const struct ordered *)b)->path;
    while(*left&&fold(*left)==fold(*right)){left++;right++;}
    unsigned x=fold(*left),y=fold(*right);return x<y?-1:x>y;
}
static void put32(uint8_t *out,uint32_t n){for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t *out,uint64_t n){for(unsigned i=0;i<8;i++)out[i]=(uint8_t)(n>>(i*8));}
int bw_source_plan_finish(struct bw_source_plan *p,struct bw_source_map *map) {
    if(!p||!map)return -1;
    memset(map,0,sizeof *map);
    if(p->sealed||p->error||!p->count)return fail(p,"incomplete_source_plan");
    p->sealed=1;
    struct ordered *order=malloc((size_t)p->count*sizeof *order);
    if(!order)return fail(p,"planner_memory_limit");
    for(uint32_t i=0;i<p->count;i++)order[i]=(struct ordered){p->paths+p->entries[i].path,&p->entries[i]};
    qsort(order,p->count,sizeof *order,compare);
    map->size=BW_MANIFEST_HEADER_SIZE+(size_t)p->count*BW_MANIFEST_RECORD_SIZE+p->paths_size;
    map->bytes=calloc(map->size,1);int has_eboot=0,bad=!map->bytes;size_t offset=0;
    if(bad)goto done;
    memcpy(map->bytes,BW_MANIFEST_MAGIC,8);put32(map->bytes+8,2);put32(map->bytes+12,BW_MANIFEST_RECORD_SIZE);put32(map->bytes+16,p->count);put64(map->bytes+24,p->paths_size);
    for(uint32_t i=0;i<p->count;i++){
        const struct bw_source_entry *entry=order[i].entry;const char *path=order[i].path;size_t length=strlen(path);
        uint8_t *row=map->bytes+BW_MANIFEST_HEADER_SIZE+(size_t)i*BW_MANIFEST_RECORD_SIZE;
        put32(row,(uint32_t)offset);put32(row+4,(uint32_t)length);put32(row+8,entry->flags);put64(row+16,entry->size);put64(row+24,entry->mtime);
        memcpy(map->bytes+BW_MANIFEST_HEADER_SIZE+(size_t)p->count*BW_MANIFEST_RECORD_SIZE+offset,path,length+1);offset+=length+1;
        if(!strcmp(path,"eboot.bin")&&entry->size)has_eboot=1;
        uint64_t *total=entry->flags==BW_MANIFEST_LOCAL?&map->local_bytes:&map->remote_bytes;
        if(entry->size>UINT64_MAX-*total){bad=1;break;}*total+=entry->size;
        if(entry->flags==BW_MANIFEST_LOCAL)map->local_count++;else map->stream_count++;
        int bootstrap;classify(path,entry->size,&bootstrap);
        if(bootstrap){if(entry->size>UINT64_MAX-map->bootstrap_bytes){bad=1;break;}map->bootstrap_bytes+=entry->size;}
    }
    if(!has_eboot||map->bootstrap_bytes>256u*1024u*1024u)bad=1;
    if(!bad){put64(map->bytes+32,bw_manifest_fnv1a64(map->bytes+BW_MANIFEST_HEADER_SIZE,map->size-BW_MANIFEST_HEADER_SIZE));
        struct bw_manifest checked;if(bw_manifest_open(&checked,map->bytes,map->size))bad=1;}
done:
    free(order);
    if(bad){bw_source_map_destroy(map);return fail(p,"invalid_or_overbudget_source_plan");}
    return 0;
}
void bw_source_plan_destroy(struct bw_source_plan *p){if(p){free(p->entries);free(p->paths);memset(p,0,sizeof *p);}}
void bw_source_map_destroy(struct bw_source_map *p){if(p){free(p->bytes);memset(p,0,sizeof *p);}}
