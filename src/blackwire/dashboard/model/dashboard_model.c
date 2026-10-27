#include "dashboard_model.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static size_t length(const char *s,size_t cap){size_t n=0;if(!s)return cap;while(n<cap&&s[n])++n;return n;}
static int nonzero(const uint8_t value[32]){unsigned n=0;for(size_t i=0;i<32;i++)n|=value[i];return n!=0;}
static int put(char *out,size_t cap,const char *s){size_t n=length(s,cap);if(n>=cap)return -1;memcpy(out,s,n+1);return 0;}
static int title_valid(const char *s){
    if(length(s,10)!=9||(memcmp(s,"PPSA",4)&&memcmp(s,"CUSA",4)))return 0;
    for(size_t i=4;i<9;i++)if(s[i]<'0'||s[i]>'9')return 0;
    return 1;
}
static int text_valid(const char *s,size_t cap,int empty){
    size_t n=length(s,cap);if(n>=cap||(!n&&!empty))return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]==127)return 0;
    return 1;
}
static int path_valid(const char *s,int absolute){
    if(!text_valid(s,BD_SOURCE_BYTES,0)||(s[0]=='/')!=absolute)return 0;
    size_t at=absolute?1u:0u,start=at;
    if(!s[at])return 0;
    for(;;at++){
        if(s[at]=='\\')return 0;
        if(!s[at]||s[at]=='/'){
            size_t n=at-start;
            if(!n||(n==1&&s[start]=='.')||(n==2&&s[start]=='.'&&s[start+1]=='.'))return 0;
            if(!s[at])return 1;
            start=at+1;
        }
    }
}
static int blackwire_root(const char *path){
    static const char base[]="/data/blackwire";size_t n=sizeof base-1;
    return !strncmp(path,base,n)&&(!path[n]||path[n]=='/');
}
static int catalog_valid(const struct bd_catalog *c){
    return c&&c->items&&c->capacity&&c->capacity<=BD_CATALOG_MAX&&c->count<=c->capacity&&c->generation;
}
const struct bd_item *bd_catalog_find(const struct bd_catalog *c,const char *id){
    if(!catalog_valid(c)||!text_valid(id,BD_ID_BYTES,0))return NULL;
    const struct bd_item *found=NULL;
    for(size_t i=0;i<c->count;i++){
        if(!text_valid(c->items[i].id,BD_ID_BYTES,0))return NULL;
        if(!strcmp(c->items[i].id,id)){if(found)return NULL;found=&c->items[i];}
    }
    return found;
}
int bd_catalog_init(struct bd_catalog *c,struct bd_item *items,size_t cap,uint64_t generation){
    if(!c||!items||!cap||cap>BD_CATALOG_MAX||!generation)return -1;
    *c=(struct bd_catalog){items,0,cap,generation};return 0;
}
static int complete_item(struct bd_catalog *c,struct bd_item *item){
    if(!catalog_valid(c)||c->count>=c->capacity)return -1;
    char hash[65];for(size_t i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",item->source_fingerprint[i]);
    const char *kind=item->kind==BD_PC_STREAM?"pc":item->kind==BD_LOCAL_FOLDER?"folder":item->kind==BD_LOCAL_IMAGE?"image":"pkg";
    int n=snprintf(item->id,sizeof item->id,"%s:%s:%s",kind,item->title_id,hash);
    if(n<0||(size_t)n>=sizeof item->id||bd_catalog_find(c,item->id))return -1;
    n=snprintf(item->cover_ref,sizeof item->cover_ref,"/api/dashboard/cover?id=%s",item->id);
    if(n<0||(size_t)n>=sizeof item->cover_ref)return -1;
    item->catalog_generation=c->generation;c->items[c->count++]=*item;return 0;
}
int bd_catalog_add_pc(struct bd_catalog *c,const struct bd_pc_input *in){
    if(!in||!title_valid(in->title_id)||!text_valid(in->title,BD_TITLE_BYTES,0)||!path_valid(in->source,0)||
       !nonzero(in->source_fingerprint)||!in->library_generation||!in->settings_revision||
       in->startup_ready>1||in->can_play>1||in->can_send>1||in->storage_known>1||
       in->can_uninstall>1||in->removed>1||in->can_restore>1||in->can_remove_startup>1||
       (in->can_remove_startup&&(in->can_uninstall||in->removed))||
       in->startup_bytes>UINT64_C(9007199254740991)||in->pc_bytes>UINT64_C(9007199254740991)||in->console_allocated_bytes>UINT64_C(9007199254740991)||
       (in->play_reason&&!text_valid(in->play_reason,BD_REASON_BYTES,1))||
       (in->send_reason&&!text_valid(in->send_reason,BD_REASON_BYTES,1))||
       (in->uninstall_reason&&!text_valid(in->uninstall_reason,BD_REASON_BYTES,1))||
       (in->restore_reason&&!text_valid(in->restore_reason,BD_REASON_BYTES,1)))return -1;
    struct bd_item item={0};item.kind=BD_PC_STREAM;item.library_generation=in->library_generation;item.settings_revision=in->settings_revision;
    if(put(item.title,sizeof item.title,in->title)||put(item.title_id,sizeof item.title_id,in->title_id)||put(item.source,sizeof item.source,in->source))return -1;
    memcpy(item.source_fingerprint,in->source_fingerprint,32);
    if(in->storage_known){item.storage_known=1;item.startup_bytes=in->startup_bytes;item.pc_bytes=in->pc_bytes;item.console_allocated_bytes=in->console_allocated_bytes;}
    if(in->startup_ready)item.flags|=BD_READY;
    if(in->can_play&&in->startup_ready)item.flags|=BD_CAN_PLAY;
    else if(put(item.play_reason,sizeof item.play_reason,!in->startup_ready&&in->can_play?
        "Send files to prepare this game before playing.":in->play_reason&&*in->play_reason?in->play_reason:"This PC source is not available to start."))return -1;
    if(in->can_send&&!in->startup_ready)item.flags|=BD_CAN_SEND;
    else if(put(item.send_reason,sizeof item.send_reason,in->startup_ready?"Startup files are already ready.":
        in->send_reason&&*in->send_reason?in->send_reason:"Startup preparation is unavailable."))return -1;
    if(in->removed){
        item.flags=BD_REMOVED|(in->can_restore?BD_CAN_RESTORE:0u);item.storage_known=0;
        if(put(item.play_reason,sizeof item.play_reason,"Restore this game to your library before preparing it.")||
           put(item.send_reason,sizeof item.send_reason,"This game was removed from your console library."))return -1;
    }else if(in->can_uninstall)item.flags|=BD_CAN_UNINSTALL;
    else if(in->can_remove_startup)item.flags|=BD_CAN_REMOVE_STARTUP;
    if(put(item.uninstall_reason,sizeof item.uninstall_reason,in->uninstall_reason?in->uninstall_reason:"")||
       put(item.restore_reason,sizeof item.restore_reason,in->restore_reason?in->restore_reason:""))return -1;
    return complete_item(c,&item);
}
int bd_catalog_add_local(struct bd_catalog *c,const struct bd_local_input *in){
    if(!in||(in->kind!=BD_LOCAL_FOLDER&&in->kind!=BD_INSTALLED_PACKAGE&&in->kind!=BD_LOCAL_IMAGE)||!title_valid(in->title_id)||
       !text_valid(in->title,BD_TITLE_BYTES,0)||!path_valid(in->source,1)||!path_valid(in->registered_source,1)||
       !nonzero(in->source_fingerprint)||(in->facts&~BD_LOCAL_REQUIRED)||(in->exclusions&~15u))return -1;
    struct bd_item item={0};item.kind=in->kind;
    if(put(item.title,sizeof item.title,in->title)||put(item.title_id,sizeof item.title_id,in->title_id)||
       put(item.source,sizeof item.source,in->source)||put(item.registered_source,sizeof item.registered_source,in->registered_source))return -1;
    memcpy(item.source_fingerprint,in->source_fingerprint,32);
    const char *why=NULL;
    if(blackwire_root(in->source)||blackwire_root(in->registered_source)||(in->exclusions&(BD_LOCAL_BLACKWIRE_OWNED|BD_LOCAL_BLACKWIRE_MARKER)))
        why="This is a Blackwire streaming copy. Start its PC source through Blackwire.";
    else if(in->exclusions&BD_LOCAL_TRANSFER_ACTIVE)why="Wait for this console copy to finish transferring.";
    else if(in->exclusions&BD_LOCAL_AMBIGUOUS)why="This title has an ambiguous console source. Refresh after selecting its intended copy.";
    else if(in->kind==BD_LOCAL_IMAGE)why="Local image launch verification is not available in this build.";
    else if(strcmp(in->source,in->registered_source)||!(in->facts&BD_LOCAL_REGISTRATION_MATCH))why="A different source of this title is registered on the console.";
    else if(in->facts!=BD_LOCAL_REQUIRED)why="This console source has not finished its identity and source-stability checks.";
    if(why){if(put(item.play_reason,sizeof item.play_reason,why))return -1;}
    else item.flags=BD_READY|BD_CAN_PLAY;
    if(put(item.send_reason,sizeof item.send_reason,"This game is already on the PS5; Send files applies to PC sources."))return -1;
    return complete_item(c,&item);
}
static int frontend_valid(const struct bd_frontend_identity *f){
    return f&&f->pid>1&&f->app_id&&f->app_id<=INT32_MAX&&f->birth_sec&&f->birth_usec<1000000u&&
        title_valid(f->title_id)&&nonzero(f->package_fingerprint);
}
static int nonce_valid(const char *s){
    if(length(s,33)!=32)return 0;
    unsigned any=0;
    for(size_t i=0;i<32;i++){if(!strchr("0123456789abcdef",s[i]))return 0;any|=s[i]!='0';}
    return any!=0;
}
static int ops_valid(const struct bd_intent_ops *o){return o&&o->now_ms&&o->frontend_state&&o->idle&&o->current&&o->dispatch;}
int bd_item_can_play(const struct bd_item *item){
    return item&&!(item->flags&BD_REMOVED)&&(item->flags&BD_CAN_PLAY)&&(item->kind!=BD_PC_STREAM||(item->flags&BD_READY));
}
int bd_item_can_send(const struct bd_item *item){
    return item&&item->kind==BD_PC_STREAM&&!(item->flags&(BD_READY|BD_REMOVED))&&(item->flags&BD_CAN_SEND);
}
int bd_item_can_uninstall(const struct bd_item *item){return item&&!(item->flags&BD_REMOVED)&&(item->flags&BD_CAN_UNINSTALL);}
int bd_item_can_restore(const struct bd_item *item){return item&&item->kind==BD_PC_STREAM&&(item->flags&BD_REMOVED)&&(item->flags&BD_CAN_RESTORE);}
int bd_item_can_remove_startup(const struct bd_item *item){return item&&item->kind==BD_PC_STREAM&&!(item->flags&BD_REMOVED)&&(item->flags&BD_CAN_REMOVE_STARTUP);}
static int action_permitted(const struct bd_item *item,enum bd_action action){
    return action==BD_PLAY?bd_item_can_play(item):action==BD_SEND?bd_item_can_send(item):
        action==BD_UNINSTALL?bd_item_can_uninstall(item):action==BD_RESTORE?bd_item_can_restore(item):action==BD_REMOVE_STARTUP?bd_item_can_remove_startup(item):0;
}
static int refuse(struct bd_intent *i,enum bd_intent_phase phase,const char *why){i->phase=phase;(void)put(i->message,sizeof i->message,why);return -1;}
int bd_intent_begin(struct bd_intent *i,const struct bd_catalog *c,const struct bd_intent_request *r,
                    const struct bd_frontend_identity *f,const char *nonce,const struct bd_intent_ops *o){
    if(!i||i->phase!=BD_INTENT_EMPTY||!r||!catalog_valid(c)||r->catalog_generation!=c->generation||
       !frontend_valid(f)||!nonce_valid(nonce)||!ops_valid(o)||(r->action!=BD_PLAY&&r->action!=BD_SEND&&r->action!=BD_UNINSTALL&&r->action!=BD_RESTORE&&r->action!=BD_REMOVE_STARTUP))return -1;
    const struct bd_item *item=bd_catalog_find(c,r->item_id);
    if(!item||item->kind!=r->kind||item->catalog_generation!=c->generation||
       !action_permitted(item,r->action))return -1;
    uint64_t now=0;if(o->now_ms(o->opaque,&now)||now>UINT64_MAX-BD_EXIT_WAIT_MS||
       o->frontend_state(o->opaque,f)!=1||o->current(o->opaque,item))return -1;
    struct bd_intent value={0};value.phase=BD_INTENT_WAIT_EXIT;value.action=r->action;value.selected=*item;value.frontend=*f;
    value.started_ms=value.last_ms=now;value.deadline_ms=now+BD_EXIT_WAIT_MS;memcpy(value.nonce,nonce,33);
    (void)put(value.message,sizeof value.message,"Selection queued. Closing the native dashboard before continuing.");*i=value;return 0;
}
int bd_intent_tick(struct bd_intent *i,const struct bd_intent_ops *o){
    if(!i||!ops_valid(o))return -1;
    if(i->phase!=BD_INTENT_WAIT_EXIT)return 0;
    uint64_t now=0;if(o->now_ms(o->opaque,&now)||now<i->last_ms)return refuse(i,BD_INTENT_UNKNOWN,"The handoff clock is uncertain. No launch was dispatched.");
    i->last_ms=now;
    if(now>=i->deadline_ms)return refuse(i,BD_INTENT_REFUSED,"The dashboard normal exit was not verified within its handoff window. No launch was dispatched.");
    if(o->current(o->opaque,&i->selected))return refuse(i,BD_INTENT_REFUSED,"The selected source or settings changed. Open the dashboard and choose it again.");
    int frontend=o->frontend_state(o->opaque,&i->frontend);
    /* Sony and process snapshots may disagree while the admitted frontend
     * exits normally. Retain its exact identity, reservation and original
     * deadline; uncertainty never authorizes dispatch. */
    if(frontend==-1)return 0;
    if(frontend<0)return refuse(i,BD_INTENT_UNKNOWN,"The dashboard process observation was invalid. No launch was dispatched.");
    if(frontend==1)return 0;
    if(frontend!=0)return refuse(i,BD_INTENT_UNKNOWN,"The dashboard process observation was invalid. No launch was dispatched.");
    /* Use unchanged full idle gates only after exact normal frontend exit.
     * The real dispatch adapter repeats its own native source and idle checks. */
    if(o->idle(o->opaque)||o->current(o->opaque,&i->selected))
        return refuse(i,BD_INTENT_REFUSED,"The console is busy or the selected source changed. No launch was dispatched.");
    frontend=o->frontend_state(o->opaque,&i->frontend);
    if(frontend==-1)return 0;
    if(frontend!=0||o->idle(o->opaque))
        return refuse(i,BD_INTENT_REFUSED,"The console is busy or the selected source changed. No launch was dispatched.");
    if(o->now_ms(o->opaque,&now)||now<i->last_ms||now>=i->deadline_ms)
        return refuse(i,BD_INTENT_UNKNOWN,"The handoff deadline could not be confirmed. No launch was dispatched.");
    i->last_ms=now;i->phase=BD_INTENT_DISPATCHING;i->dispatch_calls=1;
    int result=o->dispatch(o->opaque,&i->selected,i->action,i->nonce);
    if(result==1){i->phase=BD_INTENT_ACCEPTED;(void)put(i->message,sizeof i->message,"The selected operation was accepted. Menu and gameplay are separate observations.");return 1;}
    return refuse(i,result==0?BD_INTENT_REFUSED:BD_INTENT_UNKNOWN,result==0?
        "The selected operation was refused.":"The operation outcome is uncertain. It will not be submitted again.");
}
int bd_intent_cancel(struct bd_intent *i){
    if(!i||i->phase!=BD_INTENT_WAIT_EXIT)return -1;
    (void)refuse(i,BD_INTENT_CANCELLED,"The queued dashboard selection was cancelled.");return 0;
}
