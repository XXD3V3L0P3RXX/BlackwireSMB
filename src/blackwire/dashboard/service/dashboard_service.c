/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "dashboard_service.h"
#include "settings_guard.h"
#include "native_document.h"
#include "native_json.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BD_SAFE_INTEGER UINT64_C(9007199254740991)
#define BD_LOCAL_COVER_LIMIT (1024u*1024u)
enum feedback_phase {FB_QUEUED,FB_CHECKING,FB_STARTING,FB_STARTED,FB_FAILED,FB_UNKNOWN,FB_STOPPED};
enum settings_authority {SETTINGS_UNSET,SETTINGS_FULL_IDLE,SETTINGS_NATIVE_FRONTEND};
struct launch_feedback {
    char nonce[33],pending[160];
    uint64_t started_ms,last_ms,notice_ms;
    unsigned bound,ended,attention,elapsed_notices;
    enum feedback_phase progress_phase;
};
struct preparation_progress {
    char state[24],message[384];struct bw_source_progress progress;unsigned observed,ready;
};
struct bd_dashboard_service {
    pthread_mutex_t lock;
    struct bd_service_config config;
    char apps_root[BD_SOURCE_BYTES],database[BD_SOURCE_BYTES],settings_game_log[BD_SOURCE_BYTES];
    struct bd_frontend_owner frontend;
    struct bd_catalog catalog;
    unsigned pc_connected,local_connected;
    struct bd_intent intent;
    unsigned active,reserved,local_session,settings_guard_attached;
    uint32_t settings_operation;enum settings_authority settings_authority;struct bd_frontend_identity settings_owner;
    unsigned preparation_guard_attached,preparation_borrowed,preparation_native;
    struct bd_frontend_owner preparation_owner;
    struct bd_frontend_identity preparation_identity;
    struct bd_frontend_preparation *preparation_pin;
    struct preparation_progress preparation_progress;
    struct bw_game_uninstall_result removal_progress;
    unsigned removal_observed;
    unsigned removal_observing;
    unsigned removal_consumed_unknown;
    uint64_t removal_observe_next_ms;
    char confirmed_title[10];
    char action_token[33],backend_id[33],phase[64],message[BD_REASON_BYTES];
    struct launch_feedback feedback;
};
struct native_status {
    char token[33],library_phase[24],stream_phase[64],stream_id[33],stream_message[256];
    uint64_t generation;
    uint32_t revision;
    uint32_t game_pid;
    unsigned seen,stale;
};
static int decimal(const void *raw,size_t n,uint64_t max,uint64_t *out) {
    const unsigned char *p=raw;uint64_t value=0;
    if(!n||(n>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<n;i++){unsigned digit=p[i]-'0';if(digit>9||digit>max||value>(max-digit)/10)return -1;value=value*10+digit;}
    *out=value;return 0;
}
static int boolean(const void *raw,size_t n,unsigned *out) {
    if(n==4&&!memcmp(raw,"true",4)){*out=1;return 0;}
    if(n==5&&!memcmp(raw,"false",5)){*out=0;return 0;}return -1;
}
static int nonce_valid(const char *text) {
    if(!text||strnlen(text,33)!=32)return 0;unsigned nonzero=0;
    for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",text[i]))return 0;nonzero|=text[i]!='0';}return nonzero!=0;
}
static const char *action_name(enum bd_action action){return action==BD_PLAY?"play":action==BD_SEND?"send":action==BD_UNINSTALL?"uninstall":action==BD_RESTORE?"restore":"";}
static int removal_action(enum bd_action action){return action==BD_UNINSTALL||action==BD_RESTORE;}
static int same_token(const char *a,const char *b) {
    if(!nonce_valid(a)||!nonce_valid(b))return 0;unsigned different=0;
    for(unsigned i=0;i<32;i++)different|=(unsigned char)a[i]^(unsigned char)b[i];return !different;
}
static void message(struct bd_dashboard_service *s,const char *phase,const char *text) {
    snprintf(s->phase,sizeof s->phase,"%s",phase);snprintf(s->message,sizeof s->message,"%s",text);
}
static int text(struct bw_native_response *out,int status,const char *error) {
    struct bw_json j={.maximum=4096};bw_json_text(&j,"{\"ok\":false,\"error\":");bw_json_string(&j,error);bw_json_text(&j,"}");
    if(j.bad){free(j.bytes);return -1;}*out=(struct bw_native_response){status,"application/json",j.bytes,j.used};return 0;
}
static int now_ms(struct bd_dashboard_service *s,uint64_t *out) {
    if(s->config.ops.now_ms)return s->config.ops.now_ms(s->config.ops.opaque,out);
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L||
       (uint64_t)t.tv_sec>(UINT64_MAX-999u)/1000u)return -1;
    *out=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;return 0;
}
/* A notification is an observation, never admission or preparation work.
 * There are no new source/status reads. Startup ends at the exact matching
 * process, not at an inferred menu, and normal stop is not a failure. */
static void feedback_begin(struct bd_dashboard_service *s) {
    if(!s->config.ops.feedback||(s->intent.action!=BD_PLAY&&!removal_action(s->intent.action))||!nonce_valid(s->intent.nonce))return;
    uint64_t now;if(now_ms(s,&now))return;
    struct launch_feedback *f=&s->feedback;memset(f,0,sizeof *f);
    memcpy(f->nonce,s->intent.nonce,sizeof f->nonce);f->bound=1;
    f->progress_phase=FB_CHECKING;
    f->started_ms=f->last_ms=f->notice_ms=now;
    snprintf(f->pending,sizeof f->pending,"Blackwire: %s %.96s.",s->intent.action==BD_UNINSTALL?"Removing":s->intent.action==BD_RESTORE?"Restoring":"Launching",s->intent.selected.title);
}
static void feedback_update(struct bd_dashboard_service *s,enum feedback_phase phase) {
    struct launch_feedback *f=&s->feedback;
    if(!s->config.ops.feedback||!f->bound||f->ended||(s->intent.action!=BD_PLAY&&!removal_action(s->intent.action))||
       memcmp(f->nonce,s->intent.nonce,sizeof f->nonce))return;
    if(phase==FB_QUEUED)return; // The accepted notice already covers the normal frontend exit wait.
    if(phase==FB_STOPPED){f->ended=1;return;}
    if(phase==FB_STARTED||phase==FB_FAILED){
        const char *format=phase==FB_FAILED?"Blackwire: %.72s needs attention. Open Blackwire for details.":
            s->intent.action==BD_UNINSTALL?"Blackwire: Removed %.96s. Saved games are preserved.":
            s->intent.action==BD_RESTORE?"Blackwire: Restored %.96s to your library.":"Blackwire: %.96s has started.";
        if(phase==FB_FAILED&&s->intent.action==BD_PLAY)format="Blackwire: %.72s could not start. Open Blackwire for details.";
        snprintf(f->pending,sizeof f->pending,format,s->intent.selected.title);
        f->ended=1;return;
    }
    if(phase==FB_UNKNOWN){
        if(!f->attention){snprintf(f->pending,sizeof f->pending,"Blackwire: Operation status is unclear. Open Blackwire before trying again.");f->attention=1;}
        return;
    }
    uint64_t now;if(now_ms(s,&now)||now<f->last_ms)return;f->last_ms=now;
    /* Announce actual stage changes promptly. Same-stage reminders are15s
     * apart, with a shared four-notice budget even if stages alternate.
     * Terminal observations remain available after that bounded budget. */
    if((phase==f->progress_phase&&now-f->notice_ms<15000u)||f->elapsed_notices>=4u)return;
    snprintf(f->pending,sizeof f->pending,"Blackwire: %s %.96s (%llu s elapsed).",
        s->intent.action==BD_UNINSTALL?"Removing":s->intent.action==BD_RESTORE?"Restoring":phase==FB_STARTING?"Starting":"Checking",s->intent.selected.title,
        (unsigned long long)((now-f->started_ms)/1000u));
    f->notice_ms=now;f->progress_phase=phase;f->elapsed_notices++;
}
struct feedback_delivery {
    void *opaque;
    void (*send)(void *,const char[33],const char *);
    char nonce[33],text[160];
};
static void feedback_take(struct bd_dashboard_service *s,struct feedback_delivery *out) {
    memset(out,0,sizeof *out);
    if(s->feedback.pending[0]&&s->config.ops.feedback&&
       !memcmp(s->feedback.nonce,s->intent.nonce,sizeof s->feedback.nonce)){
        out->opaque=s->config.ops.opaque;out->send=s->config.ops.feedback;
        memcpy(out->nonce,s->feedback.nonce,sizeof out->nonce);memcpy(out->text,s->feedback.pending,sizeof out->text);
    }
    s->feedback.pending[0]=0;
}
static void feedback_deliver(const struct feedback_delivery *out) {
    if(out->send)out->send(out->opaque,out->nonce,out->text);
}
static int backend_read(struct bd_dashboard_service *s,const char *method,const char *url,
                         const void *body,size_t size,const char *token,struct bw_native_response *out) {
    return s->config.ops.backend_read?s->config.ops.backend_read(s->config.ops.opaque,method,url,body,size,token,out):
        bw_native_backend_handle_authenticated(s->config.backend,method,url,body,size,token,out);
}
static int setup_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct native_status *p=opaque;uint64_t value=0;
    if(kn==5&&!memcmp(key,"token",5)){if(p->seen&1u||bw_document_string(raw,n,p->token,sizeof p->token))return -1;p->seen|=1u;}
    else if(kn==8&&!memcmp(key,"revision",8)){if(p->seen&2u||decimal(raw,n,UINT32_MAX,&value))return -1;p->revision=(uint32_t)value;p->seen|=2u;}return 0;
}
static int library_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct native_status *p=opaque;
    if(kn==5&&!memcmp(key,"phase",5)){if(p->seen&4u||bw_document_string(raw,n,p->library_phase,sizeof p->library_phase))return -1;p->seen|=4u;}
    else if(kn==10&&!memcmp(key,"generation",10)){if(p->seen&8u||decimal(raw,n,BD_SAFE_INTEGER,&p->generation))return -1;p->seen|=8u;}
    else if(kn==5&&!memcmp(key,"stale",5)){if(p->seen&16u||boolean(raw,n,&p->stale))return -1;p->seen|=16u;}return 0;
}
static int stream_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct native_status *p=opaque;
    if(kn==5&&!memcmp(key,"phase",5)){if(p->seen&32u||bw_document_string(raw,n,p->stream_phase,sizeof p->stream_phase))return -1;p->seen|=32u;}
    else if(kn==5&&!memcmp(key,"runId",5)){if(p->seen&64u||bw_document_string(raw,n,p->stream_id,sizeof p->stream_id))return -1;p->seen|=64u;}
    else if(kn==7&&!memcmp(key,"message",7)){if(p->seen&128u||bw_document_string(raw,n,p->stream_message,sizeof p->stream_message))return -1;p->seen|=128u;}
    else if(kn==7&&!memcmp(key,"gamePid",7)){uint64_t value=0;if(p->seen&2048u||decimal(raw,n,INT32_MAX,&value))return -1;p->seen|=2048u;p->game_pid=(uint32_t)value;}return 0;
}
static int status_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct native_status *p=opaque;unsigned bit=0;
    if(kn==5&&!memcmp(key,"setup",5))bit=256u;
    else if(kn==7&&!memcmp(key,"library",7))bit=512u;
    else if(kn==6&&!memcmp(key,"stream",6))bit=1024u;else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;
    return bw_document_fields(raw,n,bit==256u?setup_field:bit==512u?library_field:stream_field,p);
}
static int native_state(struct bd_dashboard_service *s,struct native_status *p,struct bw_native_response *raw) {
    memset(p,0,sizeof *p);memset(raw,0,sizeof *raw);
    if(backend_read(s,"GET","/api/state",NULL,0,NULL,raw)||raw->status!=200||raw->size>BW_NATIVE_DOCUMENT_LIMIT||
       bw_document_fields(raw->bytes,raw->size,status_field,p)||(p->seen&799u)!=799u)return -1;
    return 0;
}
static int http(struct bd_dashboard_service *s,enum bd_http_route route,struct bd_http_reply *out) {
    struct bd_http_options options={.port=route==BD_HTTP_SMP_GAMES?s->config.smp_port:s->config.bfp_port,
        .timeout_ms=s->config.http_timeout_ms};
    return s->config.ops.http?s->config.ops.http(s->config.ops.opaque,route,&options,out):bd_local_http(route,&options,out);
}
struct transfer_status {unsigned seen,ok,busy;};
static int transfer_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct transfer_status *p=opaque;unsigned bit=0,*value=NULL;
    if(kn==2&&!memcmp(key,"ok",2)){bit=1u;value=&p->ok;}
    else if(kn==4&&!memcmp(key,"busy",4)){bit=2u;value=&p->busy;}else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;return boolean(raw,n,value);
}
/* Admission for Blackwire-owned work and scoped source checks. With the
 * optional development observer disabled, this makes no assertion about
 * external transfers. A configured observer never fails open. */
static int source_checks_allowed(struct bd_dashboard_service *s) {
    if(s->config.ops.copy_idle&&s->config.ops.copy_idle(s->config.ops.opaque))return -1;
    if(!s->config.observe_bfp)return 0;
    struct bd_http_reply reply={0};struct transfer_status p={0};
    int result=http(s,BD_HTTP_BFP_JOB_STATUS,&reply)||reply.status!=200||
        bw_document_fields(reply.bytes,reply.size,transfer_field,&p)||p.seen!=3u||!p.ok||p.busy;
    bd_local_http_reply_destroy(&reply);return result?-1:0;
}
static int order(const void *a,const void *b){return strcmp(((const struct bd_item *)a)->id,((const struct bd_item *)b)->id);}
static int equal_item(const struct bd_item *a,const struct bd_item *b) {
    return !strcmp(a->id,b->id)&&!strcmp(a->title,b->title)&&!strcmp(a->title_id,b->title_id)&&
        !strcmp(a->source,b->source)&&!strcmp(a->registered_source,b->registered_source)&&!strcmp(a->cover_ref,b->cover_ref)&&
        !strcmp(a->play_reason,b->play_reason)&&!strcmp(a->send_reason,b->send_reason)&&
        !strcmp(a->uninstall_reason,b->uninstall_reason)&&!strcmp(a->restore_reason,b->restore_reason)&&a->kind==b->kind&&a->flags==b->flags&&
        a->library_generation==b->library_generation&&a->settings_revision==b->settings_revision&&
        a->storage_known==b->storage_known&&a->startup_bytes==b->startup_bytes&&a->pc_bytes==b->pc_bytes&&a->console_allocated_bytes==b->console_allocated_bytes&&
        !memcmp(a->source_fingerprint,b->source_fingerprint,32);
}
static int refresh(struct bd_dashboard_service *s) {
    struct bd_item *items=calloc(BD_CATALOG_MAX,sizeof *items);
    struct bd_smp_record *rows=calloc(BD_CATALOG_MAX,sizeof *rows);
    if(!items||!rows){free(items);free(rows);return -1;}
    struct bd_catalog next;uint64_t generation=s->catalog.generation?s->catalog.generation:1u;
    if(bd_catalog_init(&next,items,BD_CATALOG_MAX,generation)){free(items);free(rows);return -1;}
    struct native_status state;struct bw_native_response raw={0};unsigned pc=0,local=0;
    if(!native_state(s,&state,&raw)){
        size_t skipped=0;
        if(!bd_catalog_append_blackwire(&next,raw.bytes,raw.size,&skipped))pc=!strcmp(state.library_phase,"ready")&&!state.stale;
    }
    bw_native_response_clear(&raw);
    struct bd_http_reply smp={0};size_t count=0;
    if(!http(s,BD_HTTP_SMP_GAMES,&smp)&&smp.status==200&&
       !bd_smp_records_parse(smp.bytes,smp.size,rows,BD_CATALOG_MAX,&count)){
        unsigned allowed=source_checks_allowed(s)==0;
        struct bd_local_environment e={s->apps_root,s->database,allowed,""};local=1;
        for(size_t i=0;i<count;i++){
            if(!strcmp(rows[i].title_id,BD_FRONTEND_TITLE))continue;
            struct bd_local_observation observed;
            if(bd_local_observe(&rows[i],&e,&observed)){local=0;break;}
            /* A prepared thin copy belongs to its PC item. It is never an
             * independently launchable local copy or a second cover. */
            if(observed.item.exclusions&(BD_LOCAL_BLACKWIRE_OWNED|BD_LOCAL_BLACKWIRE_MARKER))continue;
            /* SMP can briefly retain a deleted source in its cached response.
             * Two named ENOENT observations hide that stale cover. A newly
             * copied source at the same path is observed and listed again. */
            if(!(observed.item.facts&BD_LOCAL_SOURCE_PRESENT)){
                struct stat before,after;
                if(lstat(observed.item.source,&before)&&errno==ENOENT&&lstat(observed.item.source,&after)&&errno==ENOENT)continue;
            }
            if(bd_catalog_add_local(&next,&observed.item)){local=0;break;}
            if(next.count&&next.items[next.count-1].kind!=BD_PC_STREAM){
                struct bd_item *item=&next.items[next.count-1];
                if(observed.item.facts==BD_LOCAL_REQUIRED&&!observed.item.exclusions&&!strcmp(item->source,item->registered_source)&&
                   s->config.ops.local_uninstall_capable&&s->config.ops.local_uninstall&&s->config.ops.local_uninstall_state&&
                   s->config.ops.local_uninstall_capable(s->config.ops.opaque,item)==1)item->flags|=BD_CAN_UNINSTALL;
                if((item->flags&BD_CAN_PLAY)&&(!s->config.ops.local_capable||
                   s->config.ops.local_capable(s->config.ops.opaque,item)!=1)){
                    item->flags&=~(BD_CAN_PLAY|BD_READY);
                    snprintf(item->play_reason,sizeof item->play_reason,"This local source is not yet supported by the native launcher.");
                }
            }
        }
        if(!local){while(next.count&&next.items[next.count-1].kind!=BD_PC_STREAM)next.count--;}
    }
    bd_local_http_reply_destroy(&smp);free(rows);
    qsort(items,next.count,sizeof *items,order);
    int changed=s->pc_connected!=pc||s->local_connected!=local||s->catalog.count!=next.count;
    if(!changed)for(size_t i=0;i<next.count;i++)if(!equal_item(&s->catalog.items[i],&next.items[i])){changed=1;break;}
    if(changed&&s->catalog.generation){if(generation==BD_SAFE_INTEGER){free(items);return -1;}generation++;}
    next.generation=generation;for(size_t i=0;i<next.count;i++)items[i].catalog_generation=generation;
    free(s->catalog.items);s->catalog=next;s->pc_connected=pc;s->local_connected=local;return 0;
}
static int frontend_current(struct bd_dashboard_service *s) {
    return s->config.ops.frontend_current?s->config.ops.frontend_current(s->config.ops.opaque,&s->frontend):bd_frontend_current(&s->frontend);
}
static int same_frontend(const struct bd_frontend_identity *a,const struct bd_frontend_identity *b){
    return a->pid==b->pid&&a->app_id==b->app_id&&a->birth_sec==b->birth_sec&&a->birth_usec==b->birth_usec&&
        !memcmp(a->title_id,b->title_id,sizeof a->title_id)&&!memcmp(a->package_fingerprint,b->package_fingerprint,32);
}
/* Pure cached-owner comparison under the service lock. An admitted but
 * invalid owner is unknown, never equivalent to no frontend. */
static int settings_owner_same(struct bd_dashboard_service *s,const struct bd_frontend_identity *expected){
    struct bd_frontend_identity observed;
    int missing=bd_frontend_get(&s->frontend,&observed);
    return !(missing&&s->frontend.admitted)&&same_frontend(expected,&observed);
}
static int settings_idle(void *opaque,uint32_t operation){
    struct bd_dashboard_service *s=opaque;struct bd_frontend_owner owner;struct bd_frontend_identity expected;
    if(pthread_mutex_lock(&s->lock))return -1;
    int missing=bd_frontend_get(&s->frontend,&expected);
    int bad=!operation||s->active||s->reserved||!s->settings_guard_attached||(missing&&s->frontend.admitted);
    if(!bad){if(s->settings_operation!=operation){s->settings_operation=operation;s->settings_owner=expected;s->settings_authority=SETTINGS_UNSET;}
        else if(!same_frontend(&expected,&s->settings_owner))bad=1;}
    enum settings_authority authority=s->settings_authority;
    owner=s->frontend;pthread_mutex_unlock(&s->lock);if(bad)return -1;
    int first=missing?0:s->config.ops.frontend_current?s->config.ops.frontend_current(s->config.ops.opaque,&owner):bd_frontend_current(&owner);
    if(first!=0&&first!=1)return -1;
    if(authority==SETTINGS_UNSET)authority=first==1?SETTINGS_NATIVE_FRONTEND:SETTINGS_FULL_IDLE;
    /* Fix the authority for this setup operation before any permission can
     * succeed. Later callbacks cannot switch between native and browser. */
    if(pthread_mutex_lock(&s->lock))return -1;
    bad=s->active||s->reserved||!s->settings_guard_attached||s->settings_operation!=operation||
        !settings_owner_same(s,&expected)||!same_frontend(&expected,&s->settings_owner)||
        (s->settings_authority!=SETTINGS_UNSET&&s->settings_authority!=authority);
    if(!bad)s->settings_authority=authority;
    pthread_mutex_unlock(&s->lock);if(bad)return -1;
    if(authority==SETTINGS_NATIVE_FRONTEND){
        if(first!=1||bd_settings_log_idle(s->settings_game_log,&expected))return -1;
    }else if(first!=0||!s->config.ops.idle||s->config.ops.idle(s->config.ops.opaque))return -1;
    int last=missing?0:s->config.ops.frontend_current?s->config.ops.frontend_current(s->config.ops.opaque,&owner):bd_frontend_current(&owner);
    if(last!=(authority==SETTINGS_NATIVE_FRONTEND?1:0)||
       (authority==SETTINGS_FULL_IDLE&&s->config.ops.idle(s->config.ops.opaque))||pthread_mutex_lock(&s->lock))return -1;
    bad=s->active||s->reserved||!s->settings_guard_attached||s->settings_operation!=operation||s->settings_authority!=authority||
        !settings_owner_same(s,&expected)||!same_frontend(&expected,&s->settings_owner);
    pthread_mutex_unlock(&s->lock);return bad?-1:0;
}
int bd_service_attach_settings_guard(struct bd_dashboard_service *s){
    if(!s||pthread_mutex_lock(&s->lock))return -1;
    int result=s->active||s->reserved?1:bw_native_backend_set_settings_idle(s->config.backend,settings_idle,s);
    if(!result)s->settings_guard_attached=1;
    pthread_mutex_unlock(&s->lock);return result;
}
static int expected_frontend(void *opaque,const struct bd_frontend_identity *expected) {
    struct bd_dashboard_service *s=opaque;struct bd_frontend_identity original;
    if(bd_frontend_get(&s->frontend,&original)||original.pid!=expected->pid||original.app_id!=expected->app_id||
       original.birth_sec!=expected->birth_sec||original.birth_usec!=expected->birth_usec||strcmp(original.title_id,expected->title_id)||
       memcmp(original.package_fingerprint,expected->package_fingerprint,32))return -1;
    return frontend_current(s);
}
static int selected_current(void *opaque,const struct bd_item *selected) {
    struct bd_dashboard_service *s=opaque;if(refresh(s))return -1;
    const struct bd_item *current=bd_catalog_find(&s->catalog,selected->id);
    return !current||!equal_item(current,selected)?-1:0;
}
static int full_idle(void *opaque) {
    struct bd_dashboard_service *s=opaque;
    return s->config.ops.idle(s->config.ops.opaque)||source_checks_allowed(s)?-1:0;
}
static int preparation_absent(struct bd_dashboard_service *s,const struct bd_frontend_owner *owner,const struct bd_frontend_identity *expected){
    if(expected->pid<=1)return 0;struct rc_game_identity observed;
    int state=s->config.ops.preparation_identity_ops?
        rc_game_identity_current_with_ops(s->config.ops.preparation_identity_ops,&owner->original,&observed):
        rc_game_identity_current(&owner->original,&observed);
    return state==RC_GAME_ABSENT?0:-1;
}
static int preparation_idle(void *opaque,const char operation[33],const char run[33],enum bw_prepare_guard_point point){
    struct bd_dashboard_service *s=opaque;struct bd_frontend_preparation *pin=NULL;struct bd_frontend_owner owner;struct bd_frontend_identity expected;unsigned native=0;
    if(!operation||!run||point>BW_PREPARE_RELEASE||pthread_mutex_lock(&s->lock))return -1;
    int bad=!s->active||s->intent.action!=BD_SEND||!s->preparation_guard_attached||strcmp(operation,s->intent.nonce)||s->preparation_borrowed;
    if(point==BW_PREPARE_RELEASE){
        if(!bad&&s->backend_id[0]&&strcmp(run,s->backend_id))bad=1;
        if(!bad){pin=s->preparation_pin;s->preparation_pin=NULL;}
        pthread_mutex_unlock(&s->lock);return bad?-1:bd_frontend_preparation_close(pin);
    }
    bad=bad||!nonce_valid(run)||strcmp(run,s->backend_id)||!settings_owner_same(s,&s->preparation_identity);
    if(!bad){pin=s->preparation_pin;owner=s->preparation_owner;expected=s->preparation_identity;native=s->preparation_native;s->preparation_borrowed=1;}
    pthread_mutex_unlock(&s->lock);if(bad)return -1;
    if(native){
        bad=!pin||(point==BW_PREPARE_CONTROL?bd_frontend_preparation_control(pin):bd_frontend_preparation_current(pin))||
            bd_preparation_log_idle(s->settings_game_log,&expected)||bd_frontend_preparation_current(pin);
    }else bad=preparation_absent(s,&owner,&expected)||s->config.ops.idle(s->config.ops.opaque)||
        preparation_absent(s,&owner,&expected)||s->config.ops.idle(s->config.ops.opaque);
    if(!bad)bad=source_checks_allowed(s);
    if(pthread_mutex_lock(&s->lock))return -1;
    bad=bad||!s->active||s->intent.action!=BD_SEND||!s->preparation_guard_attached||strcmp(operation,s->intent.nonce)||
        strcmp(run,s->backend_id)||s->preparation_pin!=pin||s->preparation_native!=native||!settings_owner_same(s,&expected)||
        !same_frontend(&expected,&s->preparation_identity);
    s->preparation_borrowed=0;pthread_mutex_unlock(&s->lock);return bad?-1:0;
}
int bd_service_attach_preparation_guard(struct bd_dashboard_service *s){
    if(!s||pthread_mutex_lock(&s->lock))return -1;
    int rc=s->active||s->reserved||s->preparation_pin?1:bw_native_backend_set_preparation_idle(s->config.backend,preparation_idle,s);
    if(!rc)s->preparation_guard_attached=1;pthread_mutex_unlock(&s->lock);return rc;
}
/* Initial Send control runs under the existing request lock, before a worker
 * can borrow the witness. Worker callbacks later take this lock only after the
 * backend has released its own; no synchronous request calls back into it. */
static int preparation_begin(struct bd_dashboard_service *s,int present,const struct bd_frontend_identity *frontend){
    if(s->preparation_pin||s->preparation_borrowed||!s->preparation_guard_attached)return -1;
    s->preparation_owner=s->frontend;s->preparation_identity=*frontend;s->preparation_native=present==1;
    memset(&s->preparation_progress,0,sizeof s->preparation_progress);
    if(present==1){
        int rc=s->config.ops.preparation_identity_ops?
            bd_frontend_preparation_open_with_ops(&s->frontend,s->config.ops.preparation_identity_ops,&s->preparation_pin):
            bd_frontend_preparation_open(&s->frontend,&s->preparation_pin);
        if(rc||bd_preparation_log_idle(s->settings_game_log,frontend))goto failed;
    }else if(preparation_absent(s,&s->preparation_owner,frontend)||s->config.ops.idle(s->config.ops.opaque)||
        preparation_absent(s,&s->preparation_owner,frontend)||s->config.ops.idle(s->config.ops.opaque))goto failed;
    if(source_checks_allowed(s)||!settings_owner_same(s,frontend))goto failed;return 0;
failed:if(s->preparation_pin){struct bd_frontend_preparation *pin=s->preparation_pin;s->preparation_pin=NULL;(void)bd_frontend_preparation_close(pin);}return -1;
}
static int preparation_dispatch_idle(struct bd_dashboard_service *s){
    if(!s->preparation_guard_attached||s->preparation_borrowed||!settings_owner_same(s,&s->preparation_identity))return -1;
    if(s->preparation_native)return !s->preparation_pin||bd_frontend_preparation_current(s->preparation_pin)||
        bd_preparation_log_idle(s->settings_game_log,&s->preparation_identity)||bd_frontend_preparation_current(s->preparation_pin)?-1:0;
    return preparation_absent(s,&s->preparation_owner,&s->preparation_identity)||s->config.ops.idle(s->config.ops.opaque)||
        preparation_absent(s,&s->preparation_owner,&s->preparation_identity)||s->config.ops.idle(s->config.ops.opaque)?-1:0;
}
static int intent_clock(void *opaque,uint64_t *out){return now_ms(opaque,out);}
static int release(struct bd_dashboard_service *s) {
    if(!s->reserved)return 0;
    if(s->config.ops.release(s->config.ops.opaque,s->intent.nonce)){
        message(s,"unknown","The operation reservation could not be released. No retry will be made.");s->active=1;s->intent.phase=BD_INTENT_UNKNOWN;return -1;
    }
    s->reserved=0;memset(s->action_token,0,sizeof s->action_token);return 0;
}
struct accepted {char id[33];unsigned seen,ok;};
static int accepted_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct accepted *p=opaque;unsigned bit=0;
    if(kn==2&&!memcmp(key,"ok",2))bit=1u;
    else if((kn==5&&!memcmp(key,"runId",5))||(kn==5&&!memcmp(key,"jobId",5))||(kn==11&&!memcmp(key,"operationId",11)))bit=2u;else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;
    return bit==1u?boolean(raw,n,&p->ok):bw_document_string(raw,n,p->id,sizeof p->id);
}
static int dispatch(void *opaque,const struct bd_item *item,enum bd_action action,const char *nonce) {
    struct bd_dashboard_service *s=opaque;
    if(!s->reserved||strcmp(nonce,s->intent.nonce)||
       (action==BD_SEND?(source_checks_allowed(s)||preparation_dispatch_idle(s)):full_idle(s))||selected_current(s,item)||
       (action==BD_SEND&&preparation_dispatch_idle(s)))return 0;
    if(item->kind!=BD_PC_STREAM){
        if(action==BD_UNINSTALL){
            if(!s->config.ops.local_uninstall||!s->config.ops.local_uninstall_state||strcmp(s->confirmed_title,item->title_id))return 0;
            s->local_session=2;return s->config.ops.local_uninstall(s->config.ops.opaque,item,nonce,s->confirmed_title);
        }
        if(action!=BD_PLAY)return 0;s->local_session=1;return s->config.ops.local_launch(s->config.ops.opaque,item,nonce);
    }
    struct native_status state;struct bw_native_response raw={0};
    int bad=native_state(s,&state,&raw)||!same_token(state.token,s->action_token);
    bw_native_response_clear(&raw);if(bad)return 0;
    struct bw_json j={.maximum=4096};bw_json_text(&j,"{\"name\":");bw_json_string(&j,item->source);
    bw_json_format(&j,",\"libraryGeneration\":%llu,\"settingsRevision\":%u",(unsigned long long)item->library_generation,item->settings_revision);
    if(action==BD_UNINSTALL){if(strcmp(s->confirmed_title,item->title_id)){free(j.bytes);return 0;}bw_json_text(&j,",\"confirmedTitleId\":");bw_json_string(&j,s->confirmed_title);}
    bw_json_text(&j,"}");
    if(j.bad){free(j.bytes);return 0;}
    const char *route=action==BD_SEND?"/api/send":action==BD_UNINSTALL?"/api/uninstall":action==BD_RESTORE?"/api/restore":"/api/start";
    int rc=s->config.ops.backend_reserved(s->config.ops.opaque,nonce,"POST",route,j.bytes,j.used,s->action_token,&raw);
    free(j.bytes);struct accepted accepted={0};
    int result=rc?-1:raw.status==202?
        (bw_document_fields(raw.bytes,raw.size,accepted_field,&accepted)||accepted.seen!=3u||!accepted.ok||!nonce_valid(accepted.id)?-1:1):
        raw.status>=400&&raw.status<500?0:-1;
    if(result==1&&removal_action(action)&&strcmp(accepted.id,nonce))result=-1;
    if(result==1)memcpy(s->backend_id,accepted.id,sizeof s->backend_id);
    bw_native_response_clear(&raw);
    /* Native202 transfers ownership to the unchanged retained worker. Release
     * this separate reservation only after that known acceptance. */
    if(result==1&&!removal_action(action)&&release(s))return -1;
    return result;
}
static struct bd_intent_ops intent_ops(struct bd_dashboard_service *s) {
    return (struct bd_intent_ops){s,intent_clock,expected_frontend,full_idle,selected_current,dispatch};
}
struct job_status {const char *id;char state[24],message[384];unsigned seen,found,progress_seen,rows;struct bw_source_progress progress;};
static int job_progress_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct job_status *p=opaque;unsigned bit=0;uint64_t value=0,max=UINT32_MAX;
    if(kn==5&&!memcmp(key,"phase",5)){bit=1;max=BW_SOURCE_ERROR;}
    else if(kn==5&&!memcmp(key,"files",5))bit=2;
    else if(kn==10&&!memcmp(key,"filesTotal",10))bit=4;
    else if(kn==5&&!memcmp(key,"bytes",5)){bit=8;max=BD_SAFE_INTEGER;}
    else if(kn==10&&!memcmp(key,"bytesTotal",10)){bit=16;max=BD_SAFE_INTEGER;}else return 0;
    if(p->progress_seen&bit||decimal(raw,n,max,&value))return -1;p->progress_seen|=bit;
    if(bit==1)p->progress.phase=(enum bw_source_phase)value;
    else if(bit==2)p->progress.files_done=(uint32_t)value;
    else if(bit==4)p->progress.files_total=(uint32_t)value;
    else if(bit==8)p->progress.bytes_done=value;else p->progress.bytes_total=value;return 0;
}
static int job_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct job_status *p=opaque;
    if(kn==2&&!memcmp(key,"id",2)){char id[33];if(p->seen&1u||bw_document_string(raw,n,id,sizeof id))return -1;p->seen|=1u;p->found=!strcmp(id,p->id);}
    else if(kn==5&&!memcmp(key,"state",5)){if(p->seen&2u||bw_document_string(raw,n,p->state,sizeof p->state))return -1;p->seen|=2u;}
    else if(kn==7&&!memcmp(key,"message",7)){if(p->seen&4u||bw_document_string(raw,n,p->message,sizeof p->message))return -1;p->seen|=4u;}
    else if(kn==8&&!memcmp(key,"progress",8)){
        if(p->seen&8u||bw_document_fields(raw,n,job_progress_field,p)||p->progress_seen!=31u||
           p->progress.files_done>p->progress.files_total||p->progress.bytes_done>p->progress.bytes_total)return -1;
        p->seen|=8u;
    }return 0;
}
static int job_item(void *opaque,const void *raw,size_t n) {
    struct job_status *found=opaque,one={.id=found->id};
    if(++found->rows>BW_NATIVE_JOB_LIMIT||bw_document_fields(raw,n,job_field,&one)||(one.seen&7u)!=7u)return -1;
    if(one.found){if(found->found)return -1;unsigned rows=found->rows;*found=one;found->rows=rows;}return 0;
}
static int jobs_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    if(kn==4&&!memcmp(key,"jobs",4))return bw_document_array(raw,n,job_item,opaque);return 0;
}
struct removal_status {
    char id[33],source[BD_SOURCE_BYTES],title[10],state[16],message[BD_REASON_BYTES];
    unsigned seen,root_seen,phase,files,files_total,removed;
};
static int removal_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct removal_status *p=opaque;unsigned bit=0;char *dest=NULL;size_t cap=0;uint64_t value=0;
#define RSTR(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;dest=p->m;cap=sizeof p->m;}
    RSTR("operationId",1u,id) else RSTR("source",2u,source) else RSTR("titleId",4u,title)
    else RSTR("state",8u,state) else RSTR("message",16u,message)
#undef RSTR
    else if(kn==5&&!memcmp(key,"phase",5))bit=32u;else if(kn==5&&!memcmp(key,"files",5))bit=64u;
    else if(kn==10&&!memcmp(key,"filesTotal",10))bit=128u;else if(kn==7&&!memcmp(key,"removed",7))bit=256u;else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;if(dest)return bw_document_string(raw,n,dest,cap);
    if(bit==256u)return boolean(raw,n,&p->removed);
    if(decimal(raw,n,bit==32u?BW_GAME_REMOVE_UNKNOWN:UINT32_MAX,&value))return -1;
    if(bit==32u)p->phase=(unsigned)value;else if(bit==64u)p->files=(unsigned)value;else p->files_total=(unsigned)value;return 0;
}
static int removal_root(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct removal_status *p=opaque;if(kn!=7||memcmp(key,"removal",7))return 0;
    if(p->root_seen++)return -1;return bw_document_fields(raw,n,removal_field,p);
}
static void removal_message(struct bd_dashboard_service *s,const struct bw_game_uninstall_result *result,int status){
    s->removal_progress=*result;s->removal_observed=1;
    if(status>=0&&result->phase!=BW_GAME_REMOVE_UNKNOWN)s->intent.phase=BD_INTENT_ACCEPTED;
    if(status==1&&result->phase==BW_GAME_REMOVE_DONE&&result->library_removed){
        if(!release(s)){s->active=0;message(s,"stopped",s->intent.action==BD_RESTORE?
            "Restored to the library. Prepare startup files before playing.":"The selected console copy and its library entry were removed. Saved games are preserved.");feedback_update(s,FB_STARTED);}
    }else if(status==1&&result->phase==BW_GAME_REMOVE_ERROR&&!result->mutation_started){
        if(!release(s)){s->active=0;s->intent.phase=BD_INTENT_REFUSED;message(s,"refused",result->reason?result->reason:"The removal was refused before changing the console copy.");feedback_update(s,FB_FAILED);}
    }else {message(s,status<0||result->phase==BW_GAME_REMOVE_UNKNOWN?"unknown":"accepted",result->reason?result->reason:"Working on the selected console copy.");
        feedback_update(s,status<0||result->phase==BW_GAME_REMOVE_UNKNOWN?FB_UNKNOWN:FB_CHECKING);}
}
static int tick_locked(struct bd_dashboard_service *s) {
    if(!s->active||s->removal_observing)return 0;
    if(s->intent.action==BD_SEND)s->preparation_progress.observed=s->preparation_progress.ready=0;
    if(s->intent.phase==BD_INTENT_WAIT_EXIT){
        struct bd_intent_ops ops=intent_ops(s);(void)bd_intent_tick(&s->intent,&ops);
        if(s->intent.phase==BD_INTENT_WAIT_EXIT){feedback_update(s,FB_QUEUED);return 0;}
        if(s->intent.phase!=BD_INTENT_ACCEPTED){
            message(s,s->intent.phase==BD_INTENT_UNKNOWN?"unknown":"refused",s->intent.message);
            feedback_update(s,s->intent.phase==BD_INTENT_UNKNOWN?FB_UNKNOWN:FB_FAILED);
            if(!s->intent.dispatch_calls&&release(s)==0)s->active=0;
            else if(s->intent.phase==BD_INTENT_REFUSED&&release(s)==0)s->active=0;
            return 0;
        }
        message(s,"accepted",s->intent.message);
    }
    if(s->local_session==2&&s->intent.dispatch_calls&&
       (s->intent.phase==BD_INTENT_ACCEPTED||s->intent.phase==BD_INTENT_UNKNOWN)){
        struct bw_game_uninstall_result result={0};int status=s->config.ops.local_uninstall_state?
            s->config.ops.local_uninstall_state(s->config.ops.opaque,s->intent.nonce,&result):-1;
        removal_message(s,&result,status);return 0;
    }
    if(s->local_session==1&&s->intent.dispatch_calls&&
       (s->intent.phase==BD_INTENT_ACCEPTED||s->intent.phase==BD_INTENT_UNKNOWN)){
        int state=s->config.ops.local_state(s->config.ops.opaque,s->intent.nonce);
        if(state==1){s->intent.phase=BD_INTENT_ACCEPTED;message(s,"running","The exact local game process is present. Menu and gameplay are separate observations.");feedback_update(s,FB_STARTED);}
        else if(state==0){if(!release(s)){s->active=0;message(s,"stopped","The exact local game exited; its operation reservation was released.");feedback_update(s,FB_STOPPED);}}
        else {s->intent.phase=BD_INTENT_UNKNOWN;message(s,"unknown","The local launch lifecycle is uncertain; ownership is retained and no retry will be made.");feedback_update(s,FB_UNKNOWN);}
        return 0;
    }
    if(s->intent.phase!=BD_INTENT_ACCEPTED&&!(removal_action(s->intent.action)&&s->intent.phase==BD_INTENT_UNKNOWN))return 0;
    struct native_status state;struct bw_native_response raw={0};
    if(native_state(s,&state,&raw)){bw_native_response_clear(&raw);if(removal_action(s->intent.action))s->removal_observed=0;
        message(s,"unknown","The retained native operation status could not be read.");feedback_update(s,FB_UNKNOWN);return 0;}
    if(removal_action(s->intent.action)){
        struct removal_status observed={0};
        if(bw_document_fields(raw.bytes,raw.size,removal_root,&observed)||observed.root_seen!=1||observed.seen!=511u||
           (observed.files_total&&observed.files>observed.files_total)||strcmp(observed.id,s->intent.nonce)||(s->backend_id[0]&&strcmp(observed.id,s->backend_id))||
           strcmp(observed.source,s->intent.selected.source)||strcmp(observed.title,s->intent.selected.title_id)||
           (strcmp(observed.state,"running")&&strcmp(observed.state,"done")&&strcmp(observed.state,"error")&&strcmp(observed.state,"unknown"))){
            s->removal_observed=0;message(s,"unknown","The exact removal status could not be confirmed. No request was repeated.");
        }else{
            /* Removal uses the held service nonce as its backend operation ID.
             * A lost ACK can be resolved by this exact typed observation;
             * this adopts no unrelated same-title job and sends no request. */
            if(!s->backend_id[0])memcpy(s->backend_id,observed.id,sizeof s->backend_id);
            if(s->intent.action==BD_UNINSTALL&&!strcmp(observed.state,"unknown"))s->removal_consumed_unknown=1;
            struct bw_game_uninstall_result result={.phase=(enum bw_game_uninstall_phase)observed.phase,.files_removed=observed.files,.files_total=observed.files_total,.reason=observed.message,
                .library_removed=!strcmp(observed.state,"done")&&observed.removed==(s->intent.action==BD_UNINSTALL)};
            int terminal=!strcmp(observed.state,"done")||!strcmp(observed.state,"error");
            int status=terminal?1:!strcmp(observed.state,"unknown")?-1:0;
            if(s->removal_consumed_unknown&&!strcmp(observed.state,"error")){
                /* A consumed uncertain mutation cannot later be downgraded
                 * to a pre-mutation refusal. Only exact DONE may release. */
                result.phase=BW_GAME_REMOVE_UNKNOWN;result.mutation_started=1;status=-1;
                result.reason="The consumed removal is still uncertain. Ownership is retained until exact completion is observed.";
            }
            removal_message(s,&result,status);
            /* The parsed reason is stack storage; status text is already copied. */
            s->removal_progress.reason=NULL;
        }
    }else if(s->intent.action==BD_PLAY){
        if(strcmp(state.stream_id,s->backend_id)){message(s,"unknown","The native session identity no longer matches this selection.");feedback_update(s,FB_UNKNOWN);}
        else {int terminal=!strcmp(state.stream_phase,"stopped")||!strcmp(state.stream_phase,"error");
            message(s,!strcmp(state.stream_phase,"error")?"refused":terminal?"stopped":state.game_pid>1u?"running":"accepted",state.stream_message);if(terminal)s->active=0;
            feedback_update(s,!strcmp(state.stream_phase,"error")?FB_FAILED:!strcmp(state.stream_phase,"stopped")||!strcmp(state.stream_phase,"closing")?FB_STOPPED:
                !strcmp(state.stream_phase,"running")&&state.game_pid>1u?FB_STARTED:!strcmp(state.stream_phase,"launching")?FB_STARTING:
                !strcmp(state.stream_phase,"preflight")||!strcmp(state.stream_phase,"preparing")?FB_CHECKING:FB_UNKNOWN);}
    }else{
        struct job_status job={.id=s->backend_id};
        s->preparation_progress.observed=s->preparation_progress.ready=0;
        if(bw_document_fields(raw.bytes,raw.size,jobs_field,&job)||!job.found)message(s,"unknown","The exact preparation job could not be read; its outcome is retained.");
        else {int terminal=!strcmp(job.state,"done")||!strcmp(job.state,"error");
            if(!terminal&&strcmp(job.state,"queued")&&strcmp(job.state,"running"))message(s,"unknown","The exact native job state is not recognized.");
            else {
                snprintf(s->preparation_progress.state,sizeof s->preparation_progress.state,"%s",job.state);
                snprintf(s->preparation_progress.message,sizeof s->preparation_progress.message,"%s",job.message);
                s->preparation_progress.progress=job.progress;
                s->preparation_progress.observed=(job.seen&8u)!=0&&(job.progress.phase!=BW_SOURCE_IDLE||job.progress.files_total||job.progress.bytes_total);
                message(s,!strcmp(job.state,"error")?"refused":terminal?"stopped":"accepted",job.message);if(terminal)s->active=0;
            }}
    }
    bw_native_response_clear(&raw);return 0;
}
int bd_service_tick(struct bd_dashboard_service *s) {
    if(!s||pthread_mutex_lock(&s->lock))return -1;int rc=tick_locked(s);struct feedback_delivery delivery;char observe[33]={0};uint64_t now=0;
    if(s->active&&s->reserved&&!s->local_session&&!s->removal_observing&&s->intent.action==BD_UNINSTALL&&
       s->intent.selected.kind==BD_PC_STREAM&&s->intent.dispatch_calls&&s->removal_observed&&
       s->removal_progress.phase==BW_GAME_REMOVE_UNKNOWN&&nonce_valid(s->intent.nonce)&&
       !now_ms(s,&now)&&now>=s->removal_observe_next_ms&&now<=UINT64_MAX-5000u&&!full_idle(s)){
        memcpy(observe,s->intent.nonce,sizeof observe);s->removal_observing=1;s->removal_observe_next_ms=now+5000u;
    }
    feedback_take(s,&delivery);pthread_mutex_unlock(&s->lock);feedback_deliver(&delivery);
    if(observe[0]){
        struct bw_native_cleanup_observation observed={0};
        /* This never supplies terminal authority itself. A fresh typed status
         * below must still match operation, source and title before release. */
        if(s->config.ops.removal_observe)(void)s->config.ops.removal_observe(s->config.ops.opaque,observe,&observed);
        else (void)bw_native_backend_registration_observe(s->config.backend,observe,&observed);
        if(pthread_mutex_lock(&s->lock))return -1;s->removal_observing=0;
        if(s->active&&s->reserved&&s->intent.action==BD_UNINSTALL&&!strcmp(s->intent.nonce,observe))rc=tick_locked(s);
        feedback_take(s,&delivery);pthread_mutex_unlock(&s->lock);feedback_deliver(&delivery);
    }
    return rc;
}
static int valid_path(const char *p) {
    if(!p||p[0]!='/'||strnlen(p,BD_SOURCE_BYTES)>=BD_SOURCE_BYTES||strstr(p,"/../")||strstr(p,"/./")||strchr(p,'\\'))return 0;
    size_t n=strlen(p);return n>1&&p[n-1]!='/'&&strcmp(p+n-(n>=3?3:0),"/..")&&strcmp(p+n-(n>=2?2:0),"/.");
}
struct bd_dashboard_service *bd_service_create(const struct bd_service_config *config) {
    if(!config||!config->backend||!config->ops.idle||!config->ops.reserve||!config->ops.release||!config->ops.backend_reserved||
       !config->ops.local_launch||!config->ops.local_state||config->observe_bfp>1||config->http_timeout_ms>5000u||config->native_frontend_disabled>1u)return NULL;
    const char *apps=config->apps_root?config->apps_root:"/user/app";
    const char *db=config->database?config->database:"/system_data/priv/mms/app.db";
    const char *game_log=config->settings_game_log?config->settings_game_log:"/data/shadowmount/debug.log";
    if(!valid_path(apps)||!valid_path(db)||!valid_path(game_log))return NULL;
    struct bd_dashboard_service *s=calloc(1,sizeof *s);if(!s)return NULL;
    s->config=*config;s->config.http_timeout_ms=config->http_timeout_ms?config->http_timeout_ms:2000u;
    snprintf(s->apps_root,sizeof s->apps_root,"%s",apps);snprintf(s->database,sizeof s->database,"%s",db);
    snprintf(s->settings_game_log,sizeof s->settings_game_log,"%s",game_log);
    if((!config->native_frontend_disabled&&bd_frontend_init(&s->frontend,&config->frontend_package))||pthread_mutex_init(&s->lock,NULL)){free(s);return NULL;}
    message(s,"idle","No dashboard action has been submitted.");return s;
}
struct hello_request {int32_t pid;uint32_t app;unsigned seen;};
static int hello_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct hello_request *p=opaque;unsigned bit=0;uint64_t value=0;
    if(kn==3&&!memcmp(key,"pid",3))bit=1u;else if(kn==5&&!memcmp(key,"appId",5))bit=2u;else return -1;
    if(p->seen&bit||decimal(raw,n,INT32_MAX,&value)||value<2u)return -1;p->seen|=bit;
    if(bit==1u)p->pid=(int32_t)value;else p->app=(uint32_t)value;return 0;
}
struct action_request {char id[BD_ID_BYTES],action[16],confirmed_title[10];uint64_t generation;uint32_t kind;unsigned seen;};
static int action_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n) {
    struct action_request *p=opaque;unsigned bit=0;uint64_t value=0;
    if(kn==2&&!memcmp(key,"id",2))bit=1u;else if(kn==4&&!memcmp(key,"kind",4))bit=2u;
    else if(kn==6&&!memcmp(key,"action",6))bit=4u;else if(kn==17&&!memcmp(key,"catalogGeneration",17))bit=8u;
    else if(kn==16&&!memcmp(key,"confirmedTitleId",16))bit=16u;else return -1;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1u)return bw_document_string(raw,n,p->id,sizeof p->id);
    if(bit==4u)return bw_document_string(raw,n,p->action,sizeof p->action);
    if(bit==16u)return bw_document_string(raw,n,p->confirmed_title,sizeof p->confirmed_title);
    if(decimal(raw,n,bit==2u?4u:BD_SAFE_INTEGER,&value)||!value)return -1;
    if(bit==2u)p->kind=(uint32_t)value;else p->generation=value;return 0;
}
static int authorize(struct bd_dashboard_service *s,const char *token) {
    struct native_status state;struct bw_native_response raw={0};
    int rc=native_state(s,&state,&raw)||!same_token(token,state.token);bw_native_response_clear(&raw);return rc?-1:0;
}
/* Retiring a completed frontend owner needs exact process absence, not a
 * second qualification of its old installation. The replacement candidate
 * still performs the complete current source/Sony/birth admission below.
 * Each public identity call independently proves two exact-PID absences;
 * a live or reused PID, malformed capture and uncertainty never retire it. */
static int frontend_retired(struct bd_dashboard_service *s,const struct bd_frontend_owner *owner,
                            const struct bd_frontend_identity *expected) {
    struct bd_frontend_identity captured;
    return expected->pid<=1||bd_frontend_get(owner,&captured)||!same_frontend(&captured,expected)||
        preparation_absent(s,owner,expected)||preparation_absent(s,owner,expected)?-1:0;
}
static int hello(struct bd_dashboard_service *s,const void *raw,size_t size,struct bw_native_response *out) {
    struct hello_request request={0};
    if(!size||size>1024||bw_document_fields(raw,size,hello_field,&request)||request.seen!=3u)
        return text(out,400,"Choose the exact native frontend PID and app ID.");
    struct bd_frontend_identity prior;
    if(!bd_frontend_get(&s->frontend,&prior)&&(prior.pid!=request.pid||prior.app_id!=request.app)){
        /* Hello may arrive before the ticker observes the previous worker's
         * terminal result. Reconcile observations only: never advance a queued
         * WAIT_EXIT intent or invoke another dispatch during admission. */
        if(s->active&&s->intent.dispatch_calls&&
           (s->intent.phase==BD_INTENT_ACCEPTED||(s->local_session&&s->intent.phase==BD_INTENT_UNKNOWN)))
            (void)tick_locked(s);
        if(s->active||s->reserved||s->preparation_pin||s->preparation_borrowed)
            return text(out,409,"The previous Blackwire operation is still active or awaiting confirmation.");
        if(frontend_retired(s,&s->frontend,&prior))return text(out,409,"The previous dashboard process is still present or uncertain.");
        struct bd_frontend_owner candidate;
        if(bd_frontend_init(&candidate,&s->config.frontend_package))return text(out,503,"The expected frontend tuple is unavailable.");
        int admitted=s->config.ops.frontend_admit?s->config.ops.frontend_admit(s->config.ops.opaque,&candidate,request.pid,request.app):
            bd_frontend_admit(&candidate,request.pid,request.app);
        if(admitted!=1||frontend_retired(s,&s->frontend,&prior))
            return text(out,409,"The replacement dashboard identity could not be confirmed.");
        s->frontend=candidate;
    }else {
        int admitted=s->config.ops.frontend_admit?s->config.ops.frontend_admit(s->config.ops.opaque,&s->frontend,request.pid,request.app):
            bd_frontend_admit(&s->frontend,request.pid,request.app);
        if(admitted!=1)return text(out,409,"The exact native frontend identity could not be admitted.");
    }
    const char body[]="{\"ok\":true,\"admitted\":true,\"frontendTitleId\":\"" BD_FRONTEND_TITLE "\"}";
    out->bytes=malloc(sizeof body-1);if(!out->bytes)return -1;memcpy(out->bytes,body,sizeof body-1);
    out->size=sizeof body-1;out->status=200;out->type="application/json";return 0;
}
static int accepted_reply(struct bd_dashboard_service *s,struct bw_native_response *out) {
    struct bw_json j={.maximum=4096};bw_json_text(&j,"{\"ok\":true,\"accepted\":true,\"id\":");bw_json_string(&j,s->intent.nonce);
    bw_json_text(&j,",\"phase\":");bw_json_string(&j,s->intent.phase==BD_INTENT_WAIT_EXIT?"waiting-for-dashboard-exit":"accepted");
    bw_json_text(&j,",\"message\":");bw_json_string(&j,s->intent.action==BD_SEND?"Preparing startup files. You can keep browsing Blackwire.":s->message);
    bw_json_text(&j,",\"action\":");bw_json_string(&j,action_name(s->intent.action));
    bw_json_format(&j,",\"exitRequested\":%s}",s->intent.action!=BD_SEND&&s->intent.phase==BD_INTENT_WAIT_EXIT?"true":"false");
    if(j.bad){free(j.bytes);return -1;}*out=(struct bw_native_response){202,"application/json",j.bytes,j.used};return 0;
}
static int action(struct bd_dashboard_service *s,const void *raw,size_t size,const char *token,struct bw_native_response *out) {
    struct action_request r={0};
    if(!size||size>4096||bw_document_fields(raw,size,action_field,&r)||
       (strcmp(r.action,"play")&&strcmp(r.action,"send")&&strcmp(r.action,"uninstall")&&strcmp(r.action,"restore"))||
       r.seen!=(!strcmp(r.action,"uninstall")?31u:15u))return text(out,400,"Choose one exact current catalog action and confirm the selected title for Uninstall.");
    if(s->active||s->reserved)return text(out,409,"The previous operation remains active or uncertain.");
    if(refresh(s))return text(out,503,"The catalog could not be refreshed.");
    const struct bd_item *found=bd_catalog_find(&s->catalog,r.id);
    enum bd_action selected=!strcmp(r.action,"play")?BD_PLAY:!strcmp(r.action,"send")?BD_SEND:!strcmp(r.action,"uninstall")?BD_UNINSTALL:BD_RESTORE;
    if(!found||found->kind!=(enum bd_source_kind)r.kind||r.generation!=s->catalog.generation||
       (selected==BD_PLAY?!bd_item_can_play(found):selected==BD_SEND?!bd_item_can_send(found):selected==BD_UNINSTALL?!bd_item_can_uninstall(found):!bd_item_can_restore(found))||
       (selected==BD_UNINSTALL&&strcmp(r.confirmed_title,found->title_id)))
        return text(out,409,"The selected source or catalog generation changed; refresh and choose it again.");
    struct bd_item item=*found;char nonce[33]={0};
    if((s->config.ops.nonce?s->config.ops.nonce(s->config.ops.opaque,nonce):bw_native_setup_nonce(nonce))||!nonce_valid(nonce)||
       !strcmp(nonce,s->intent.nonce))return text(out,503,"A fresh operation identity could not be generated.");
    struct bd_intent candidate={0};struct bd_frontend_identity frontend;
    int present=bd_frontend_get(&s->frontend,&frontend)?0:frontend_current(s);
    if(present<0)return text(out,409,"The native frontend lifecycle is uncertain.");
    if(selected==BD_SEND){
        if(preparation_begin(s,present,&frontend))return text(out,409,"The exact frontend or preparation idle authority could not be retained.");
        candidate.phase=BD_INTENT_DISPATCHING;candidate.action=BD_SEND;candidate.selected=item;candidate.frontend=frontend;memcpy(candidate.nonce,nonce,33);
    }else if(present==1){
        struct bd_intent_request request={r.id,r.generation,(enum bd_source_kind)r.kind,selected};
        struct bd_intent_ops ops=intent_ops(s);
        /* current() refreshes the service catalog. The pure model must borrow
         * this private immutable one-item snapshot, not freed catalog storage. */
        struct bd_catalog one={.items=&item,.count=1,.capacity=1,.generation=r.generation};
        if(bd_intent_begin(&candidate,&one,&request,&frontend,nonce,&ops))return text(out,409,"The native handoff could not be qualified.");
    }else{
        /* Unadmitted browser clients acquire no invented frontend identity.
         * The unchanged full idle check also excludes any live frontend. */
        if(full_idle(s)||selected_current(s,&item)||full_idle(s))return text(out,409,"Close running games and finish transfers before this action.");
        candidate.phase=BD_INTENT_DISPATCHING;candidate.action=selected;candidate.selected=item;memcpy(candidate.nonce,nonce,33);
    }
    if(s->config.ops.reserve(s->config.ops.opaque,nonce,token)){
        struct bd_frontend_preparation *pin=s->preparation_pin;s->preparation_pin=NULL;
        if(bd_frontend_preparation_close(pin))return text(out,503,"The refused preparation witness could not be closed; no work was accepted.");
        return text(out,409,"Another native mutation or operation already owns the console.");}
    s->intent=candidate;s->reserved=1;s->active=1;s->local_session=0;memset(s->backend_id,0,sizeof s->backend_id);memcpy(s->action_token,token,33);
    memset(&s->removal_progress,0,sizeof s->removal_progress);s->removal_observed=0;s->removal_consumed_unknown=0;s->removal_observe_next_ms=0;memcpy(s->confirmed_title,r.confirmed_title,sizeof s->confirmed_title);
    if(present==1&&selected!=BD_SEND){message(s,"waiting-for-dashboard-exit",candidate.message);feedback_begin(s);return accepted_reply(s,out);}
    if(selected!=BD_SEND&&!bd_frontend_get(&s->frontend,&frontend)&&frontend_current(s)!=0){
        s->intent.phase=BD_INTENT_REFUSED;message(s,"refused","The frontend state changed before dispatch.");
        if(!release(s))s->active=0;return text(out,409,s->message);
    }
    s->intent.dispatch_calls=1;int result=dispatch(s,&item,selected,nonce);
    s->intent.phase=result==1?BD_INTENT_ACCEPTED:result==0?BD_INTENT_REFUSED:BD_INTENT_UNKNOWN;
    message(s,result==1?"accepted":result==0?"refused":"unknown",result==1?
        "The selected operation was accepted. Menu and gameplay are separate observations.":result==0?
        "The selected operation was refused before a known acceptance.":"The operation outcome is uncertain. Ownership is retained; it will not be replayed.");
    if(result==0){if(!release(s)){
            struct bd_frontend_preparation *pin=s->preparation_pin;s->preparation_pin=NULL;
            if(bd_frontend_preparation_close(pin)){message(s,"unknown","The refused preparation witness close is uncertain.");s->intent.phase=BD_INTENT_UNKNOWN;}
            else s->active=0;
        }return text(out,409,s->message);}
    if(result<0)return text(out,503,s->message);
    feedback_begin(s);
    return accepted_reply(s,out);
}
static int same_stat(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int cover(struct bd_dashboard_service *s,const char *id,struct bw_native_response *out) {
    if(!*id||strnlen(id,BD_ID_BYTES)>=BD_ID_BYTES||strspn(id,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_-")!=strlen(id))
        return text(out,400,"Invalid exact cover identity.");
    if(refresh(s))return text(out,503,"The cover catalog could not be refreshed.");
    const struct bd_item *found=bd_catalog_find(&s->catalog,id);if(!found)return text(out,404,"Cover unavailable.");struct bd_item item=*found;
    if(item.kind==BD_PC_STREAM){
        char url[3u*BD_SOURCE_BYTES+64u];size_t used=0;
        int n=snprintf(url,sizeof url,"/cover?name=");if(n<0)return -1;used=(size_t)n;
        for(const unsigned char *p=(const unsigned char *)item.source;*p;p++){
            if(used+4u>=sizeof url)return -1;n=snprintf(url+used,sizeof url-used,"%%%02X",(unsigned)*p);if(n!=3)return -1;used+=3u;
        }
        n=snprintf(url+used,sizeof url-used,"&v=%llu",(unsigned long long)item.library_generation);if(n<0||(size_t)n>=sizeof url-used)return -1;
        return backend_read(s,"GET",url,NULL,0,NULL,out);
    }
    char path[BD_SOURCE_BYTES],real[PATH_MAX];struct stat first,held,named;
    int n=snprintf(path,sizeof path,"%s/%s/sce_sys/icon0.png",s->apps_root,item.title_id);
    if(n<0||(size_t)n>=sizeof path||!realpath(path,real)||strcmp(path,real)||lstat(path,&first)||
       !S_ISREG(first.st_mode)||first.st_nlink!=1||first.st_size<33||(uint64_t)first.st_size>BD_LOCAL_COVER_LIMIT)
        return text(out,404,"Cover unavailable.");
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return text(out,404,"Cover unavailable.");
    unsigned char *data=malloc((size_t)first.st_size);size_t used=0;int bad=!data||fstat(fd,&held)||!same_stat(&first,&held);
    while(!bad&&used<(size_t)first.st_size){ssize_t got=pread(fd,data+used,(size_t)first.st_size-used,(off_t)used);if(got<0&&errno==EINTR)continue;if(got<=0){bad=1;break;}used+=(size_t)got;}
    if(!bad)bad=fstat(fd,&held)||lstat(path,&named)||!same_stat(&first,&held)||!same_stat(&held,&named)||!realpath(path,real)||strcmp(real,path);
    if(close(fd))bad=1;
    static const unsigned char png[]={137,80,78,71,13,10,26,10};
    if(!bad)bad=memcmp(data,png,8)||memcmp(data+12,"IHDR",4)||data[8]||data[9]||data[10]||data[11]!=13;
    if(bad){free(data);return text(out,404,"Cover unavailable.");}
    *out=(struct bw_native_response){200,"image/png",data,used};return 0;
}
int bd_service_handle(struct bd_dashboard_service *s,const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out) {
    if(!s||!method||!url||!out||(!body&&size))return -1;memset(out,0,sizeof *out);
    if(strncmp(url,"/api/dashboard/",15))return 1;
    if(pthread_mutex_lock(&s->lock))return -1;
    int rc=0;
    if(!strcmp(method,"GET")&&!strcmp(url,"/api/dashboard/library")){
        (void)tick_locked(s);
        if(refresh(s))rc=text(out,503,"The dashboard catalog could not be refreshed.");
        else {struct bd_catalog_operation operation={.id=s->intent.nonce,.item_id=s->intent.selected.id,.phase=s->phase,.message=s->message,.active=s->active,
                .action=action_name(s->intent.action),.title=s->intent.selected.title};
            if(s->active&&s->intent.action!=BD_SEND&&s->intent.phase==BD_INTENT_WAIT_EXIT&&expected_frontend(s,&s->intent.frontend)==1){operation.exit_requested=1;operation.frontend_pid=s->intent.frontend.pid;}
            if(removal_action(s->intent.action)){
                operation.removal.observed=s->removal_observed;operation.removal.phase=s->removal_progress.phase;
                operation.removal.files=s->removal_progress.files_removed;operation.removal.files_total=s->removal_progress.files_total;
            }
            if(s->intent.action==BD_SEND){
                struct preparation_progress *p=&s->preparation_progress;const struct bd_item *current=bd_catalog_find(&s->catalog,s->intent.selected.id);
                p->ready=!strcmp(p->state,"done")&&p->observed&&p->progress.phase==BW_SOURCE_PREPARED&&
                    p->progress.files_total&&p->progress.files_done==p->progress.files_total&&
                    p->progress.bytes_total&&p->progress.bytes_done==p->progress.bytes_total&&
                    current&&current->kind==BD_PC_STREAM&&(current->flags&BD_READY)&&
                    !strcmp(current->source,s->intent.selected.source)&&!strcmp(current->title_id,s->intent.selected.title_id)&&
                    current->settings_revision==s->intent.selected.settings_revision&&current->library_generation==s->intent.selected.library_generation&&
                    !memcmp(current->source_fingerprint,s->intent.selected.source_fingerprint,32);
                operation.preparation.job_id=s->backend_id;operation.preparation.job_state=p->state;operation.preparation.message=p->message;
                operation.preparation.observed=p->observed;operation.preparation.ready=p->ready;operation.preparation.phase=(unsigned)p->progress.phase;
                operation.preparation.files=p->progress.files_done;operation.preparation.files_total=p->progress.files_total;
                operation.preparation.bytes=p->progress.bytes_done;operation.preparation.bytes_total=p->progress.bytes_total;
            }
            char *json=NULL;size_t n=0;
            if(bd_catalog_json(&s->catalog,s->pc_connected,s->local_connected,&operation,&json,&n))rc=-1;
            else *out=(struct bw_native_response){200,"application/json",json,n};}
    }else if(!strcmp(method,"GET")&&!strncmp(url,"/api/dashboard/cover?id=",24))rc=cover(s,url+24,out);
    else if(!strcmp(method,"POST")&&(!strcmp(url,"/api/dashboard/frontend")||!strcmp(url,"/api/dashboard/action"))){
        if(s->config.native_frontend_disabled&&!strcmp(url,"/api/dashboard/frontend"))rc=text(out,404,"The native Dashboard frontend is not part of Blackwire.");
        else if(authorize(s,token))rc=text(out,403,"Refresh the native authentication token before this action.");
        else rc=!strcmp(url,"/api/dashboard/frontend")?hello(s,body,size,out):action(s,body,size,token,out);
    }else rc=text(out,!strcmp(method,"GET")||!strcmp(method,"POST")?404:405,"Unsupported dashboard route.");
    struct feedback_delivery delivery;feedback_take(s,&delivery);
    pthread_mutex_unlock(&s->lock);feedback_deliver(&delivery);return rc;
}
int bd_service_destroy(struct bd_dashboard_service *s) {
    if(!s||pthread_mutex_lock(&s->lock))return -1;
    if(s->active||s->reserved||s->preparation_pin||s->preparation_borrowed||s->removal_observing){pthread_mutex_unlock(&s->lock);return 1;}
    if(s->preparation_guard_attached){
        if(bw_native_backend_clear_preparation_idle(s->config.backend,preparation_idle,s)){pthread_mutex_unlock(&s->lock);return 1;}
        s->preparation_guard_attached=0;
    }
    if(s->settings_guard_attached&&bw_native_backend_clear_settings_idle(s->config.backend,settings_idle,s)){
        pthread_mutex_unlock(&s->lock);return 1;
    }
    pthread_mutex_unlock(&s->lock);pthread_mutex_destroy(&s->lock);free(s->catalog.items);memset(s,0,sizeof *s);free(s);return 0;
}
