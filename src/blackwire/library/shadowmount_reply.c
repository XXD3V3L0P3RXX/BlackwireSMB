#include "shadowmount_client.h"
#include "native_document.h"
#include <string.h>
enum {STATUS=1,VERSION=2,CAPS=4,COUNT=8,PATHS=16,PATH=32,PRESENT=64,CHANGED=128,QUEUED=256,RESET=512};
struct parser {enum bw_sm_operation kind;const char *root;struct bw_sm_reply next;unsigned seen,items;};
static int integer(const void *value,size_t size,uint32_t max,uint32_t *out){
    const unsigned char *p=value;if(!size||(size>1&&p[0]=='0'))return -1;uint32_t n=0;
    for(size_t i=0;i<size;i++){if(p[i]<'0'||p[i]>'9'||(uint32_t)(p[i]-'0')>max||n>(max-(p[i]-'0'))/10)return -1;n=n*10+(p[i]-'0');}*out=n;return 0;
}
static int boolean(const void *value,size_t size,unsigned *out){
    if(size==4&&!memcmp(value,"true",4)){*out=1;return 0;}if(size==5&&!memcmp(value,"false",5)){*out=0;return 0;}return -1;
}
static int capability(void *opaque,const void *bytes,size_t size){
    struct parser *p=opaque;char text[96];if(++p->items>128||bw_document_string(bytes,size,text,sizeof text))return -1;
    if(!strcmp(text,"list_manual_sources"))p->next.capabilities|=1;
    if(!strcmp(text,"add_manual_source"))p->next.capabilities|=2;
    if(!strcmp(text,"rescan"))p->next.capabilities|=4;
    if(!strcmp(text,"remove_manual_source"))p->next.remove_supported=1;
    if(!strcmp(text,"uninstall_game"))p->next.uninstall_supported=1;
    if(!strcmp(text,"unmount_game"))p->next.unmount_supported=1;
    if(!strcmp(text,"manage_settings"))p->next.settings_supported=1;return 0;
}
static int path_item(void *opaque,const void *bytes,size_t size){
    struct parser *p=opaque;char text[2048];if(++p->items>4096||bw_document_string(bytes,size,text,sizeof text))return -1;
    if(!strcmp(text,p->root))p->next.present=1;return 0;
}
static int field(void *opaque,const char *key,size_t key_size,const void *bytes,size_t size){
    struct parser *p=opaque;unsigned bit=0;uint32_t n;
#define KEY(s) (key_size==sizeof(s)-1&&!memcmp(key,s,sizeof(s)-1))
    if(KEY("status"))bit=STATUS;else if(KEY("api_version"))bit=VERSION;else if(KEY("capabilities"))bit=CAPS;
    else if(KEY("count"))bit=COUNT;else if(KEY("paths"))bit=PATHS;else if(KEY("path"))bit=PATH;
    else if(KEY("present"))bit=PRESENT;else if(KEY("changed"))bit=CHANGED;else if(KEY("queued"))bit=QUEUED;else if(KEY("reset_attempts"))bit=RESET;else return 0;
#undef KEY
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==STATUS)return integer(bytes,size,INT32_MAX,&n)||n?-1:0;
    if(bit==VERSION)return integer(bytes,size,1,&n)||n!=1?-1:0;
    if(bit==CAPS){if(p->kind!=BW_SM_VERSION)return -1;p->items=0;return bw_document_array(bytes,size,capability,p);}
    if(bit==PATHS){if(p->kind!=BW_SM_MANUAL_LIST)return -1;p->items=0;return bw_document_array(bytes,size,path_item,p);}
    if(bit==COUNT)return integer(bytes,size,4096,&p->next.count);
    if(bit==PATH){char text[2048];return bw_document_string(bytes,size,text,sizeof text)||strcmp(text,p->root)?-1:0;}
    if(bit==PRESENT)return boolean(bytes,size,&p->next.present);
    if(bit==CHANGED)return boolean(bytes,size,&p->next.changed);
    if(bit==QUEUED)return boolean(bytes,size,&p->next.queued);
    unsigned reset;if(boolean(bytes,size,&reset)||reset)return -1;return 0;
}
int bw_sm_reply_parse(enum bw_sm_operation kind,const char *root,const void *bytes,size_t size,struct bw_sm_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(kind>BW_SM_MANUAL_REMOVE||kind<BW_SM_VERSION||!root||strnlen(root,2048)>=2048)return -1;
    struct parser p={.kind=kind,.root=root};if(bw_document_fields(bytes,size,field,&p))return -1;
    unsigned required=STATUS;
    if(kind==BW_SM_VERSION)required|=VERSION|CAPS;
    else if(kind==BW_SM_MANUAL_LIST)required|=COUNT|PATHS;
    else if(kind==BW_SM_MANUAL_ADD||kind==BW_SM_MANUAL_REMOVE)required|=PATH|PRESENT|CHANGED;
    else required|=QUEUED;
    unsigned allowed=required|(kind==BW_SM_SCAN?RESET:0);
    if((p.seen&required)!=required||(p.seen&~allowed)||(kind==BW_SM_VERSION&&p.next.capabilities!=7)||
       (kind==BW_SM_MANUAL_LIST&&p.items!=p.next.count)||(kind==BW_SM_MANUAL_ADD&&!p.next.present)||
       (kind==BW_SM_MANUAL_REMOVE&&p.next.present)||(kind==BW_SM_SCAN&&!p.next.queued))return -1;
    *out=p.next;return 0;
}
struct uninstall_parser {const char *title;struct bw_sm_uninstall_reply value;unsigned seen;char reason[32];};
static int uninstall_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct uninstall_parser *p=opaque;unsigned bit=0;uint32_t status=0;
#define KEY(s) (kn==sizeof(s)-1&&!memcmp(key,s,sizeof(s)-1))
    if(KEY("status"))bit=1u;else if(KEY("title_id"))bit=2u;
    else if(KEY("uninstall_requested"))bit=4u;else if(KEY("scan_queued"))bit=8u;
    else if(KEY("scan_deferred"))bit=16u;else if(KEY("scan_deferred_reason"))bit=32u;else return 0;
#undef KEY
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1u)return integer(raw,n,INT32_MAX,&status)||status?-1:0;
    if(bit==2u){char title[10];return bw_document_string(raw,n,title,sizeof title)||strcmp(title,p->title)?-1:0;}
    if(bit==4u)return boolean(raw,n,&p->value.requested);
    if(bit==8u)return boolean(raw,n,&p->value.scan_queued);
    if(bit==16u)return boolean(raw,n,&p->value.scan_deferred);
    return bw_document_string(raw,n,p->reason,sizeof p->reason);
}
int bw_sm_uninstall_reply_parse(const char title[10],const void *bytes,size_t size,struct bw_sm_uninstall_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    struct uninstall_parser p={.title=title};
    if(bw_document_fields(bytes,size,uninstall_field,&p)||p.seen!=63u||!p.value.requested||
       (!!p.reason[0])!=p.value.scan_deferred||(p.value.scan_deferred&&!p.value.scan_queued))return -1;
    if(p.reason[0]&&strcmp(p.reason,"rest_mode")&&strcmp(p.reason,"game_active")&&strcmp(p.reason,"runtime_prepared"))return -1;
    *out=p.value;return 0;
}
struct title_error_parser {unsigned seen,status;char reason[64],error[256];};
static int title_error_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct title_error_parser *p=opaque;unsigned bit=0;uint32_t status=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1;
    else if(kn==5&&!memcmp(key,"error",5))bit=2;
    else if(kn==12&&!memcmp(key,"error_reason",12))bit=4;else return -1;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1){if(integer(raw,n,INT32_MAX,&status)||!status)return -1;p->status=status;return 0;}
    return bw_document_string(raw,n,bit==2?p->error:p->reason,bit==2?sizeof p->error:sizeof p->reason);
}
int bw_sm_uninstall_error_parse(unsigned http,const void *raw,size_t size,struct bw_sm_uninstall_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);struct title_error_parser p={0};
    if(http==200||bw_document_fields(raw,size,title_error_field,&p)||p.seen!=7||!p.error[0])return -1;
    out->response_complete=1;out->http_status=http;out->api_status=p.status;
    memcpy(out->error_reason,p.reason,sizeof p.reason);memcpy(out->error,p.error,sizeof p.error);
    /* These exact API-v1 reasons are produced before AppUnInstall is called.
     * Complete generic errors (including allocation after a platform call)
     * remain uncertain and never acquire retry authority. */
    static const char *const reasons[]={"rest_mode","game_active","install_pending","runtime_prepared","scanner_busy","runtime_busy","title_unmount_busy"};
    if(http==409&&p.status==16)for(unsigned i=0;i<sizeof reasons/sizeof *reasons;i++)
        if(!strcmp(p.reason,reasons[i]))out->refused_before_platform=1;
    return 0;
}
struct unmount_parser {const char *title;unsigned seen,mounted;};
static int unmount_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct unmount_parser *p=opaque;unsigned bit=0;uint32_t value=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1;
    else if(kn==8&&!memcmp(key,"title_id",8))bit=2;
    else if(kn==7&&!memcmp(key,"mounted",7))bit=4;else return -1;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1)return integer(raw,size,INT32_MAX,&value)||value?-1:0;
    if(bit==2){char title[10];return bw_document_string(raw,size,title,sizeof title)||strcmp(title,p->title)?-1:0;}
    return boolean(raw,size,&p->mounted);
}
int bw_sm_unmount_reply_parse(const char title[10],unsigned http,const void *raw,size_t size,struct bw_sm_unmount_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    if(http!=200){
        struct title_error_parser p={0};if(bw_document_fields(raw,size,title_error_field,&p)||
            (p.seen!=3&&p.seen!=7)||!p.error[0])return -1;
        out->response_complete=1;out->http_status=http;out->api_status=p.status;
        memcpy(out->error_reason,p.reason,sizeof p.reason);memcpy(out->error,p.error,sizeof p.error);return -1;
    }
    struct unmount_parser p={.title=title};
    if(bw_document_fields(raw,size,unmount_field,&p)||p.seen!=7||p.mounted)return -1;
    out->released=out->response_complete=1;out->http_status=200;return 0;
}
