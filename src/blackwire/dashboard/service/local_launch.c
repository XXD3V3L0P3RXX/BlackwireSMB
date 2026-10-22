/* SPDX-License-Identifier: GPL-3.0-or-later
 * Ordinary local Sony launch, reusing the frozen controller's bounded log
 * discovery and the unchanged process birth/role/title/app identity module.
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "local_launch.h"
#include "catalog_adapter.h"
#include "source_param.h"
#include "lifecycle.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BD_LOG_LIMIT 1048576u
#define BD_LOG_FILE_LIMIT 33554432u
#define BD_DISCOVER_MS UINT64_C(30000)
#define BD_NONCE_LIMIT 4096u
_Static_assert(sizeof(struct bd_local_launch_context)==32&&offsetof(struct bd_local_launch_context,crash)==16&&offsetof(struct bd_local_launch_context,check)==24,"Retained Sony launch ABI");
int sceSystemServiceLaunchApp(const char *,char **,struct bd_local_launch_context *);

struct bd_local_launcher {
    char apps[BD_SOURCE_BYTES],database[BD_SOURCE_BYTES],log_path[BD_SOURCE_BYTES];
    struct bd_local_platform ops;
    struct bd_item selected;
    struct bd_local_launch_status state;
    int log_fd,consumed_result,log_poisoned;
    struct stat log_id;
    unsigned char anchor[512],*log_bytes;
    size_t anchor_size;
    uint64_t began_ms,last_ms;
    char consumed[BD_NONCE_LIMIT][33];
    unsigned consumed_count;
};
static int foreground(void *o,uint32_t *u,struct bw_reader_user_observation *s){(void)o;return bw_reader_foreground_user(u,s);}
static int launch(void *o,const char *t,char **a,struct bd_local_launch_context *c){(void)o;return sceSystemServiceLaunchApp(t,a,c);}
static int admit(void *o,const struct rc_game_selection *s,struct rc_game_identity *i){(void)o;return rc_game_identity_admit(s,i);}
static int current(void *o,const struct rc_game_identity *s,struct rc_game_identity *i){(void)o;return rc_game_identity_current(s,i);}
static int now(void *o,uint64_t *out){
    (void)o;struct timespec t;
    if(!out||clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L||
        (uint64_t)t.tv_sec>(UINT64_MAX-999u)/1000u)return -1;
    *out=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;return *out?0:-1;
}
static void reason(struct bd_local_launcher *s,const char *text){snprintf(s->state.reason,sizeof s->state.reason,"%s",text);}
static int terminated(const char *s,size_t n){return s&&memchr(s,0,n)!=NULL;}
static int path(const char *p){
    if(!p||p[0]!='/'||strnlen(p,BD_SOURCE_BYTES)>=BD_SOURCE_BYTES||!p[1]||p[strlen(p)-1]=='/'||strchr(p,'\\'))return 0;
    size_t start=1;
    for(size_t i=1;;i++)if(!p[i]||p[i]=='/'){
        size_t n=i-start;if(!n||(n==1&&p[start]=='.')||(n==2&&!memcmp(p+start,"..",2)))return 0;
        if(!p[i])return 1;start=i+1;
    }
}
static int nonce(const char *p){
    if(!p||strnlen(p,33)!=32)return 0;unsigned any=0;
    for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",p[i]))return 0;any|=p[i]!='0';}return any!=0;
}
int bd_local_launch_title_supported(const char *t){
    if(!t||strnlen(t,10)!=9||memcmp(t,"PPSA",4))return 0;
    for(unsigned i=4;i<9;i++)if(t[i]<'0'||t[i]>'9')return 0;return 1;
}
int bd_local_launch_item_supported(const struct bd_item *i){
    return i&&terminated(i->id,sizeof i->id)&&i->id[0]&&terminated(i->title,sizeof i->title)&&i->title[0]&&
        bd_local_launch_title_supported(i->title_id)&&terminated(i->source,sizeof i->source)&&path(i->source)&&
        terminated(i->registered_source,sizeof i->registered_source)&&!strcmp(i->source,i->registered_source)&&
        (i->kind==BD_LOCAL_FOLDER||i->kind==BD_INSTALLED_PACKAGE);
}
static int item_valid(const struct bd_item *i){return bd_local_launch_item_supported(i)&&(i->flags&(BD_READY|BD_CAN_PLAY))==(BD_READY|BD_CAN_PLAY);}
static int same_stat(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&
        a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_nlink==b->st_nlink&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int read_at(int fd,uint64_t offset,void *data,size_t size){
    if(offset>INT64_MAX||size>(uint64_t)INT64_MAX-offset)return -1;
    for(size_t done=0;done<size;){ssize_t n=pread(fd,(unsigned char *)data+done,size-done,(off_t)(offset+done));
        if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-done)return -1;done+=(size_t)n;}return 0;
}
static int join(char out[BD_SOURCE_BYTES],const char *base,const char *leaf){int n=snprintf(out,BD_SOURCE_BYTES,"%s/%s",base,leaf);return n<0||(size_t)n>=BD_SOURCE_BYTES?-1:0;}
static int parameter(const char *name,char title[10],char content[128]){
    char real[PATH_MAX];struct stat first,held,named;
    if(!realpath(name,real)||strcmp(name,real)||lstat(name,&first)||!S_ISREG(first.st_mode)||first.st_nlink!=1||
        first.st_size<=0||(uint64_t)first.st_size>BW_SOURCE_PARAM_LIMIT)return -1;
    int fd=open(name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    unsigned char *bytes=malloc((size_t)first.st_size);int bad=!bytes||fstat(fd,&held)||!same_stat(&first,&held);
    if(!bad)bad=read_at(fd,0,bytes,(size_t)first.st_size)||fstat(fd,&held)||lstat(name,&named)||
        !same_stat(&first,&held)||!same_stat(&held,&named)||!realpath(name,real)||strcmp(name,real)||
        bw_source_param_identity(bytes,(size_t)first.st_size,title,content);
    free(bytes);if(close(fd))bad=1;return bad?-1:0;
}
int bd_local_launch_capable(struct bd_local_launcher *s,const struct bd_item *i){
    if(!s||!item_valid(i))return 0;
    struct bd_smp_record row={0};char app[BD_SOURCE_BYTES],name[BD_SOURCE_BYTES],title[10];
    const char *metadata_root=i->source;
    if(i->kind==BD_INSTALLED_PACKAGE){if(join(app,s->apps,i->title_id))return 0;metadata_root=app;}
    if(join(name,metadata_root,"sce_sys/param.json")||parameter(name,title,row.content_id)||strcmp(title,i->title_id))return 0;
    memcpy(row.title,i->title,sizeof row.title);memcpy(row.title_id,i->title_id,sizeof row.title_id);
    memcpy(row.source,i->source,sizeof row.source);memcpy(row.runtime_source,i->source,sizeof row.runtime_source);
    row.installed=row.source_available=1;row.installed_pkg=i->kind==BD_INSTALLED_PACKAGE;row.managed=!row.installed_pkg;
    snprintf(row.source_type,sizeof row.source_type,"%s",row.installed_pkg?"pkg":"folder");
    struct bd_local_environment env={s->apps,s->database,1,NULL};struct bd_local_observation observation;
    if(bd_local_observe(&row,&env,&observation)||!observation.metadata_verified||observation.item.facts!=BD_LOCAL_REQUIRED||observation.item.exclusions)return 0;
    struct bd_item fresh;struct bd_catalog catalog;
    if(bd_catalog_init(&catalog,&fresh,1,i->catalog_generation)||bd_catalog_add_local(&catalog,&observation.item)||catalog.count!=1)return 0;
    return (fresh.flags&BD_CAN_PLAY)&&fresh.kind==i->kind&&!strcmp(fresh.id,i->id)&&!strcmp(fresh.source,i->source)&&
        !strcmp(fresh.registered_source,i->registered_source)&&!memcmp(fresh.source_fingerprint,i->source_fingerprint,32);
}
static int log_regular(const struct stat *p){return S_ISREG(p->st_mode)&&p->st_nlink==1&&p->st_size>=0;}
static int log_current(struct bd_local_launcher *s,struct stat *out){
    struct stat named;unsigned char anchor[512];char real[PATH_MAX];
    return s->log_fd<0||fstat(s->log_fd,out)||!log_regular(out)||out->st_dev!=s->log_id.st_dev||out->st_ino!=s->log_id.st_ino||
        out->st_uid!=s->log_id.st_uid||out->st_gid!=s->log_id.st_gid||out->st_mode!=s->log_id.st_mode||
        (uint64_t)out->st_size<s->state.log_base||(uint64_t)out->st_size-s->state.log_base>BD_LOG_LIMIT||
        lstat(s->log_path,&named)||!log_regular(&named)||named.st_dev!=out->st_dev||named.st_ino!=out->st_ino||
        named.st_uid!=out->st_uid||named.st_gid!=out->st_gid||named.st_mode!=out->st_mode||named.st_size<out->st_size||
        !realpath(s->log_path,real)||strcmp(s->log_path,real)||
        read_at(s->log_fd,s->state.log_base-s->anchor_size,anchor,s->anchor_size)||memcmp(anchor,s->anchor,s->anchor_size)?-1:0;
}
static void close_log(struct bd_local_launcher *s){
    if(s->log_fd>=0){int fd=s->log_fd;s->log_fd=-1;(void)close(fd);}free(s->log_bytes);s->log_bytes=NULL;
}
static int baseline(struct bd_local_launcher *s){
    char real[PATH_MAX];
    if(!realpath(s->log_path,real)||strcmp(s->log_path,real))return -1;
    s->log_fd=open(s->log_path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(s->log_fd<0||fstat(s->log_fd,&s->log_id)||!log_regular(&s->log_id)||s->log_id.st_size<=0||
        (uint64_t)s->log_id.st_size>BD_LOG_FILE_LIMIT)return -1;
    s->state.log_base=(uint64_t)s->log_id.st_size;s->anchor_size=s->state.log_base<512?(size_t)s->state.log_base:512;
    if(read_at(s->log_fd,s->state.log_base-s->anchor_size,s->anchor,s->anchor_size)||s->anchor[s->anchor_size-1]!='\n')return -1;
    s->log_bytes=malloc(BD_LOG_LIMIT);struct stat fresh;return !s->log_bytes||log_current(s,&fresh)?-1:0;
}
static int same_identity(const struct rc_game_identity *a,const struct rc_game_identity *b){
    return a->firmware==b->firmware&&a->app_id==b->app_id&&a->admitted_wall_us==b->admitted_wall_us&&
        !memcmp(a->title,b->title,sizeof a->title)&&bw_stop_identity_match(&a->process,&b->process)==1;
}
static int discover(struct bd_local_launcher *s,struct rc_game_selection *out){
    struct stat observed;uint64_t time=0;
    if(s->log_poisoned)return -1;
    if(s->ops.now_ms(s->ops.opaque,&time)||!time||time<s->last_ms||time-s->began_ms>BD_DISCOVER_MS){reason(s,"pending_identity_clock_or_deadline_unknown");s->log_poisoned=1;return -1;}
    s->last_ms=time;
    if(log_current(s,&observed)){reason(s,"anchored_log_identity_or_bounds_unknown");s->log_poisoned=1;return -1;}
    size_t size=(size_t)((uint64_t)observed.st_size-s->state.log_base);s->state.log_used=size;
    if(!size)return 0;
    if(read_at(s->log_fd,s->state.log_base,s->log_bytes,size)||log_current(s,&observed)){reason(s,"anchored_log_read_unknown");s->log_poisoned=1;return -1;}
    if(s->log_bytes[size-1]!='\n')return 0; /*A still-appending tail never proves EOF.*/
    struct bw_lifecycle parser;
    if(bw_lifecycle_init(&parser,s->selected.title_id,0))return -1;
    size_t at=0;int32_t first=0;
    while(at<size){
        const unsigned char *lf=memchr(s->log_bytes+at,'\n',size-at);if(!lf)goto bad;
        size_t length=(size_t)(lf-(s->log_bytes+at))+1;
        if(bw_lifecycle_feed_line(&parser,s->log_bytes+at,length)<0)goto bad;
        if(parser.latest.seen){if(first&&first!=parser.latest.pid)goto bad;first=parser.latest.pid;}at+=length;
    }
    int32_t pid=bw_lifecycle_finish(&parser,1);
    if(pid>1&&parser.latest.app_id>0&&parser.latest.app_id<=INT32_MAX){
        *out=(struct rc_game_selection){pid,parser.latest.app_id,{0}};memcpy(out->title,s->selected.title_id,10);return 1;
    }
    if(pid==BW_LIFECYCLE_STOPPED)goto bad;
    return 0;
bad:reason(s,"appended_lifecycle_ambiguous_or_stopped_before_admission");s->log_poisoned=1;return -1;
}
struct bd_local_launcher *bd_local_launcher_create(const struct bd_local_launch_config *config){
    struct bd_local_launch_config c={0};if(config)c=*config;
    const char *apps=c.apps_root?c.apps_root:"/user/app",*db=c.database?c.database:"/system_data/priv/mms/app.db",*log=c.game_log?c.game_log:"/data/shadowmount/debug.log";
    if(!path(apps)||!path(db)||!path(log))return NULL;
    struct bd_local_launcher *s=calloc(1,sizeof *s);if(!s)return NULL;
    snprintf(s->apps,sizeof s->apps,"%s",apps);snprintf(s->database,sizeof s->database,"%s",db);snprintf(s->log_path,sizeof s->log_path,"%s",log);
    s->ops=c.platform;s->log_fd=-1;
    if(!s->ops.foreground_user)s->ops.foreground_user=foreground;if(!s->ops.launch_app)s->ops.launch_app=launch;
    if(!s->ops.admit)s->ops.admit=admit;if(!s->ops.current)s->ops.current=current;if(!s->ops.now_ms)s->ops.now_ms=now;
    reason(s,"idle_no_local_launch");return s;
}
int bd_local_launcher_destroy(struct bd_local_launcher *s){if(!s)return -1;if(s->state.active)return 1;close_log(s);memset(s,0,sizeof *s);free(s);return 0;}
int bd_local_launcher_snapshot(const struct bd_local_launcher *s,struct bd_local_launch_status *out){if(!s||!out)return -1;*out=s->state;return 0;}
int bd_local_launch(void *opaque,const struct bd_item *item,const char run[33]){
    struct bd_local_launcher *s=opaque;
    if(!s||!nonce(run))return 0;
    if(!strcmp(s->state.nonce,run)){
        if(!item_valid(item)||item->kind!=s->selected.kind||strcmp(item->id,s->selected.id)||
            strcmp(item->source,s->selected.source)||strcmp(item->registered_source,s->selected.registered_source)||
            strcmp(item->title_id,s->selected.title_id)||item->catalog_generation!=s->selected.catalog_generation||
            memcmp(item->source_fingerprint,s->selected.source_fingerprint,32))return -1;
        return s->consumed_result; /*Exact consumed selection, never replay.*/
    }
    if(s->state.active)return -1;
    for(unsigned i=0;i<s->consumed_count;i++)if(!strcmp(s->consumed[i],run))return 0;
    if(s->consumed_count>=BD_NONCE_LIMIT)return 0;
    memcpy(s->consumed[s->consumed_count++],run,33);
    close_log(s);memset(&s->state,0,sizeof s->state);memset(&s->selected,0,sizeof s->selected);
    s->log_poisoned=0;s->consumed_result=0;memcpy(s->state.nonce,run,33);reason(s,"source_or_capability_refused");
    if(!item_valid(item))return 0;s->selected=*item;memcpy(s->state.title_id,item->title_id,10);
    if(!bd_local_launch_capable(s,&s->selected))return 0;
    reason(s,"log_baseline_refused");if(baseline(s)){close_log(s);return 0;}
    struct bd_local_launch_context ctx={.size=sizeof ctx};char *arguments[]={NULL};
    reason(s,"foreground_user_refused");
    if(s->ops.foreground_user(s->ops.opaque,&ctx.user,&s->state.user)||ctx.user==UINT32_MAX){close_log(s);return 0;}
    struct stat log;reason(s,"last_prelaunch_source_log_or_clock_refused");
    if(!bd_local_launch_capable(s,&s->selected)||log_current(s,&log)||s->ops.now_ms(s->ops.opaque,&s->began_ms)||!s->began_ms){close_log(s);return 0;}
    s->last_ms=s->began_ms;s->state.attempted=1;
    s->state.launch_result=s->ops.launch_app(s->ops.opaque,s->selected.title_id,arguments,&ctx);
    if(s->state.launch_result<0){reason(s,"sony_launch_api_refused_no_menu_claim");close_log(s);return 0;}
    s->state.accepted=s->state.active=1;s->consumed_result=1;
    reason(s,"sony_launch_accepted_fresh_identity_pending_no_menu_claim");
    if(!bd_local_launch_capable(s,&s->selected)){reason(s,"postlaunch_source_changed_lifecycle_retained_unknown");s->consumed_result=-1;return -1;}
    return 1;
}
int bd_local_state(void *opaque,const char run[33]){
    struct bd_local_launcher *s=opaque;
    if(!s||!nonce(run)||strcmp(run,s->state.nonce))return -1;
    if(s->state.exact_exit)return 0;
    if(!s->state.active||!s->state.accepted)return -1;
    if(!s->state.identity_admitted){
        struct rc_game_selection selected={0};int found=discover(s,&selected);
        if(found<=0)return -1;
        struct rc_game_identity identity={0};
        if(s->ops.admit(s->ops.opaque,&selected,&identity)!=RC_GAME_QUALIFIED||identity.process.pid!=selected.pid||
            identity.app_id!=selected.app_id||memcmp(identity.title,selected.title,10)||
            identity.firmware!=BW_STOP_PROFILE_FW||bw_stop_identity_admit(&identity.process,identity.admitted_wall_us)){
            reason(s,"fresh_pid_birth_admission_unknown_no_fallback");s->log_poisoned=1;return -1;
        }
        s->state.identity=identity;s->state.identity_admitted=1;close_log(s);
    }
    struct rc_game_identity fresh={0};
    int observed=s->ops.current(s->ops.opaque,&s->state.identity,&fresh);
    if(observed==RC_GAME_ABSENT){s->state.exact_exit=1;s->state.active=0;reason(s,"exact_admitted_generation_absence_verified");return 0;}
    if(observed!=RC_GAME_QUALIFIED||!same_identity(&s->state.identity,&fresh)){
        reason(s,"exact_pid_birth_role_title_or_app_unknown");return -1;
    }
    if(!bd_local_launch_capable(s,&s->selected)){reason(s,"exact_process_present_source_binding_changed_unknown");return -1;}
    reason(s,"exact_current_process_qualified_no_menu_claim");return 1;
}
