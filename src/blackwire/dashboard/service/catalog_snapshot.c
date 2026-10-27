#include "catalog_snapshot.h"
#include "native_document.h"
#include "native_json.h"
#include <stdlib.h>
#include <string.h>

struct pc_row {
    char title[BD_TITLE_BYTES],title_id[10],source[BD_SOURCE_BYTES],binding[75];
    char play_reason[BD_REASON_BYTES],send_reason[BD_REASON_BYTES];
    char uninstall_reason[BD_REASON_BYTES],restore_reason[BD_REASON_BYTES];
    unsigned ready,can_start,can_send,seen;
    unsigned can_uninstall,removed,can_restore,can_remove_startup;
    unsigned size_known;
    uint64_t startup_bytes,pc_bytes,console_allocated_bytes;
    char storage_verified[16];
};
struct pc_parser {struct pc_row *rows;size_t count,capacity;uint64_t generation;uint32_t revision;unsigned seen;};
static int number(const void *raw,size_t size,uint64_t max,uint64_t *out){
    const unsigned char *p=raw;uint64_t n=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>max||n>(max-d)/10)return -1;n=n*10+d;}*out=n;return 0;
}
static int flag(const void *raw,size_t size,unsigned *out){
    if(size==4&&!memcmp(raw,"true",4)){*out=1;return 0;}if(size==5&&!memcmp(raw,"false",5)){*out=0;return 0;}return -1;
}
static int pc_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct pc_row *p=opaque;unsigned bit=0;char *text=NULL;size_t cap=0;unsigned *boolean=NULL;
#define STR(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;text=p->m;cap=sizeof p->m;}
    STR("title",1u,title) else STR("titleId",2u,title_id) else STR("source",4u,source)
    else STR("sourceBinding",8u,binding) else STR("playReason",16u,play_reason) else STR("preparationReason",32u,send_reason)
    else STR("storageVerifiedAt",8192u,storage_verified)
    else STR("uninstallReason",131072u,uninstall_reason) else STR("restoreReason",262144u,restore_reason)
#undef STR
#define BOOL(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;boolean=&p->m;}
    else BOOL("startupReady",64u,ready) else BOOL("canStart",128u,can_start) else BOOL("canPrepare",256u,can_send)
    else BOOL("sizeKnown",512u,size_known)
    else BOOL("canRemoveStartup",524288u,can_remove_startup) else BOOL("canUninstall",16384u,can_uninstall) else BOOL("removed",32768u,removed) else BOOL("canRestore",65536u,can_restore)
    else if(kn==9&&!memcmp(key,"sendBytes",9))bit=1024u;
    else if(kn==9&&!memcmp(key,"stayBytes",9))bit=2048u;
    else if(kn==21&&!memcmp(key,"consoleAllocatedBytes",21))bit=4096u;
    else return 0;
#undef BOOL
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1024u||bit==2048u||bit==4096u)return number(raw,size,UINT64_C(9007199254740991),bit==1024u?&p->startup_bytes:bit==2048u?&p->pc_bytes:&p->console_allocated_bytes);
    return text?bw_document_string(raw,size,text,cap):flag(raw,size,boolean);
}
static int pc_item(void *opaque,const void *raw,size_t size){
    struct pc_parser *p=opaque;if(p->count>=p->capacity)return -1;
    struct pc_row *row=&p->rows[p->count];memset(row,0,sizeof *row);
    if(bw_document_fields(raw,size,pc_field,row)||(row->seen&511u)!=511u||
       (row->size_known&&((row->seen&15360u)!=15360u||strcmp(row->storage_verified,"preparation")))||
       (row->removed&&(row->ready||row->can_start||row->can_send||row->can_uninstall||row->can_remove_startup))||(row->can_restore&&!row->removed))return -1;
    p->count++;return 0;
}
static int revision_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct pc_parser *p=opaque;uint64_t n=0;
    if(kn==8&&!memcmp(key,"revision",8)){if(p->seen&8u||number(raw,size,UINT32_MAX,&n))return -1;p->seen|=8u;p->revision=(uint32_t)n;}return 0;
}
static int generation_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct pc_parser *p=opaque;
    if(kn==10&&!memcmp(key,"generation",10)){if(p->seen&16u||number(raw,size,UINT64_C(9007199254740991),&p->generation))return -1;p->seen|=16u;}return 0;
}
static int root_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct pc_parser *p=opaque;unsigned bit=0;
    if(kn==5&&!memcmp(key,"setup",5))bit=1u;
    else if(kn==7&&!memcmp(key,"library",7))bit=2u;
    else if(kn==5&&!memcmp(key,"games",5))bit=4u;
    else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;
    return bit==1u?bw_document_fields(raw,size,revision_field,p):bit==2u?bw_document_fields(raw,size,generation_field,p):bw_document_array(raw,size,pc_item,p);
}
int bd_catalog_append_blackwire(struct bd_catalog *catalog,const void *raw,size_t size,size_t *skipped){
    if(skipped)*skipped=0;if(!catalog||!catalog->items||!skipped)return -1;
    struct pc_parser p={.capacity=BD_CATALOG_MAX};p.rows=calloc(p.capacity,sizeof *p.rows);if(!p.rows)return -1;
    int result=-1;size_t original=catalog->count;
    if(bw_document_fields(raw,size,root_field,&p)||p.seen!=31u)goto done;
    for(size_t i=0;i<p.count;i++){
        const struct pc_row *row=&p.rows[i];
        if(!p.generation||!p.revision||strlen(row->binding)!=74||strlen(row->title_id)!=9||memcmp(row->binding,row->title_id,9)||row->binding[9]!='_'){
            (*skipped)++;continue;
        }
        struct bd_pc_input item={.title=row->title,.title_id=row->title_id,.source=row->source,.play_reason=row->play_reason,.send_reason=row->send_reason,
            .startup_ready=row->ready,.can_play=row->can_start,.can_send=row->can_send,.library_generation=p.generation,.settings_revision=p.revision,
            .can_remove_startup=row->can_remove_startup,.can_uninstall=row->can_uninstall,.removed=row->removed,.can_restore=row->can_restore,.uninstall_reason=row->uninstall_reason,.restore_reason=row->restore_reason,
            .storage_known=row->size_known,.startup_bytes=row->startup_bytes,.pc_bytes=row->pc_bytes,.console_allocated_bytes=row->console_allocated_bytes};
        static const char digits[]="0123456789abcdef";
        for(unsigned j=0;j<32;j++){
            const char *a=strchr(digits,row->binding[10+2*j]),*b=strchr(digits,row->binding[11+2*j]);
            if(!a||!b)goto done;item.source_fingerprint[j]=(uint8_t)(((a-digits)<<4)|(b-digits));
        }
        if(bd_catalog_add_pc(catalog,&item))goto done;
    }
    result=0;
done:
    if(result)catalog->count=original;free(p.rows);return result;
}
int bd_catalog_json(const struct bd_catalog *catalog,unsigned pc_connected,unsigned local_connected,
                    const struct bd_catalog_operation *operation,char **out,size_t *size){
    if(!out||!size)return -1;*out=NULL;*size=0;
    if(!catalog||!catalog->items||catalog->count>catalog->capacity||catalog->capacity>BD_CATALOG_MAX||!catalog->generation||
       pc_connected>1||local_connected>1||!operation||operation->active>1||operation->exit_requested>1||operation->frontend_pid<0||
       (operation->exit_requested&&(!operation->active||operation->frontend_pid<=1||!operation->phase||strcmp(operation->phase,"waiting-for-dashboard-exit")||
        !operation->action||(strcmp(operation->action,"play")&&strcmp(operation->action,"uninstall")&&strcmp(operation->action,"restore")&&strcmp(operation->action,"remove-startup"))))||
       (operation->action&&*operation->action&&strcmp(operation->action,"play")&&strcmp(operation->action,"send")&&strcmp(operation->action,"uninstall")&&strcmp(operation->action,"restore")&&strcmp(operation->action,"remove-startup"))||
       operation->removal.observed>1||operation->removal.phase>7u||(operation->removal.files_total&&operation->removal.files>operation->removal.files_total)||
       (operation->removal.observed&&(!operation->action||(strcmp(operation->action,"uninstall")&&strcmp(operation->action,"restore")&&strcmp(operation->action,"remove-startup"))))||
       operation->preparation.observed>1||operation->preparation.ready>1||operation->preparation.phase>13u||
       operation->preparation.files>operation->preparation.files_total||operation->preparation.bytes>operation->preparation.bytes_total||
       operation->preparation.bytes_total>UINT64_C(9007199254740991)||
       (operation->preparation.ready&&(!operation->preparation.job_state||strcmp(operation->preparation.job_state,"done")||
        !operation->action||strcmp(operation->action,"send"))))return -1;
    struct bw_json json={.maximum=BW_NATIVE_DOCUMENT_LIMIT};
    bw_json_format(&json,"{\"schema\":1,\"catalogGeneration\":%llu,\"pcConnected\":%s,\"consoleConnected\":%s,\"items\":[",
        (unsigned long long)catalog->generation,pc_connected?"true":"false",local_connected?"true":"false");
    for(size_t i=0;i<catalog->count;i++){
        const struct bd_item *item=&catalog->items[i];
        if(item->storage_known>1||item->startup_bytes>UINT64_C(9007199254740991)||item->pc_bytes>UINT64_C(9007199254740991)||
           item->console_allocated_bytes>UINT64_C(9007199254740991)||(item->storage_known&&item->kind!=BD_PC_STREAM)){free(json.bytes);return -1;}
        bw_json_text(&json,i?",{\"id\":":"{\"id\":");bw_json_string(&json,item->id);
        bw_json_text(&json,",\"title\":");bw_json_string(&json,item->title);
        bw_json_text(&json,",\"titleId\":");bw_json_string(&json,item->title_id);
        bw_json_text(&json,",\"source\":");bw_json_string(&json,item->source);
        bw_json_format(&json,",\"kind\":%u,\"startupReady\":%s,\"canPlay\":%s,\"canSend\":%s,\"libraryGeneration\":%llu,\"settingsRevision\":%u",
            (unsigned)item->kind,item->flags&BD_READY?"true":"false",bd_item_can_play(item)?"true":"false",bd_item_can_send(item)?"true":"false",
            (unsigned long long)item->library_generation,item->settings_revision);
        bw_json_text(&json,",\"playReason\":");bw_json_string(&json,item->play_reason);
        bw_json_text(&json,",\"sendReason\":");bw_json_string(&json,item->send_reason);
        bw_json_format(&json,",\"canUninstall\":%s,\"removed\":%s,\"canRestore\":%s",
            bd_item_can_uninstall(item)?"true":"false",item->flags&BD_REMOVED?"true":"false",bd_item_can_restore(item)?"true":"false");
        bw_json_format(&json,",\"canRemoveStartup\":%s",bd_item_can_remove_startup(item)?"true":"false");
        bw_json_text(&json,",\"uninstallReason\":");bw_json_string(&json,item->uninstall_reason);
        bw_json_text(&json,",\"restoreReason\":");bw_json_string(&json,item->restore_reason);
        bw_json_format(&json,",\"storageKnown\":%s",item->storage_known?"true":"false");
        if(item->storage_known)bw_json_format(&json,",\"startupBytes\":%llu,\"pcBytes\":%llu,\"consoleAllocatedBytes\":%llu,\"storageVerifiedAt\":\"preparation\"",
            (unsigned long long)item->startup_bytes,(unsigned long long)item->pc_bytes,(unsigned long long)item->console_allocated_bytes);
        bw_json_text(&json,",\"cover\":");bw_json_string(&json,item->cover_ref);bw_json_text(&json,"}");
    }
    bw_json_text(&json,"],\"operation\":{\"id\":");bw_json_string(&json,operation->id?operation->id:"");
    bw_json_text(&json,",\"itemId\":");bw_json_string(&json,operation->item_id?operation->item_id:"");
    bw_json_text(&json,",\"phase\":");bw_json_string(&json,operation->phase?operation->phase:"idle");
    bw_json_text(&json,",\"message\":");bw_json_string(&json,operation->message?operation->message:"");
    bw_json_format(&json,",\"active\":%s,\"exitRequested\":%s,\"frontendPid\":%d",operation->active?"true":"false",operation->exit_requested?"true":"false",operation->frontend_pid);
    bw_json_text(&json,",\"action\":");bw_json_string(&json,operation->action?operation->action:"");
    bw_json_text(&json,",\"title\":");bw_json_string(&json,operation->title?operation->title:"");
    bw_json_text(&json,",\"preparation\":{\"jobId\":");bw_json_string(&json,operation->preparation.job_id?operation->preparation.job_id:"");
    bw_json_text(&json,",\"jobState\":");bw_json_string(&json,operation->preparation.job_state?operation->preparation.job_state:"");
    bw_json_text(&json,",\"message\":");bw_json_string(&json,operation->preparation.message?operation->preparation.message:"");
    bw_json_format(&json,",\"observed\":%s,\"phase\":%u,\"files\":%u,\"filesTotal\":%u,\"bytes\":%llu,\"bytesTotal\":%llu,\"ready\":%s}",
        operation->preparation.observed?"true":"false",operation->preparation.phase,operation->preparation.files,operation->preparation.files_total,
        (unsigned long long)operation->preparation.bytes,(unsigned long long)operation->preparation.bytes_total,operation->preparation.ready?"true":"false");
    bw_json_format(&json,",\"removal\":{\"observed\":%s,\"phase\":%u,\"files\":%u,\"filesTotal\":%u}}}",
        operation->removal.observed?"true":"false",operation->removal.phase,operation->removal.files,operation->removal.files_total);
    if(json.bad){free(json.bytes);return -1;}*out=json.bytes;*size=json.used;return 0;
}
