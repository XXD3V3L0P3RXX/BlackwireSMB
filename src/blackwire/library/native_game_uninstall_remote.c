/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit confirmed raw-FOLDER Uninstall; normal fixed-route SMP only.
 * The core persists its consumed registration unit BEFORE this callback. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_game_uninstall_remote.h"
#include "native_registration_maintenance.h"
#include "native_registration_lifecycle.h"
#include "native_registration_removed.h"
#include "native_cleanup_remote.h"
#include "native_registration.h"
#include "registration_param_compat.h"
#include "source_param.h"
#include "send_diagnostic.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <time.h>
#include <dirent.h>

struct gur_blob {struct stat id;unsigned char hash[32];size_t size;unsigned char *body;};
struct gur {
    const struct bw_game_uninstall_request *accepted;
    const struct bw_game_uninstall_files_request *files_accepted;
    struct bw_game_uninstall_request request;
    struct bw_game_uninstall_ops outer;
    struct bw_nrc_remote remote;
    struct bw_nrc_subject subject;
    struct bw_nrc_subject owner;
    struct bw_reader_selection selection;
    struct bw_nrl_retained_source source;
    char paths[3][BW_NRC_PATH_BYTES],root[BW_NRC_PATH_BYTES];
    int apps,app;
    struct stat apps_id,app_id;
    struct gur_blob source_param,app_param,mount,receipt;
    unsigned binding_started,bound,app_known,registration_called,manual_called,uninstall_called,owner_known,runtime_release_done;
    unsigned generation_observe_allowed,generation_release_unknown,release_unknown;
    const char *reason,*release_reason,*callback_stage;
    int manual_result,uninstall_result;
    struct bw_sm_uninstall_reply uninstall_reply;
};
static int gur_release(struct gur *p,int fd){int rc=close(fd);if(rc){p->release_unknown=1;if(!p->release_reason)p->release_reason="normal_adapter_descriptor_close_unconfirmed";}return rc;}
/* Remote collection resets its observation flag per call: capture it before
 * another independent observation can begin. Never infer it from a message. */
static int gur_remote_result(struct gur *p,int status){if(p->remote.observation_release_unknown){p->release_unknown=1;if(!p->release_reason)p->release_reason="normal_remote_observation_release_unconfirmed";}return status;}
static double gur_now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int gur_current(void *opaque){
    struct gur *p=opaque;const struct bw_game_uninstall_request *a=p->accepted,*r=&p->request;
    const struct bw_game_uninstall_files_request *f=p->files_accepted;
    if(p->release_unknown)return -1;
    if(f&&(memcmp(&f->identity,&r->identity,sizeof r->identity)||memcmp(&f->prepared,&r->prepared,sizeof r->prepared)||
       strcmp(f->operation,r->operation)||strcmp(f->confirmed_title,r->confirmed_title)||
       strcmp(f->environment.data_root,p->paths[0])||strcmp(f->environment.apps_root,p->paths[1])||strcmp(f->environment.database,p->paths[2])||
       f->reader_size!=r->reader_size||memcmp(f->reader_sha256,r->reader_sha256,32)))return -1;
    return !p->outer.current||p->outer.current(p->outer.opaque)||memcmp(&a->identity,&r->identity,sizeof r->identity)||
        memcmp(a->prepared.record_sha256,r->prepared.record_sha256,32)||strcmp(a->prepared.run,r->prepared.run)||
        strcmp(a->operation,r->operation)||strcmp(a->confirmed_title,r->confirmed_title)||
        strcmp(a->environment.data_root,p->paths[0])||strcmp(a->environment.apps_root,p->paths[1])||strcmp(a->environment.database,p->paths[2])?-1:0;
}
static int gur_node(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;}
static int gur_same(const struct stat *a,const struct stat *b){return gur_node(a,b)&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&
    a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int gur_dir(struct gur *p,const char *path,struct stat *id){
    char real[PATH_MAX];struct stat named;if(!realpath(path,real)||strcmp(path,real)||lstat(path,&named)||!S_ISDIR(named.st_mode))return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,id)||!gur_node(id,&named)){gur_release(p,fd);return -1;}return fd;
}
static int gur_dir_current(int fd,const char *path,const struct stat *id){
    struct stat held,named;char real[PATH_MAX];return fd<0||fstat(fd,&held)||lstat(path,&named)||!S_ISDIR(held.st_mode)||!gur_node(id,&held)||!gur_node(&held,&named)||!realpath(path,real)||strcmp(path,real)?-1:0;
}
static int gur_read(struct gur *p,const char *path,size_t maximum,struct gur_blob *out){
    memset(out,0,sizeof *out);char real[PATH_MAX];int fd=-1,rc=-1;struct stat held,named;
    if(gur_current(p)||!realpath(path,real)||strcmp(path,real))return -1;
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,&out->id)||!S_ISREG(out->id.st_mode)||out->id.st_nlink!=1||out->id.st_size<=0||(uint64_t)out->id.st_size>maximum)goto done;
    out->size=(size_t)out->id.st_size;out->body=malloc(out->size);if(!out->body)goto done;
    for(size_t at=0;at<out->size;){if(gur_current(p))goto done;ssize_t n=pread(fd,out->body+at,out->size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>out->size-at)goto done;at+=(size_t)n;}
    cp_sha_bytes(out->body,out->size,out->hash);
    if(fstat(fd,&held)||lstat(path,&named)||!gur_same(&out->id,&held)||!gur_same(&held,&named)||!realpath(path,real)||strcmp(path,real)||gur_current(p))goto done;rc=0;
done:if(gur_release(p,fd))rc=-1;if(rc){free(out->body);memset(out,0,sizeof *out);}return rc;
}
static int gur_join(char out[BW_NRC_PATH_BYTES],const char *a,const char *b){int n=snprintf(out,BW_NRC_PATH_BYTES,"%s/%s",a,b);return n<0||(size_t)n>=BW_NRC_PATH_BYTES?-1:0;}
static int gur_blob_current(struct gur *p,const char *path,const struct gur_blob *expected){
    struct gur_blob now={0};int rc=gur_read(p,path,expected->size,&now)||now.size!=expected->size||!gur_same(&expected->id,&now.id)||memcmp(expected->hash,now.hash,32);free(now.body);return rc?-1:0;
}
static int gur_subject(struct gur *p,const struct bw_removed_source *s){return !s||memcmp(&s->identity,&p->request.identity,sizeof s->identity)||memcmp(s->prepared_sha256,p->request.prepared.record_sha256,32)||strcmp(s->operation,p->request.operation)||gur_current(p)?-1:0;}
static int gur_absent(struct gur *p){
    struct stat st;return gur_current(p)||gur_dir_current(p->apps,p->paths[1],&p->apps_id)||
        !cp_fstatat(p->apps,p->subject.title,&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||
        bw_registration_db_match(p->paths[2],p->subject.title,p->subject.content)!=0||gur_current(p)?-1:0;
}
static int gur_absence(void *opaque,const struct bw_nrc_subject *s){
    struct gur *p=opaque;
    return !p->bound||!s||strcmp(s->title,p->subject.title)||strcmp(s->source,p->subject.source)||
        strcmp(s->content,p->subject.content)||strcmp(s->game,p->subject.game)||
        memcmp(s->prepared_record_sha256,p->subject.prepared_record_sha256,32)||gur_absent(p)?-1:0;
}
static int gur_registration_owner_capture(struct gur *p){
    if(p->generation_observe_allowed){
        struct bw_nrc_result observed={0};
        int status=bw_nrl_reconcile_prepared_generation(&p->request.environment,&p->selection,
            p->remote.connection.port,gur_current,p,&observed);
        if(status){p->generation_release_unknown=status==BW_NRC_RELEASE_UNCONFIRMED;
            p->reason=observed.stage?observed.stage:observed.reason?observed.reason:"previous_generation_observation_unconfirmed";return -1;}
    }
    struct bw_nrc_candidate *list=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *list);size_t n=0;int rc=-1;
    if(!list)return -1;int candidates=bw_nrc_candidates(&p->request.environment,list,BW_NRC_CANDIDATE_LIMIT,&n);
    if(candidates){if(candidates==BW_NRC_RELEASE_UNCONFIRMED){p->release_unknown=1;p->release_reason="registration_candidates_release_unconfirmed";}goto done;}
    for(size_t i=0;i<n;i++)if(!strcmp(list[i].subject.title,p->subject.title)){
        const struct bw_nrc_subject *s=&list[i].subject;
        if(p->owner_known||list[i].pending||list[i].retiring||strcmp(s->game,p->subject.game)||strcmp(s->source,p->subject.source)||
           strcmp(s->content,p->subject.content)||memcmp(s->prepared_record_sha256,p->subject.prepared_record_sha256,32)){
            p->reason="registration_owner_is_pending_or_differs_from_confirmed_prepared_copy";goto done;
        }p->owner=*s;p->owner_known=1;
    }rc=0;
done:free(list);return rc;
}
static int gur_bind(struct gur *p,const struct bw_removed_source *s){
    if(gur_subject(p,s))return -1;if(p->bound)return 0;
    if(p->binding_started++)return -1;
    const struct bw_source_binding *b=&p->request.identity.binding;char parent[BW_NRC_PATH_BYTES],path[BW_NRC_PATH_BYTES],title[10];
    if(b->game[74]||strlen(b->game)!=74||strcmp(b->title,p->request.confirmed_title)||gur_join(parent,p->paths[0],"games")||gur_join(p->root,parent,b->game))return -1;
    p->source.parent=gur_dir(p,parent,&p->source.parent_identity);p->source.root=gur_dir(p,p->root,&p->source.root_identity);p->apps=gur_dir(p,p->paths[1],&p->apps_id);
    if(p->source.parent<0||p->source.root<0||p->apps<0||gur_join(path,p->root,"sce_sys/param.json")||gur_read(p,path,BW_SOURCE_PARAM_LIMIT,&p->source_param)||
       bw_source_param_identity(p->source_param.body,p->source_param.size,title,p->subject.content)||strcmp(title,b->title))return -1;
    strcpy(p->subject.source,p->root);strcpy(p->subject.title,b->title);strcpy(p->subject.game,b->game);strcpy(p->subject.run,p->request.operation);memcpy(p->subject.prepared_record_sha256,p->request.prepared.record_sha256,32);
    int length=snprintf(path,sizeof path,"%s/native-prepared/%s/prepared.bin",p->paths[0],b->game);
    if(length<0||(size_t)length>=sizeof path||gur_read(p,path,384,&p->receipt)||p->receipt.size!=384||memcmp(p->receipt.hash,p->request.prepared.record_sha256,32))return -1;
    /* Only registration's address/identity view uses this temporary selection.
     * Header facts and boot size are actual held local metadata, not invented
     * Ready authority; the core already verified the complete map/inventory. */
    unsigned char head[40];struct stat boot,st,named;int map=v2_openat(p->source.root,"blackwire.files",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0),bad=1;
    if(map>=0&&!fstat(map,&st)&&S_ISREG(st.st_mode)&&pread(map,head,sizeof head,0)==sizeof head&&!memcmp(head,BW_MANIFEST_MAGIC,8)&&
       head[8]==BW_MANIFEST_VERSION&&head[9]==0&&head[10]==0&&head[11]==0&&!cp_fstatat(p->source.root,"blackwire.files",&named,AT_SYMLINK_NOFOLLOW)&&gur_same(&st,&named)&&
       !cp_fstatat(p->source.root,"eboot.bin",&boot,AT_SYMLINK_NOFOLLOW)&&S_ISREG(boot.st_mode)&&boot.st_size>0){
        p->selection.source_kind=p->request.identity.kind;p->selection.source_policy=BW_PROVIDER_POLICY_V1;
        strcpy(p->selection.source,b->source);strcpy(p->selection.game,b->game);strcpy(p->selection.title,b->title);strcpy(p->selection.run,p->request.operation);
        for(unsigned i=0;i<32;i++){snprintf(p->selection.binding_hex+2*i,3,"%02x",b->sha256[i]);snprintf(p->selection.prepared_record_hex+2*i,3,"%02x",p->request.prepared.record_sha256[i]);}
        p->selection.eboot_size=(uint64_t)boot.st_size;
        for(unsigned i=0;i<4;i++)p->selection.manifest_count|=(uint32_t)head[16+i]<<(8*i);
        for(unsigned i=0;i<8;i++)p->selection.manifest_hash|=(uint64_t)head[32+i]<<(8*i);bad=0;
    }
    if(map>=0&&gur_release(p,map))bad=1;if(bad||gur_registration_owner_capture(p))return -1;
    enum bw_registration_state state=bw_native_registration_state(p->paths[0],p->paths[1],p->paths[2],&p->selection);
    if(p->files_accepted&&state!=BW_REG_ABSENT)return -1;
    if(state==BW_REG_MATCH){
        if(gur_join(path,p->paths[1],b->title))return -1;p->app=gur_dir(p,path,&p->app_id);if(p->app<0)return -1;
        if(gur_join(parent,path,"sce_sys/param.json")||gur_read(p,parent,BW_SOURCE_PARAM_LIMIT,&p->app_param)||
           bw_registration_param_pair(p->source_param.body,p->source_param.size,p->app_param.body,p->app_param.size)<0||
           gur_join(parent,path,"mount.lnk")||gur_read(p,parent,BW_NRC_PATH_BYTES+2u,&p->mount))return -1;
        p->app_known=1;
    }else if(state!=BW_REG_ABSENT||gur_absent(p)||gur_absent(p))return -1;
    p->bound=1;return gur_current(p);
}
static int gur_outside(struct gur *p){
    struct bw_sm_reply version={0};struct bw_sm_document docs[2]={{0}};int rc=-1;
    if(gur_current(p)||bw_sm_post(&p->remote.connection,BW_SM_VERSION,"/",&version)||!version.uninstall_supported||!version.remove_supported||!version.settings_supported)goto done;
    for(unsigned i=0;i<2;i++)if(bw_sm_observe(&p->remote.connection,BW_SM_OBSERVE_SETTINGS,docs+i)||bw_nrl_settings_exclude_retained(&p->remote.connection,&p->subject,docs[i].bytes,docs[i].size,&p->source))goto done;
    if(docs[0].size!=docs[1].size||memcmp(docs[0].bytes,docs[1].bytes,docs[0].size)||gur_current(p))goto done;rc=0;
done:for(unsigned i=0;i<2;i++)bw_sm_document_clear(docs+i);return rc;
}
static int gur_manual(struct gur *p){
    struct bw_sm_reply a={0},b={0};if(gur_current(p)||bw_sm_post(&p->remote.connection,BW_SM_MANUAL_LIST,p->root,&a)||bw_sm_post(&p->remote.connection,BW_SM_MANUAL_LIST,p->root,&b)||a.present!=b.present||a.count!=b.count||gur_current(p))return -1;return a.present?1:0;
}
static int gur_source_current(struct gur *p){
    char path[BW_NRC_PATH_BYTES];int n;
    if(!p->bound||gur_dir_current(p->source.root,p->root,&p->source.root_identity)||
       gur_join(path,p->root,"sce_sys/param.json")||gur_blob_current(p,path,&p->source_param))return -1;
    n=snprintf(path,sizeof path,"%s/native-prepared/%s/prepared.bin",p->paths[0],p->request.identity.binding.game);
    return n<0||(size_t)n>=sizeof path||gur_blob_current(p,path,&p->receipt)||gur_current(p)?-1:0;
}
static int gur_confirmed_registered(void *,const struct bw_nrc_subject *);
static int gur_safe(void *opaque,const struct bw_removed_source *s){
    struct gur *p=opaque;return gur_bind(p,s)||gur_remote_result(p,bw_nrc_remote_confirmed_sources(&p->remote,&p->subject,gur_confirmed_registered,p))||gur_outside(p)||gur_source_current(p)?-1:0;
}
static int gur_excluded(void *opaque,const struct bw_removed_source *s){
    struct gur *p=opaque;if(!p->bound&&(!p->files_accepted||gur_bind(p,s)))return -1;
    return gur_subject(p,s)||gur_remote_result(p,bw_nrc_remote_absent_sources(&p->remote,&p->subject,gur_absence,p))||
        gur_manual(p)!=0||gur_outside(p)||gur_absence(p,&p->subject)?-1:0;
}
static int gur_registration_current(struct gur *p){
    char path[BW_NRC_PATH_BYTES],app[BW_NRC_PATH_BYTES];struct stat st;
    if(!p->app_known||gur_current(p)||gur_join(app,p->paths[1],p->subject.title)||gur_dir_current(p->app,app,&p->app_id)||
       gur_join(path,p->root,"sce_sys/param.json")||gur_blob_current(p,path,&p->source_param)||
       gur_join(path,app,"sce_sys/param.json")||gur_blob_current(p,path,&p->app_param)||
       bw_registration_db_match(p->paths[2],p->subject.title,p->subject.content)!=1)return -1;
    const char *forbidden[]={"app.pkg","mount_img.lnk","mount_image.lnk"};
    for(unsigned i=0;i<3;i++)if(!cp_fstatat(p->app,forbidden[i],&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
    if(!cp_fstatat(p->app,"mount.lnk",&st,AT_SYMLINK_NOFOLLOW)){
        if(gur_join(path,app,"mount.lnk")||gur_blob_current(p,path,&p->mount))return -1;
    }else if(errno!=ENOENT||!p->manual_called)return -1;
    return gur_current(p);
}
static int gur_confirmed_registered(void *opaque,const struct bw_nrc_subject *s){
    struct gur *p=opaque;if(!p->bound||!s||memcmp(s,&p->subject,sizeof *s)||gur_current(p)||gur_source_current(p))return -1;
    if(!gur_absent(p)&&!gur_absent(p))return 0;
    return p->app_known&&!gur_registration_current(p)&&!gur_registration_current(p)?1:-1;
}
static int gur_runtime_release(struct gur *p){
    if(!p->app_known)return gur_absent(p)||gur_absent(p)?-1:0;
    if(!p->runtime_release_done){
        if(gur_remote_result(p,bw_nrc_remote_release_runtime(&p->remote,&p->subject,gur_confirmed_registered,p))){
            p->reason=bw_nrc_remote_status_message(&p->remote);return -1;
        }p->runtime_release_done=1;
    }
    /* Remote collection can block after the prior local witness. Keep the
     * exact retained source/app/receipt check last before request authority. */
    return gur_confirmed_registered(p,&p->subject)!=1||gur_remote_result(p,bw_nrc_remote_sources(&p->remote,&p->subject))||
        gur_confirmed_registered(p,&p->subject)!=1?-1:0;
}
static int gur_registration(void *opaque,const struct bw_removed_source *s){
    struct gur *p=opaque;p->callback_stage="entry";if(p->registration_called++)return -1;
    p->callback_stage="initial_safe_recheck";if(gur_safe(p,s))return -1;
    p->callback_stage="initial_manual_presence";int manual=gur_manual(p);if(manual<0)return -1;
    /* Diagnostic stage assignments add no checks, queries, retries or time.
     * The core's durable .registration still precedes this one-shot unit. */
    p->callback_stage="initial_runtime_release";if(gur_runtime_release(p))return -1;
    if(manual){
        p->callback_stage="source_before_manual";if(gur_source_current(p))return -1;
        p->callback_stage="registration_before_manual";if(p->app_known&&gur_registration_current(p))return -1;
        struct bw_sm_reply reply={0};p->manual_called=1;p->callback_stage="manual_remove_call";
        int rc=bw_sm_post(&p->remote.connection,BW_SM_MANUAL_REMOVE,p->root,&reply);p->manual_result=rc;
        p->reason=rc?"manual_remove_outcome_unknown_no_retry":"manual_remove_requested_readback_required";
    }
    p->callback_stage="poll_start_clock";double start=gur_now(),deadline=start+10.0;if(start<0)return -1;
    for(unsigned pass=0;pass<100;pass++){
        p->callback_stage="poll_clock";double at=gur_now();if(at<0)return -1;
        p->callback_stage="poll_deadline";if(at>=deadline)return -1;
        p->callback_stage="poll_current";if(gur_current(p))return -1;
        p->callback_stage="poll_manual_presence";int present=gur_manual(p);if(present<0)return -1;
        if(!present){
            p->callback_stage="poll_title_absence";
            if(!gur_absent(p)&&!gur_absent(p)){p->callback_stage="absent_source_exclusion";return gur_excluded(p,s);}
            if(!p->uninstall_called){
                p->callback_stage="registration_before_uninstall";if(gur_registration_current(p))return -1;
                p->callback_stage="safe_before_uninstall";if(gur_safe(p,s))return -1;
                p->callback_stage="manual_absent_before_uninstall";if(gur_manual(p)!=0)return -1;
                p->callback_stage="runtime_before_uninstall";if(gur_runtime_release(p))return -1;
                struct bw_sm_uninstall_reply reply={0};p->uninstall_called=1;p->callback_stage="uninstall_call";
                int rc=bw_sm_uninstall(&p->remote.connection,p->subject.title,&reply);p->uninstall_result=rc;p->uninstall_reply=reply;
                p->reason=rc?(reply.refused_before_platform?bw_nrc_remote_status_message(&(struct bw_nrc_remote){.reason=!strcmp(reply.error_reason,"title_unmount_busy")?"cleanup_uninstall_refused_title_unmount_busy_before_platform":"cleanup_uninstall_refused_before_platform"}):"normal_uninstall_outcome_unknown_no_retry"):"normal_uninstall_requested_absence_unverified";
            }
        }
        p->callback_stage="poll_sleep";struct timespec delay={0,100000000};if(nanosleep(&delay,NULL))return -1;
    }
    p->callback_stage="poll_limit";return 1;
}
static void gur_observe(void *opaque,const struct bw_game_uninstall_progress *value){struct gur *p=opaque;if(p->outer.observe)p->outer.observe(p->outer.opaque,value);}
static int gur_owner_sources(void *opaque,const struct bw_nrc_subject *subject){
    struct gur *p=opaque;return !p->owner_known||!subject||memcmp(subject,&p->owner,sizeof *subject)||gur_current(p)||
        gur_remote_result(p,bw_nrc_remote_absent_sources(&p->remote,subject,gur_absence,p))?-1:0;
}
static int gur_owner_manual(void *opaque,const struct bw_nrc_subject *subject){
    struct gur *p=opaque;return !p->owner_known||!subject||memcmp(subject,&p->owner,sizeof *subject)?-1:gur_manual(p);
}
static int gur_owner_complete(struct gur *p){
    if(!p->owner_known)return 0;
    struct bw_removed_source removed={.identity=p->request.identity};memcpy(removed.operation,p->request.operation,33);memcpy(removed.prepared_sha256,p->request.prepared.record_sha256,32);
    struct bw_nrc_removed_observer ops={.opaque=p,.current=gur_current,.sources=gur_owner_sources,.manual_present=gur_owner_manual};struct bw_nrc_result result={0};
    return bw_nrc_observe_removed(&p->request.environment,p->owner.title,p->owner.run,&removed,&ops,&result)||result.state!=BW_NRC_ABSENT?-1:0;
}
static struct gur *gur_create(const struct bw_game_uninstall_request *q,const struct bw_game_uninstall_files_request *files,uint16_t port,struct bw_game_uninstall_result *out){
    if(!out)return NULL;*out=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_UNKNOWN,.reason="normal_adapter_request_unqualified"};
    if(!q||!q->ops.current)return NULL;struct gur *p=calloc(1,sizeof *p);if(!p)return NULL;
    p->source.parent=p->source.root=p->apps=p->app=-1;p->accepted=q;p->files_accepted=files;p->request=*q;p->outer=q->ops;
    const char *paths[]={q->environment.data_root,q->environment.apps_root,q->environment.database};
    for(unsigned i=0;i<3;i++){if(!paths[i]||strnlen(paths[i],sizeof p->paths[i])>=sizeof p->paths[i]){free(p);return NULL;}strcpy(p->paths[i],paths[i]);}
    p->request.environment=(struct bw_nrc_environment){p->paths[0],p->paths[1],p->paths[2]};p->remote.connection=(struct bw_sm_connection){port,gur_current,p};
    return p;
}
/* Escape into a fixed encoded budget: no raw paths, response bodies or
 * credentials. This record is evidence only and never supplies authority. */
static void gur_diag_text(char *out,size_t capacity,const char *text){
    size_t used=0;const unsigned char *p=(const unsigned char *)(text?text:"");
    for(;*p;p++){unsigned c=*p;size_t n=c>=33&&c<=126&&c!='"'&&c!='\\'?1:4;
        if(n>capacity-1-used)break;
        if(n==1)out[used++]=(char)c;
        else{static const char hex[]="0123456789abcdef";out[used++]='\\';out[used++]='x';out[used++]=hex[c>>4];out[used++]=hex[c&15];}}
    out[used]=0;
}
static void gur_finish_diag(const struct gur *p,int rc,const char *core_reason,const struct bw_game_uninstall_result *out){
    char core[193],adapter[193],remote[129],substage[129],path_class[65],error_reason[65];
    gur_diag_text(core,sizeof core,core_reason);gur_diag_text(adapter,sizeof adapter,p->reason);
    gur_diag_text(remote,sizeof remote,p->remote.reason);gur_diag_text(substage,sizeof substage,p->remote.substage);
    gur_diag_text(path_class,sizeof path_class,p->remote.path_class);gur_diag_text(error_reason,sizeof error_reason,p->uninstall_reply.error_reason);
    bw_send_log("normal-uninstall-finish","operation=%.32s rc=%d phase=%u mutation=%u files=%u/%u releaseUnknown=%u registrationCalled=%u manualCalled=%u uninstallCalled=%u runtimeReleased=%u manualResult=%d uninstallResult=%d uninstallComplete=%u uninstallHttp=%u uninstallApi=%u uninstallRefused=%u uninstallRequested=%u callbackStage=%s remoteErrno=%d remoteRow=%u remoteMissing=%u remoteReleaseUnknown=%u coreReason=\"%s\" adapterReason=\"%s\" remoteReason=\"%s\" substage=\"%s\" pathClass=\"%s\" apiReason=\"%s\"",
        p->request.operation,rc,(unsigned)out->phase,out->mutation_started,out->files_removed,out->files_total,out->release_unknown,
        p->registration_called,p->manual_called,p->uninstall_called,p->runtime_release_done,p->manual_result,p->uninstall_result,
        p->uninstall_reply.response_complete,p->uninstall_reply.http_status,p->uninstall_reply.api_status,p->uninstall_reply.refused_before_platform,p->uninstall_reply.requested,
        p->callback_stage?p->callback_stage:"not_entered",p->remote.observation_error,p->remote.row_index,p->remote.missing_fields,p->remote.observation_release_unknown,
        core,adapter,remote,substage,path_class,error_reason);
}
static int gur_finish(struct gur *p,int rc,struct bw_game_uninstall_result *out){
    const char *core_reason=out->reason;
    if(rc&&p->reason)out->reason=p->reason;
    if(p->generation_release_unknown){p->release_unknown=1;if(!p->release_reason)p->release_reason="previous_generation_observation_release_unconfirmed";rc=-1;out->phase=BW_GAME_REMOVE_UNKNOWN;out->reason="previous_generation_observation_release_unconfirmed";}
    if(!rc&&out->phase==BW_GAME_REMOVE_DONE&&gur_owner_complete(p)){rc=-1;out->phase=BW_GAME_REMOVE_UNKNOWN;out->reason="removed_copy_registration_owner_completion_unconfirmed";}
    {int fds[]={p->app,p->apps,p->source.root,p->source.parent};for(unsigned i=0;i<4;i++)if(fds[i]>=0&&gur_release(p,fds[i])){rc=-1;out->phase=BW_GAME_REMOVE_UNKNOWN;out->reason="normal_adapter_descriptor_close_unconfirmed";}}
    if(p->remote.observation_release_unknown||p->release_unknown){out->release_unknown=1;rc=-1;out->phase=BW_GAME_REMOVE_UNKNOWN;out->reason=p->release_reason?p->release_reason:"normal_remote_observation_release_unconfirmed";}
    gur_finish_diag(p,rc,core_reason,out);
    free(p->source_param.body);free(p->app_param.body);free(p->mount.body);free(p->receipt.body);free(p);return rc;
}
int bw_native_game_uninstall_normal(const struct bw_game_uninstall_request *q,uint16_t port,struct bw_game_uninstall_result *out){
    struct gur *p=gur_create(q,NULL,port,out);if(!p)return -1;
    p->generation_observe_allowed=1; /* Explicit ordinary Uninstall only. */
    p->request.ops=(struct bw_game_uninstall_ops){.opaque=p,.current=gur_current,.registration_absent=gur_registration,.source_excluded=gur_excluded,.source_safe=gur_safe,.observe=gur_observe};
    int rc=bw_native_game_uninstall(&p->request,out);return gur_finish(p,rc,out);
}
int bw_native_game_uninstall_files_normal(const struct bw_game_uninstall_files_request *q,uint16_t port,struct bw_game_uninstall_result *out){
    if(!q){if(out)*out=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_UNKNOWN,.reason="file_only_adapter_request_unqualified"};return -1;}
    struct bw_game_uninstall_request common={.environment=q->environment,.identity=q->identity,.prepared=q->prepared,
        .ops={.opaque=q->ops.opaque,.current=q->ops.current,.observe=q->ops.observe},.reader_size=q->reader_size};
    memcpy(common.operation,q->operation,33);memcpy(common.confirmed_title,q->confirmed_title,10);memcpy(common.reader_sha256,q->reader_sha256,32);
    struct gur *p=gur_create(&common,q,port,out);if(!p)return -1;
    struct bw_game_uninstall_files_request next={.environment=p->request.environment,.identity=p->request.identity,
        .prepared=p->request.prepared,.ops={.opaque=p,.current=gur_current,.source_excluded=gur_excluded,.observe=gur_observe},.reader_size=p->request.reader_size};
    memcpy(next.operation,p->request.operation,33);memcpy(next.confirmed_title,p->request.confirmed_title,10);memcpy(next.reader_sha256,p->request.reader_sha256,32);
    int rc=bw_native_game_uninstall_continue_files(&next,out);return gur_finish(p,rc,out);
}

/* Explicit registration-only maintenance. Its action journal is deliberately
 * separate from automatic event ownership and complete game Uninstall. */
#define NRM_INTENT 4096u
#define NRM_PHASE 128u
static const char nrm_marker[]="Blackwire registration maintenance actions v1\n";
struct nrm {
    const struct bw_registration_maintenance_request *accepted;
    struct bw_registration_maintenance_request captured;
    char roots[3][BW_NRC_PATH_BYTES],journal_path[BW_NRC_PATH_BYTES],prefix[48];
    int data,journal;
    struct stat data_id,journal_id;
    struct gur *bound;
    unsigned char intent[NRM_INTENT];
};
static int nrm_nonce(const char *s){if(strnlen(s,33)!=32)return -1;unsigned n=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",s[i]))return -1;n|=s[i]!='0';}return n?0:-1;}
static int nrm_current(void *opaque){
    struct nrm *m=opaque;const struct bw_registration_maintenance_request *q=m->accepted;
    if(memcmp(q,&m->captured,sizeof *q)||strcmp(q->environment.data_root,m->roots[0])||strcmp(q->environment.apps_root,m->roots[1])||
       strcmp(q->environment.database,m->roots[2])||!m->captured.current||m->captured.current(m->captured.opaque))return -1;
    if(m->data>=0&&gur_dir_current(m->data,m->roots[0],&m->data_id))return -1;
    if(m->journal>=0&&gur_dir_current(m->journal,m->journal_path,&m->journal_id))return -1;
    return 0;
}
static int nrm_read(struct nrm *m,const char *leaf,const void *expected,size_t size){
    if(!expected&&size!=NRM_INTENT)return -1;
    int fd=v2_openat(m->journal,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    unsigned char *body=malloc(size?size:1);struct stat first,held,named;int rc=-1;
    if(!body||fstat(fd,&first)||!S_ISREG(first.st_mode)||(first.st_mode&07777)!=0600||first.st_uid!=geteuid()||first.st_nlink!=1||first.st_size!=(off_t)size)goto done;
    for(size_t at=0;at<size;){ssize_t n=pread(fd,body+at,size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at)goto done;at+=(size_t)n;}
    if((expected&&memcmp(body,expected,size))||fstat(fd,&held)||cp_fstatat(m->journal,leaf,&named,AT_SYMLINK_NOFOLLOW)||!gur_same(&first,&held)||!gur_same(&held,&named)||nrm_current(m))goto done;
    if(!expected)memcpy(m->intent,body,size);rc=0;
done:free(body);if(close(fd))rc=-1;return rc;
}
static int nrm_write(struct nrm *m,const char *leaf,const void *body,size_t size){
    if(nrm_current(m))return -1;int fd=v2_openat(m->journal,leaf,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK,0600);if(fd<0)return -1;int bad=0;
    for(size_t at=0;at<size;){ssize_t n=write(fd,(const unsigned char *)body+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at){bad=1;break;}at+=(size_t)n;}
    if(fsync(fd))bad=1;if(close(fd))bad=1;if(fsync(m->journal))bad=1;return bad||nrm_read(m,leaf,body,size)?-1:0;
}
static void nrm_stat(unsigned char out[128],const struct stat *s){
    memset(out,0,128);uint64_t values[]={(uint64_t)s->st_dev,(uint64_t)s->st_ino,(uint64_t)s->st_mode,(uint64_t)s->st_uid,(uint64_t)s->st_gid,
        (uint64_t)s->st_nlink,(uint64_t)s->st_size,(uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,(uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};
    for(unsigned i=0;i<sizeof values/sizeof values[0];i++)for(unsigned j=0;j<8;j++)out[8*i+j]=(unsigned char)(values[i]>>(8*j));
}
static void nrm_encode(struct nrm *m){
    struct gur *p=m->bound;unsigned char *v=m->intent;memset(v,0,NRM_INTENT);memcpy(v,"BWNRM001",8);v[8]=1;v[13]=16;
    memcpy(v+16,p->request.identity.binding.sha256,32);memcpy(v+48,p->request.prepared.record_sha256,32);memcpy(v+80,p->request.operation,33);
    memcpy(v+128,p->subject.title,10);memcpy(v+144,p->subject.game,75);memcpy(v+224,p->subject.source,strlen(p->subject.source)+1);memcpy(v+2272,p->subject.content,128);
    memcpy(v+2400,p->source_param.hash,32);nrm_stat(v+2432,&p->receipt.id);nrm_stat(v+2560,&p->source.root_identity);nrm_stat(v+2688,&p->source.parent_identity);
    nrm_stat(v+2816,&p->apps_id);if(p->app_known){nrm_stat(v+2944,&p->app_id);memcpy(v+3072,p->app_param.hash,32);memcpy(v+3104,p->mount.hash,32);v[3136]=1;}
    cp_sha_bytes(v,NRM_INTENT-32,v+NRM_INTENT-32);
}
static int nrm_existing(struct nrm *m){
    unsigned char expected[NRM_INTENT],hash[32];memcpy(expected,m->intent,sizeof expected);
    char leaf[64];snprintf(leaf,sizeof leaf,"%s.intent",m->prefix);int rc=nrm_read(m,leaf,NULL,NRM_INTENT);if(rc)return rc;
    cp_sha_bytes(m->intent,NRM_INTENT-32,hash);
    if(memcmp(m->intent,"BWNRM001",8)||m->intent[8]!=1||m->intent[13]!=16||memcmp(hash,m->intent+NRM_INTENT-32,32)||
       memcmp(m->intent+16,expected+16,64)||memcmp(m->intent+80,expected+80,33)||memcmp(m->intent+128,expected+128,2272-128)||
       memcmp(m->intent+2272,expected+2272,544)||memcmp(m->intent+2816,expected+2816,40))return -1;
    return 0;
}
static void nrm_phase(struct nrm *m,const char magic[8],unsigned char bytes[NRM_PHASE]){
    memset(bytes,0,NRM_PHASE);memcpy(bytes,magic,8);memcpy(bytes+16,m->intent+NRM_INTENT-32,32);memcpy(bytes+48,m->captured.operation,33);cp_sha_bytes(bytes,96,bytes+96);
}
static int nrm_phase_read(struct nrm *m,const char *suffix,const char magic[8]){char leaf[64];unsigned char phase[NRM_PHASE];snprintf(leaf,sizeof leaf,"%s.%s",m->prefix,suffix);nrm_phase(m,magic,phase);return nrm_read(m,leaf,phase,sizeof phase);}
static int nrm_phase_write(struct nrm *m,const char *suffix,const char magic[8]){char leaf[64];unsigned char phase[NRM_PHASE];snprintf(leaf,sizeof leaf,"%s.%s",m->prefix,suffix);nrm_phase(m,magic,phase);return nrm_write(m,leaf,phase,sizeof phase);}
static int nrm_journal(struct nrm *m){
    m->data=gur_dir(m->bound,m->roots[0],&m->data_id);if(m->data<0||m->data_id.st_uid!=geteuid()||(m->data_id.st_mode&0022))return -1;
    int made=!v2_mkdirat(m->data,"registration-maintenance",0700);if(!made&&errno!=EEXIST)return -1;
    if(gur_join(m->journal_path,m->roots[0],"registration-maintenance"))return -1;m->journal=gur_dir(m->bound,m->journal_path,&m->journal_id);
    if(m->journal<0||m->journal_id.st_uid!=geteuid()||(m->journal_id.st_mode&07777)!=0700)return -1;
    if(made){if(nrm_write(m,".blackwire-registration-maintenance",nrm_marker,sizeof nrm_marker-1)||fsync(m->data))return -1;}
    else if(nrm_read(m,".blackwire-registration-maintenance",nrm_marker,sizeof nrm_marker-1))return -1;
    snprintf(m->prefix,sizeof m->prefix,"%s.%s",m->captured.confirmed_title,m->captured.operation);
    int scan=v2_openat(m->journal,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(scan<0)return -1;DIR *dir=fdopendir(scan);if(!dir){close(scan);return -1;}int bad=0;unsigned count=0;
    for(;;){errno=0;struct dirent *e=readdir(dir);if(!e){if(errno)bad=1;break;}if(++count>2048){bad=1;break;}
        const char *name=e->d_name;if(!strcmp(name,".")||!strcmp(name,"..")||!strcmp(name,".blackwire-registration-maintenance"))continue;
        size_t n=strlen(name);if(n<47||memcmp(name,"PPSA",4)||name[9]!='.'||name[42]!='.'){bad=1;break;}
        for(unsigned i=4;i<9;i++)if(name[i]<'0'||name[i]>'9')bad=1;if(bad)break;
        char nonce[33];memcpy(nonce,name+10,32);nonce[32]=0;if(nrm_nonce(nonce)){bad=1;break;}
        const char *suffix=name+43;if(strcmp(suffix,"intent")&&strcmp(suffix,"manual")&&strcmp(suffix,"uninstall")&&strcmp(suffix,"done")){bad=1;break;}
        if(!strncmp(name,m->captured.confirmed_title,9)&&strncmp(name,m->prefix,42)&&!strcmp(suffix,"intent")){
            /* A different same-title action must have a bound complete marker;
             * pending/unknown history cannot authorize another normal request. */
            unsigned char original[NRM_INTENT];memcpy(original,m->intent,sizeof original);char done[64];unsigned char old[NRM_INTENT],hash[32],phase[NRM_PHASE];
            if(nrm_read(m,name,NULL,NRM_INTENT)){bad=1;break;}memcpy(old,m->intent,sizeof old);memcpy(m->intent,original,sizeof original);
            cp_sha_bytes(old,NRM_INTENT-32,hash);if(memcmp(old,"BWNRM001",8)||memcmp(hash,old+NRM_INTENT-32,32)||memcmp(old+128,name,9)||memcmp(old+80,nonce,32)){bad=1;break;}
            memset(phase,0,sizeof phase);memcpy(phase,"BWNRMD01",8);memcpy(phase+16,hash,32);memcpy(phase+48,nonce,33);cp_sha_bytes(phase,96,phase+96);
            snprintf(done,sizeof done,"%.42s.done",name);if(nrm_read(m,done,phase,sizeof phase)){bad=1;break;}
        }
    }
    if(closedir(dir))bad=1;return bad||nrm_current(m)?-1:0;
}
static int nrm_registered(void *opaque,const struct bw_nrc_subject *s){
    struct nrm *m=opaque;struct gur *p=m->bound;
    if(!p||!s||strcmp(s->title,p->subject.title)||strcmp(s->source,p->subject.source)||strcmp(s->content,p->subject.content)||
       strcmp(s->game,p->subject.game)||memcmp(s->prepared_record_sha256,p->subject.prepared_record_sha256,32)||gur_source_current(p)||nrm_current(m))return -1;
    if(!gur_absent(p)&&!gur_absent(p))return 0;
    return p->app_known&&!gur_registration_current(p)&&!gur_registration_current(p)?1:-1;
}
static int nrm_sources(struct nrm *m){return bw_nrc_remote_confirmed_sources(&m->bound->remote,&m->bound->subject,nrm_registered,m)||gur_outside(m->bound)||nrm_current(m)?-1:0;}
static int nrm_preserved(struct nrm *m){
    struct bw_native_prepared fresh={0};return bw_native_send_verify(m->roots[0],&m->captured.identity,nrm_current,m,&fresh)||fresh.state!=BW_PREPARED_VALID||
        memcmp(fresh.record_sha256,m->captured.prepared.record_sha256,32)||strcmp(fresh.run,m->captured.prepared.run)||gur_source_current(m->bound)||nrm_current(m)?-1:0;
}
int bw_native_registration_maintain(const struct bw_registration_maintenance_request *q,uint16_t port,struct bw_registration_maintenance_result *out){
    if(out)*out=(struct bw_registration_maintenance_result){.state=BW_REG_MAINT_UNKNOWN,.reason="confirmed_registration_maintenance_unqualified"};
    if(!q||!out||!q->current||nrm_nonce(q->operation)||strnlen(q->confirmed_title,10)!=9||strcmp(q->confirmed_title,q->identity.binding.title))return -1;
    struct nrm m={.accepted=q,.captured=*q,.data=-1,.journal=-1};const char *roots[]={q->environment.data_root,q->environment.apps_root,q->environment.database};
    for(unsigned i=0;i<3;i++){if(!roots[i]||strnlen(roots[i],sizeof m.roots[i])>=sizeof m.roots[i])return -1;strcpy(m.roots[i],roots[i]);}
    struct bw_game_uninstall_request common={.environment=q->environment,.identity=q->identity,.prepared=q->prepared,.ops={.opaque=&m,.current=nrm_current}};
    memcpy(common.operation,q->operation,33);memcpy(common.confirmed_title,q->confirmed_title,10);struct bw_game_uninstall_result ignored={0};m.bound=gur_create(&common,NULL,port,&ignored);if(!m.bound)return -1;
    struct bw_removed_source subject={.identity=q->identity};memcpy(subject.operation,q->operation,33);memcpy(subject.prepared_sha256,q->prepared.record_sha256,32);int rc=-1,prior=-1;
    struct bw_game_uninstall_capability cap={0};
    if(nrm_current(&m)||bw_native_game_uninstall_probe(&q->environment,&q->identity,&q->prepared,&cap)||!cap.can_uninstall||gur_bind(m.bound,&subject)||m.bound->owner_known||nrm_preserved(&m)||nrm_sources(&m)){
        out->reason=m.bound->owner_known?"committed_registration_requires_its_owned_lifecycle":"confirmed_prepared_registration_binding_unavailable";goto done;}
    nrm_encode(&m);if(nrm_journal(&m)){out->reason="registration_maintenance_journal_unconfirmed";goto done;}
    prior=nrm_existing(&m);if(prior<0){out->reason="registration_maintenance_action_conflict";goto done;}
    if(prior==1){char leaf[64];if(nrm_phase_read(&m,"manual","BWNRMM01")!=1||nrm_phase_read(&m,"uninstall","BWNRMU01")!=1||nrm_phase_read(&m,"done","BWNRMD01")!=1){out->reason="registration_maintenance_orphan_phase_refused";goto done;}
        snprintf(leaf,sizeof leaf,"%s.intent",m.prefix);if(nrm_write(&m,leaf,m.intent,sizeof m.intent)){out->reason="registration_maintenance_intent_unconfirmed";goto done;}}
    /* A restarted/existing action is observation-only even if its original
     * manual request never happened. It cannot advance a normal request. */
    int manual=gur_manual(m.bound);if(manual<0){out->reason="registration_maintenance_manual_state_unknown";goto done;}
    if(prior==1&&manual){
        if(nrm_sources(&m)||nrm_phase_write(&m,"manual","BWNRMM01")){out->reason="registration_maintenance_manual_intent_unconfirmed";goto done;}
        if(gur_runtime_release(m.bound)){out->reason=m.bound->reason;goto done;}
        m.bound->manual_called=1;out->manual_attempted=1;struct bw_sm_reply reply={0};
        (void)bw_sm_post(&m.bound->remote.connection,BW_SM_MANUAL_REMOVE,m.bound->root,&reply);
    }
    double start=gur_now(),deadline=start+10.0;if(start<0)goto done;
    for(unsigned pass=0;pass<100;pass++){
        double at=gur_now();if(at<0||at>=deadline||nrm_current(&m))break;
        int present=gur_manual(m.bound);if(present<0)break;
        int state=nrm_registered(&m,&m.bound->subject);if(state<0)break;
        if(!present&&state==0){
            if(nrm_sources(&m)||nrm_preserved(&m)||nrm_phase_read(&m,"done","BWNRMD01")<0)break;
            int completed=nrm_phase_read(&m,"done","BWNRMD01");if(completed==1&&nrm_phase_write(&m,"done","BWNRMD01"))break;
            if(gur_source_current(m.bound)||nrm_current(&m))break;out->state=BW_REG_MAINT_ABSENT;out->prepared_retained=1;out->reason="registration_removed_prepared_data_and_saves_retained";rc=0;break;
        }
        if(prior==1&&!present&&state==1&&!out->uninstall_attempted){
            if(nrm_sources(&m)||nrm_phase_write(&m,"uninstall","BWNRMU01")||nrm_registered(&m,&m.bound->subject)!=1)break;
            if(gur_runtime_release(m.bound)){out->reason=m.bound->reason;goto done;}
            out->uninstall_attempted=1;struct bw_sm_uninstall_reply reply={0};(void)bw_sm_uninstall(&m.bound->remote.connection,m.bound->subject.title,&reply);
        }
        struct timespec delay={0,100000000};if(nanosleep(&delay,NULL))break;
    }
    if(rc)out->reason="registration_maintenance_outcome_unknown_observe_without_retry";
done:
    {struct gur *p=m.bound;int fds[]={p->app,p->apps,p->source.root,p->source.parent,m.journal,m.data};for(unsigned i=0;i<6;i++)if(fds[i]>=0&&close(fds[i])){rc=-1;out->state=BW_REG_MAINT_UNKNOWN;out->prepared_retained=0;out->reason="registration_maintenance_close_unconfirmed";}
        free(p->source_param.body);free(p->app_param.body);free(p->mount.body);free(p->receipt.body);free(p);}
    return rc;
}
