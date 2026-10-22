/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "local_removal_runtime.h"
#include "native_document.h"
#include "native_setup.h"
#include "startup_runtime_idle.h"
#include "copy_sha256.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LR_NONCES 4096u
struct bd_local_removal_runtime {
    pthread_mutex_t lock;pthread_cond_t wake;pthread_t worker;
    struct bd_local_removal_runtime_config config;
    struct bd_local_removal_config component;
    char paths[3][BD_SOURCE_BYTES],folder[BD_LOCAL_REMOVE_ROOTS][BD_SOURCE_BYTES],package[BD_LOCAL_REMOVE_ROOTS][BD_SOURCE_BYTES],log[BD_SOURCE_BYTES];
    struct bd_local_removal_request request;
    struct bd_local_removal *owner;
    struct bw_game_uninstall_result result;
    char consumed[LR_NONCES][33];unsigned consumed_count;
    unsigned queued,active,terminal,stop,settings_bound;
    uint8_t settings_hash[32];
};
static int lr_nonce(const char *n){if(!n||strnlen(n,33)!=32)return 0;unsigned any=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",n[i]))return 0;any|=n[i]!='0';}return any!=0;}
static int lr_path(const char *p){
    if(!p||p[0]!='/'||strnlen(p,BD_SOURCE_BYTES)>=BD_SOURCE_BYTES)return -1;if(!p[1])return 0;
    for(const char *at=p+1;*at;){const char *end=strchr(at,'/');size_t n=end?(size_t)(end-at):strlen(at);
        if(!n||(n==1&&at[0]=='.')||(n==2&&!memcmp(at,"..",2)))return -1;
        for(size_t i=0;i<n;i++)if((unsigned char)at[i]<32||at[i]==127||at[i]=='\\')return -1;
        if(!end)break;at=end+1;if(!*at)return -1;}return 0;
}
static int lr_overlap(const char *a,const char *b){size_t an=strlen(a),bn=strlen(b);return !strcmp(a,"/")||!strcmp(b,"/")||!strcmp(a,b)||(an<bn&&!memcmp(a,b,an)&&b[an]=='/')||(bn<an&&!memcmp(a,b,bn)&&a[bn]=='/');}
/* Resolve the existing prefix even for absent/unmounted default roots. A
 * dangling final symlink or unreadable ancestor is unknown, not exclusion. */
static int lr_resolve(const char *path,char out[BD_SOURCE_BYTES]){
    char prefix[BD_SOURCE_BYTES],real[PATH_MAX];if(lr_path(path))return -1;strcpy(prefix,path);size_t cut=strlen(prefix);
    for(;;){struct stat st;if(!lstat(prefix,&st)){
            if(!realpath(prefix,real))return -1;int n=snprintf(out,BD_SOURCE_BYTES,"%s%s",real,path+cut);return n<0||(size_t)n>=BD_SOURCE_BYTES?-1:0;}
        if(errno!=ENOENT)return -1;char *slash=strrchr(prefix,'/');if(!slash)return -1;if(slash==prefix){prefix[1]=0;cut=1;}else{*slash=0;cut=(size_t)(slash-prefix);}
    }
}
struct lr_roots {const char *destination;unsigned seen,count,declared;};
static int lr_number(const void *raw,size_t n,unsigned maximum,unsigned *out){const unsigned char *p=raw;unsigned v=0;if(!n||(n>1&&p[0]=='0'))return -1;for(size_t i=0;i<n;i++){unsigned d=p[i]-'0';if(d>9||d>maximum||v>(maximum-d)/10)return -1;v=v*10+d;}*out=v;return 0;}
static int lr_root(const char *root,const char *destination){char resolved[BD_SOURCE_BYTES];return lr_path(root)||lr_overlap(root,destination)||lr_resolve(root,resolved)||lr_overlap(resolved,destination)?-1:0;}
static int lr_root_item(void *opaque,const void *raw,size_t n){struct lr_roots *s=opaque;char root[BD_SOURCE_BYTES];return ++s->count>64||bw_document_string(raw,n,root,sizeof root)||lr_root(root,s->destination)?-1:0;}
static int lr_root_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct lr_roots *s=opaque;unsigned bit=0,value=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1;else if(kn==10&&!memcmp(key,"scan_paths",10))bit=2;else if(kn==15&&!memcmp(key,"scan_path_count",15))bit=4;else return 0;
    if(s->seen&bit)return -1;s->seen|=bit;if(bit==2)return bw_document_array(raw,n,lr_root_item,s);
    if(lr_number(raw,n,bit==1?0:64,&value))return -1;if(bit==4)s->declared=value;return 0;
}
int bd_local_removal_scan_excluded(const void *raw,size_t size,const char *destination){
    /* sm_paths.h and add_runtime_managed_scan_paths in the pinned upstream
     * shadowMountPlus1.7-beta5fix1; unioning defaults with custom roots is
     * conservative even when custom roots replace the default list. */
    static const char *roots[]={"/data/homebrew","/data/etaHEN/games","/mnt/ext0","/mnt/ext1","/mnt/usb0","/mnt/usb1","/mnt/usb2","/mnt/usb3","/mnt/usb4","/mnt/usb5","/mnt/usb6","/mnt/usb7","/mnt/shadowmnt"};
    char actual[BD_SOURCE_BYTES];if(!raw||!size||lr_path(destination)||lr_resolve(destination,actual)||strcmp(actual,destination))return -1;
    struct lr_roots s={.destination=destination};if(bw_document_fields(raw,size,lr_root_field,&s)||s.seen!=7||s.count!=s.declared)return -1;
    for(unsigned i=0;i<sizeof roots/sizeof roots[0];i++)if(lr_root(roots[i],destination))return -1;return 0;
}
static int lr_lease(void *opaque){
    struct bd_local_removal_runtime *r=opaque;if(!r)return -1;char nonce[33];pthread_mutex_lock(&r->lock);int bad=r->stop||!r->active||!lr_nonce(r->request.operation);memcpy(nonce,r->request.operation,33);pthread_mutex_unlock(&r->lock);
    if(bad)return -1;return r->config.reservation_current?r->config.reservation_current(r->config.opaque,nonce):bw_native_backend_external_current(r->config.backend,nonce);
}
static int lr_current(void *opaque){
    struct bd_local_removal_runtime *r=opaque;if(lr_lease(r))return -1;
    if(r->config.idle)return r->config.idle(r->config.opaque)||lr_lease(r)?-1:0;
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    struct bw_startup_idle_request idle={.deadline=(double)t.tv_sec+(double)t.tv_nsec/1e9+2.0,.current=lr_lease,.opaque=r,.game_log=r->log[0]?r->log:NULL};
    return bw_native_setup_idle(NULL)||bw_startup_idle_ps5(&idle)||lr_lease(r)?-1:0;
}
static int lr_outside(void *opaque,const char *destination){
    struct bd_local_removal_runtime *r=opaque;if(lr_current(r))return -1;
    struct bw_sm_document d={0};struct bw_sm_connection c={r->component.smp_port,lr_lease,r};
    int bad=r->config.settings?r->config.settings(r->config.opaque,&d):bw_sm_observe(&c,BW_SM_OBSERVE_SETTINGS,&d);
    uint8_t hash[32];if(!bad)bad=bd_local_removal_scan_excluded(d.bytes,d.size,destination);
    if(!bad){cp_sha_bytes(d.bytes,d.size,hash);if(r->settings_bound)bad=memcmp(hash,r->settings_hash,32);else{memcpy(r->settings_hash,hash,32);r->settings_bound=1;}}
    bw_sm_document_clear(&d);return bad||lr_current(r)?-1:0;
}
static int lr_collect(void *opaque,struct bd_smp_record *rows,size_t cap,size_t *count){struct bd_local_removal_runtime *r=opaque;return r->config.removal.ops.collect(r->config.removal.ops.opaque,rows,cap,count);}
static int lr_uninstall(void *opaque,const char title[10]){struct bd_local_removal_runtime *r=opaque;return r->config.removal.ops.uninstall(r->config.removal.ops.opaque,title);}
static int lr_manual_present(void *opaque,const char *path){struct bd_local_removal_runtime *r=opaque;return r->config.removal.ops.manual_present(r->config.removal.ops.opaque,path);}
static int lr_manual_remove(void *opaque,const char *path){struct bd_local_removal_runtime *r=opaque;return r->config.removal.ops.manual_remove(r->config.removal.ops.opaque,path);}
static void lr_progress(void *opaque,const struct bw_game_uninstall_progress *p){struct bd_local_removal_runtime *r=opaque;pthread_mutex_lock(&r->lock);
    /* Component completion is not published until retained descriptors close. */
    r->result.phase=p->phase==BW_GAME_REMOVE_DONE?BW_GAME_REMOVE_COMMITTING:p->phase;r->result.files_removed=p->files_done;
    r->result.reason=p->phase==BW_GAME_REMOVE_DONE?"Checking the final uninstall result.":p->message;pthread_mutex_unlock(&r->lock);}
static void lr_publish(struct bd_local_removal_runtime *r,const struct bd_local_removal_result *p,unsigned terminal){
    pthread_mutex_lock(&r->lock);r->result=(struct bw_game_uninstall_result){.phase=p->phase,.mutation_started=p->mutation_started,
        .registration_confirmed=p->registration_confirmed,.source_absent=p->source_absent,.prepared_record_absent=1,
        .library_removed=p->library_removed,.files_removed=p->files_removed,.files_total=p->files_total,.reason=p->reason};
    r->terminal=terminal;if(terminal)r->active=0;pthread_mutex_unlock(&r->lock);
}
static int lr_admit(struct bd_local_removal_runtime *r){
    if(lr_current(r))return -1;
    if(!r->config.removal.ops.uninstall){struct bw_sm_reply version={0};struct bw_sm_connection c={r->component.smp_port,lr_lease,r};
        if(bw_sm_post(&c,BW_SM_VERSION,"/",&version)||!version.uninstall_supported||
           (r->request.selected.kind==BD_LOCAL_FOLDER&&(!version.remove_supported||!version.settings_supported)))return -1;}
    if(r->request.selected.kind==BD_LOCAL_FOLDER){char destination[BD_SOURCE_BYTES];int n=snprintf(destination,sizeof destination,"%s/local-removals/%s/source",r->component.data_root,r->request.operation);
        if(n<0||(size_t)n>=sizeof destination||lr_outside(r,destination))return -1;}
    return lr_current(r);
}
static void *lr_worker(void *opaque){
    struct bd_local_removal_runtime *r=opaque;pthread_mutex_lock(&r->lock);
    for(;;){while(!r->queued&&!r->stop)pthread_cond_wait(&r->wake,&r->lock);if(r->stop)break;r->queued=0;pthread_mutex_unlock(&r->lock);
        struct bd_local_removal_result result={0};int rc;
        if(lr_admit(r)){result=(struct bd_local_removal_result){.phase=BW_GAME_REMOVE_ERROR,.reason="Close running apps and refresh the game and scan settings before uninstalling."};lr_publish(r,&result,1);}
        else {
            rc=bd_local_removal_begin(&r->component,&r->request,&r->owner,&result);
            if(rc!=0&&!r->owner){result.phase=BW_GAME_REMOVE_ERROR;if(!result.reason)result.reason="The selected game could not be checked. Refresh before trying again.";}
            lr_publish(r,&result,rc!=0&&!r->owner);
            while(r->owner){
                rc=bd_local_removal_poll(r->owner,&result);
                if(rc==1){int closed=bd_local_removal_destroy(r->owner);
                    /*1 retains a live owner. Negative close-error frees the
                     * component; never keep a dangling pointer or replay it. */
                    if(closed!=1)r->owner=NULL;
                    if(closed){result.phase=BW_GAME_REMOVE_UNKNOWN;result.reason="Uninstall finished, but its final result needs checking.";lr_publish(r,&result,0);}else lr_publish(r,&result,1);break;}
                lr_publish(r,&result,0);struct timespec delay={0,250000000};while(nanosleep(&delay,&delay)&&errno==EINTR){}
            }
        }
        pthread_mutex_lock(&r->lock);
    }pthread_mutex_unlock(&r->lock);return NULL;
}
struct bd_local_removal_runtime *bd_local_removal_runtime_create(const struct bd_local_removal_runtime_config *config){
    if(!config||(!config->backend&&!config->reservation_current)||config->removal.folder_root_count>BD_LOCAL_REMOVE_ROOTS||config->removal.package_root_count>BD_LOCAL_REMOVE_ROOTS)return NULL;
    struct bd_local_removal_runtime *r=calloc(1,sizeof *r);if(!r)return NULL;r->config=*config;r->component=config->removal;
    const char *paths[]={config->removal.data_root?config->removal.data_root:"/data/blackwire",config->removal.apps_root?config->removal.apps_root:"/user/app",config->removal.database?config->removal.database:"/system_data/priv/mms/app.db"};
    for(unsigned i=0;i<3;i++){if(lr_path(paths[i])||!paths[i][1])goto fail;strcpy(r->paths[i],paths[i]);}
    r->component.data_root=r->paths[0];r->component.apps_root=r->paths[1];r->component.database=r->paths[2];
    for(size_t i=0;i<config->removal.folder_root_count;i++){if(lr_path(config->removal.folder_roots[i]))goto fail;strcpy(r->folder[i],config->removal.folder_roots[i]);r->component.folder_roots[i]=r->folder[i];}
    for(size_t i=0;i<config->removal.package_root_count;i++){if(lr_path(config->removal.package_roots[i]))goto fail;strcpy(r->package[i],config->removal.package_roots[i]);r->component.package_roots[i]=r->package[i];}
    if(config->game_log){if(lr_path(config->game_log))goto fail;strcpy(r->log,config->game_log);}
    r->component.ops=(struct bd_local_removal_ops){.opaque=r,.current=lr_current,.quarantine_allowed=lr_outside,
        .collect=config->removal.ops.collect?lr_collect:NULL,.uninstall=config->removal.ops.uninstall?lr_uninstall:NULL,
        .manual_present=config->removal.ops.manual_present?lr_manual_present:NULL,.manual_remove=config->removal.ops.manual_remove?lr_manual_remove:NULL,.observe=lr_progress};
    if(pthread_mutex_init(&r->lock,NULL))goto fail;if(pthread_cond_init(&r->wake,NULL)){pthread_mutex_destroy(&r->lock);goto fail;}
    pthread_attr_t attr;if(pthread_attr_init(&attr)){pthread_cond_destroy(&r->wake);pthread_mutex_destroy(&r->lock);goto fail;}
    int rc=pthread_attr_setstacksize(&attr,4u*1024u*1024u);if(!rc)rc=pthread_create(&r->worker,&attr,lr_worker,r);pthread_attr_destroy(&attr);
    if(rc){pthread_cond_destroy(&r->wake);pthread_mutex_destroy(&r->lock);goto fail;}return r;
fail:free(r);return NULL;
}
int bd_local_removal_runtime_capable(void *opaque,const struct bd_item *item){struct bd_local_removal_runtime *r=opaque;return r&&bd_local_removal_capable(&r->component,item);}
int bd_local_removal_runtime_start(void *opaque,const struct bd_item *item,const char nonce[33],const char title[10]){
    struct bd_local_removal_runtime *r=opaque;if(!r||!lr_nonce(nonce)||!item||!title||strnlen(title,10)!=9||!bd_local_removal_runtime_capable(r,item)||strcmp(title,item->title_id))return 0;
    pthread_mutex_lock(&r->lock);int result=0;
    if(r->consumed_count&& !strcmp(nonce,r->request.operation))result=!strcmp(item->id,r->request.selected.id)&&item->kind==r->request.selected.kind&&item->catalog_generation==r->request.selected.catalog_generation&&
        !memcmp(item->source_fingerprint,r->request.selected.source_fingerprint,32)?1:0;
    else if(!r->stop&&!r->active&&r->consumed_count<LR_NONCES){
        unsigned seen=0;for(unsigned i=0;i<r->consumed_count;i++)if(!strcmp(nonce,r->consumed[i]))seen=1;
        if(!seen){memset(&r->request,0,sizeof r->request);r->request.selected=*item;memcpy(r->request.operation,nonce,33);
            strcpy(r->request.confirmation.id,item->id);memcpy(r->request.confirmation.title_id,title,10);r->request.confirmation.kind=item->kind;r->request.confirmation.catalog_generation=item->catalog_generation;r->request.confirmation.confirmed=1;
            memcpy(r->consumed[r->consumed_count++],nonce,33);r->settings_bound=0;r->terminal=0;r->active=r->queued=1;
            r->result=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_CHECKING,.reason="Checking the selected game before uninstalling."};pthread_cond_signal(&r->wake);result=1;
        }
    }pthread_mutex_unlock(&r->lock);return result;
}
int bd_local_removal_runtime_state(void *opaque,const char nonce[33],struct bw_game_uninstall_result *out){
    struct bd_local_removal_runtime *r=opaque;if(!r||!out||!lr_nonce(nonce))return -1;pthread_mutex_lock(&r->lock);int result=-1;
    if(!strcmp(nonce,r->request.operation)){*out=r->result;result=r->terminal?1:r->result.phase==BW_GAME_REMOVE_UNKNOWN?-1:0;}pthread_mutex_unlock(&r->lock);return result;
}
int bd_local_removal_runtime_destroy(struct bd_local_removal_runtime *r){
    if(!r)return -1;pthread_mutex_lock(&r->lock);if(r->active||r->queued||r->owner){pthread_mutex_unlock(&r->lock);return 1;}r->stop=1;pthread_cond_signal(&r->wake);pthread_mutex_unlock(&r->lock);
    if(pthread_join(r->worker,NULL))return -1;int bad=pthread_cond_destroy(&r->wake)||pthread_mutex_destroy(&r->lock);free(r);return bad?-1:0;
}
