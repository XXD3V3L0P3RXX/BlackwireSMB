#define _POSIX_C_SOURCE 200809L
#include "startup_runtime_control_internal.h"
#include "native_document.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
enum {STATUS=1,VERSION=2,CAPS=4,PATH=8,RUNTIME=16,TITLE=32,SOURCE=64,MANAGED=128,MOUNTED=256,PKG=512,INSTALLED=1024,IMAGE=2048,AVAILABLE=4096,PLATFORM=8192,PATHS=16384,COUNT=32768};
struct parser {
    enum bw_src_operation operation;const char *root,*title;struct bw_src_reply out;
    unsigned seen,key_count,cap_count;size_t count;char keys[128][128],caps[128][96];
};
void bw_src_reply_clear(struct bw_src_reply *p){if(p){for(size_t i=0;i<p->path_count;i++)free(p->paths[i]);free(p->paths);memset(p,0,sizeof *p);}}
static int path(void *opaque,const void *bytes,size_t size){
    struct parser *p=opaque;if(p->out.path_count>=BW_SRC_PATH_LIMIT)return -1;
    char value[BW_SRC_PATH_BYTES];if(bw_document_string(bytes,size,value,sizeof value)||value[0]!='/')return -1;
    for(const unsigned char *s=(const unsigned char *)value;*s;s++)if(*s<32||*s==127||*s=='\\')return -1;
    for(size_t i=0;i<p->out.path_count;i++)if(!strcmp(value,p->out.paths[i]))return -1;
    if(!p->out.paths){p->out.paths=calloc(BW_SRC_PATH_LIMIT,sizeof *p->out.paths);if(!p->out.paths)return -1;}
    char *copy=strdup(value);if(!copy)return -1;p->out.paths[p->out.path_count++]=copy;return 0;
}
static int boolean(const void *bytes,size_t size,unsigned *out){if(size==4&&!memcmp(bytes,"true",4)){*out=1;return 0;}if(size==5&&!memcmp(bytes,"false",5)){*out=0;return 0;}return -1;}
static int exact_string(const void *bytes,size_t size,const char *expected){char value[2048];return bw_document_string(bytes,size,value,sizeof value)||strcmp(value,expected)?-1:0;}
static int capability(void *opaque,const void *bytes,size_t size){
    struct parser *p=opaque;if(p->cap_count>=128)return -1;char *value=p->caps[p->cap_count];if(bw_document_string(bytes,size,value,96)||!*value)return -1;
    for(const unsigned char *s=(const unsigned char *)value;*s;s++)if(*s<32||*s>=127)return -1;
    for(unsigned i=0;i<p->cap_count;i++)if(!strcmp(value,p->caps[i]))return -1;p->cap_count++;
    if(!strcmp(value,"game_info"))p->out.capabilities|=1;if(!strcmp(value,"unmount_game"))p->out.capabilities|=2;
    if(!strcmp(value,"manage_settings"))p->out.capabilities|=4;if(!strcmp(value,"list_manual_sources"))p->out.capabilities|=8;return 0;
}
static int field(void *opaque,const char *key,size_t n,const void *bytes,size_t size){
    struct parser *p=opaque;if(!n||n>=128||p->key_count>=128||memchr(key,0,n))return -1;
    for(size_t i=0;i<n;i++)if((unsigned char)key[i]<32||(unsigned char)key[i]>=127)return -1;
    for(unsigned i=0;i<p->key_count;i++)if(strlen(p->keys[i])==n&&!memcmp(key,p->keys[i],n))return -1;
    memcpy(p->keys[p->key_count],key,n);p->keys[p->key_count++][n]=0;
    unsigned bit=0;
#define KEY(s) (n==sizeof(s)-1&&!memcmp(key,s,sizeof(s)-1))
    if(KEY("status"))bit=STATUS;else if(KEY("api_version"))bit=VERSION;else if(KEY("capabilities"))bit=CAPS;
    else if(KEY("path"))bit=PATH;else if(KEY("runtime_path"))bit=RUNTIME;else if(KEY("title_id"))bit=TITLE;else if(KEY("source_type"))bit=SOURCE;
    else if(KEY("managed"))bit=MANAGED;else if(KEY("mounted"))bit=MOUNTED;else if(KEY("installed_pkg"))bit=PKG;else if(KEY("installed"))bit=INSTALLED;
    else if(KEY("image_backed"))bit=IMAGE;else if(KEY("source_available"))bit=AVAILABLE;else if(KEY("platform"))bit=PLATFORM;
    else if((p->operation==BW_SRC_SETTINGS&&KEY("scan_paths"))||(p->operation==BW_SRC_MANUAL&&KEY("paths")))bit=PATHS;
    else if((p->operation==BW_SRC_SETTINGS&&KEY("scan_path_count"))||(p->operation==BW_SRC_MANUAL&&KEY("count")))bit=COUNT;
#undef KEY
    if(!bit){
        /* Installed api1 info/version additions are scalar metadata. Do not
         * admit unknown objects with unobserved duplicate/nested authority. */
        if(p->operation==BW_SRC_UNMOUNT||p->operation==BW_SRC_MANUAL||!size||((const unsigned char *)bytes)[0]=='{'||((const unsigned char *)bytes)[0]=='[')return -1;return 0;
    }
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==STATUS)return size!=1||memcmp(bytes,"0",1)?-1:0;
    if(bit==VERSION)return size!=1||memcmp(bytes,"1",1)?-1:0;
    if(bit==CAPS)return p->operation!=BW_SRC_VERSION?-1:bw_document_array(bytes,size,capability,p);
    if(bit==PATHS)return bw_document_array(bytes,size,path,p);
    if(bit==COUNT){
        const unsigned char *s=bytes;size_t v=0;if(!size||size>3||(size>1&&s[0]=='0'))return -1;
        for(size_t i=0;i<size;i++){unsigned d=(unsigned)s[i]-'0';if(d>9||v>(BW_SRC_PATH_LIMIT-d)/10)return -1;v=v*10+d;}p->count=v;return 0;
    }
    if(bit==PATH||bit==RUNTIME)return exact_string(bytes,size,p->root);
    if(bit==TITLE)return exact_string(bytes,size,p->title);
    if(bit==SOURCE)return exact_string(bytes,size,"folder");
    if(bit==PLATFORM)return exact_string(bytes,size,"ps5");
    unsigned value;if(boolean(bytes,size,&value))return -1;
    if(bit==MOUNTED){p->out.mounted=value;return 0;}
    if(bit==AVAILABLE){p->out.source_available=value;return 0;}
    return ((bit==MANAGED||bit==INSTALLED)?!value:value)?-1:0;
}
int bw_src_reply_parse(enum bw_src_operation operation,const char *root,const char *title,const void *bytes,size_t size,struct bw_src_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(operation<BW_SRC_VERSION||operation>BW_SRC_MANUAL||!root||!title||!bytes||!size||size>BW_SRC_DOCUMENT_LIMIT)return -1;
    struct parser *p=calloc(1,sizeof *p);if(!p)return -1;p->operation=operation;p->root=root;p->title=title;int result=-1;
    if(bw_document_fields(bytes,size,field,p))goto done;
    unsigned required=operation==BW_SRC_VERSION?(STATUS|VERSION|CAPS):operation==BW_SRC_INFO?(STATUS|PATH|RUNTIME|TITLE|SOURCE|MANAGED|MOUNTED|PKG|INSTALLED|IMAGE|AVAILABLE|PLATFORM):operation==BW_SRC_UNMOUNT?(STATUS|TITLE|MOUNTED):(STATUS|PATHS|COUNT);
    if(p->seen!=required||(operation==BW_SRC_VERSION&&p->out.capabilities!=15)||(operation==BW_SRC_UNMOUNT&&p->out.mounted)||((operation==BW_SRC_SETTINGS||operation==BW_SRC_MANUAL)&&p->count!=p->out.path_count))goto done;
    *out=p->out;memset(&p->out,0,sizeof p->out);result=0;
done:bw_src_reply_clear(&p->out);free(p);return result;
}
