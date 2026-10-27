/* SPDX-License-Identifier: GPL-3.0-or-later
 * Private implementation included once by native_backend.c. */
#ifndef BW_NATIVE_BACKEND_REMOVAL_IMPL_H
#define BW_NATIVE_BACKEND_REMOVAL_IMPL_H
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
struct removal_default_control {
    int (*current)(void *);void *guard;
    void (*observe)(void *,const struct bw_game_uninstall_progress *);void *observer;
};
static int removal_default_current(void *v){struct removal_default_control *c=v;return !c||!c->current||c->current(c->guard)?-1:0;}
static void removal_default_observe(void *v,const struct bw_game_uninstall_progress *progress){struct removal_default_control *c=v;if(c->observe)c->observe(c->observer,progress);}
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
/* Bounded owner metadata only; never a prepared-tree or STREAM inventory. */
static int removal_registration_clear(const char title[10]){
    struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;
    struct bw_nrc_candidate *list=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *list);if(!list)return -1;
    size_t count=0;int bad=bw_nrc_candidates(&env,list,BW_NRC_CANDIDATE_LIMIT,&count);
    for(size_t i=0;!bad&&i<count;i++)if(!strcmp(list[i].subject.title,title))bad=1;
    free(list);return bad?-1:0;
}
#endif
static int default_removal_probe(void *opaque,const struct bw_provider_binding *identity,const struct bw_native_prepared *prepared,struct bw_game_uninstall_capability *out){
    (void)opaque;
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
    struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;int status=bw_native_game_uninstall_probe(&env,identity,prepared,out);
    if(!status&&out->removed&&removal_registration_clear(identity->binding.title)){out->can_restore=0;out->reason="Registration removal is still being independently confirmed. Refresh status without repeating Uninstall.";}
    return status;
#else
    (void)identity;(void)prepared;if(out)memset(out,0,sizeof *out);return -1;
#endif
}
static int default_remove(void *opaque,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *context,void (*observe)(void *,const struct bw_game_uninstall_progress *),void *observer,struct bw_game_uninstall_result *out,unsigned startup_only){
    (void)opaque;
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
    struct removal_default_control control={guard,context,observe,observer};struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;
    if(!out)return -1;memset(out,0,sizeof *out);
    if(selected->startup_only!=startup_only){out->phase=BW_GAME_REMOVE_ERROR;out->reason="The confirmed removal purpose changed.";return -1;}
    char root[1152];int n=snprintf(root,sizeof root,"%s/games/%s",backend_data_root,selected->identity.binding.game);
    if(n<0||(size_t)n>=sizeof root){out->phase=BW_GAME_REMOVE_ERROR;out->reason="The selected console source path is invalid.";return -1;}
    struct bw_preload_bundle_upgrade_request selection={.bundle={root,removal_default_current,&control,now()+20.0},
        .data_root=backend_data_root,.game=selected->identity.binding.game,.prepared_run=selected->prepared.run,
        .launch_run=selected->operation,.prepared_record_sha256=selected->prepared.record_sha256};
    struct bw_preload_reader_pin pin={0};struct bw_preload_bundle_result diagnostic={0};
    struct bw_native_launch_admission *startup=NULL;const struct bw_preload_source_ampr *source_ampr=NULL;const char *error=NULL;
    const struct bw_preload_companions *companions=NULL;int projected=selected->prepared.prepared_policy==3;
    const struct bw_preload_legacy_companions *legacy_companions=NULL;unsigned legacy_source=0;
    int status=bw_native_launch_admission_open_for_removal(&startup,backend_data_root,&selected->identity,&selected->prepared,
        selected->operation,removal_default_current,&control,&error);
    if(!status&&!projected){int applicability=bw_native_launch_admission_legacy_companions_for_removal(startup,&legacy_companions);
        if(applicability<0)status=-1;else legacy_source=applicability==0;}
    if(!status&&!legacy_source)status=projected?bw_native_launch_admission_companions_for_removal(startup,&companions):bw_native_launch_admission_source_ampr_for_removal(startup,&source_ampr);
    if(!status)status=legacy_source?bw_preload_bundle_select_legacy_source_for_removal(&selection,legacy_companions,&pin,&diagnostic):
        projected?bw_preload_bundle_select_projection_for_removal(&selection,companions,&pin,&diagnostic):bw_preload_bundle_select_source_for_removal(&selection,source_ampr,&pin,&diagnostic);
    /* This is local sealed ownership evidence only. Never connect the PC or
     * turn a removal witness into install/launch authority. The deletion core
     * independently rehashes its original source manifest below. */
    if(bw_native_launch_admission_close(&startup)){diagnostic.release_unknown=1;status=-2;}
    if(status||diagnostic.release_unknown){out->release_unknown=status==-2||diagnostic.release_unknown;out->phase=out->release_unknown?BW_GAME_REMOVE_UNKNOWN:BW_GAME_REMOVE_ERROR;
        out->reason=out->release_unknown?"Reader library checks could not be released. Refresh status without submitting removal again.":
            "The exact owned reader library set is unresolved or differs. No registration or files were changed.";
        return -1;}
    struct bw_game_uninstall_request q={.environment=env,.identity=selected->identity,.prepared=selected->prepared,.ops={.opaque=&control,.current=removal_default_current,.observe=removal_default_observe},.reader_size=pin.size,.legacy_source_fakelib2=legacy_source};
    memcpy(q.operation,selected->operation,33);memcpy(q.confirmed_title,selected->confirmed_title,10);memcpy(q.reader_sha256,pin.sha256,32);
    if(startup_only){
        struct bw_preparation_remove_request only={.environment=q.environment,.identity=q.identity,.prepared=q.prepared,
            .ops={.opaque=&control,.current=removal_default_current,.observe=removal_default_observe},.reader_size=q.reader_size,.legacy_source_fakelib2=q.legacy_source_fakelib2};
        memcpy(only.operation,q.operation,33);memcpy(only.confirmed_title,q.confirmed_title,10);memcpy(only.reader_sha256,q.reader_sha256,32);
        return bw_native_preparation_remove_normal(&only,0,out);
    }
    return bw_native_game_uninstall_normal(&q,0,out);
#else
    (void)selected;(void)guard;(void)context;(void)observe;(void)observer;(void)startup_only;if(out)memset(out,0,sizeof *out);return -1;
#endif
}
static int default_uninstall(void *o,const struct bw_native_removal_selection *s,int (*g)(void *),void *c,void (*v)(void *,const struct bw_game_uninstall_progress *),void *x,struct bw_game_uninstall_result *r){return default_remove(o,s,g,c,v,x,r,0);}
static int default_remove_startup(void *o,const struct bw_native_removal_selection *s,int (*g)(void *),void *c,void (*v)(void *,const struct bw_game_uninstall_progress *),void *x,struct bw_game_uninstall_result *r){return default_remove(o,s,g,c,v,x,r,1);}
struct removal_restore_guard {const struct bw_native_removal_selection *selected;int (*current)(void *);void *opaque;};
static int removal_restore_current(void *v){struct removal_restore_guard *g=v;return !g||!g->current||g->current(g->opaque)?-1:0;}
static int removal_absent_parent(const char *parent,const char *leaf){
    char real[PATH_MAX],path[2048];struct stat before,held,after;int n=snprintf(path,sizeof path,"%s/%s",parent,leaf);
    if(n<0||(size_t)n>=sizeof path||!realpath(parent,real)||strcmp(real,parent)||lstat(parent,&before)||!S_ISDIR(before.st_mode))return -1;
    int fd=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    int bad=fstat(fd,&held)||held.st_dev!=before.st_dev||held.st_ino!=before.st_ino||held.st_mode!=before.st_mode||held.st_uid!=before.st_uid||held.st_gid!=before.st_gid;
    if(!bad&&(!lstat(path,&after)||errno!=ENOENT))bad=1;
    if(!bad&&(lstat(parent,&after)||after.st_dev!=held.st_dev||after.st_ino!=held.st_ino||after.st_mode!=held.st_mode||after.st_uid!=held.st_uid||after.st_gid!=held.st_gid||!realpath(parent,real)||strcmp(real,parent)))bad=1;
    if(close(fd))bad=1;return bad?-1:0;
}
static int removal_restore_absent(void *v,const struct bw_removed_source *s){
    struct removal_restore_guard *g=v;
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
    /* The shared reservation retains the owner-clear check made immediately
     * before Restore. Do not acquire the owner-ledger mutex while the Removed
     * store holds its mutex; the owner observer reads the stores in the other
     * direction. No normal register/cleanup can run under this reservation. */
    if(removal_restore_current(g)||s->identity.kind!=g->selected->identity.kind||s->identity.policy!=g->selected->identity.policy||strcmp(s->identity.binding.source,g->selected->identity.binding.source)||strcmp(s->identity.binding.title,g->selected->identity.binding.title)||strcmp(s->identity.binding.game,g->selected->identity.binding.game)||memcmp(s->identity.binding.sha256,g->selected->identity.binding.sha256,32))return -1;
    char games[2048],prepared[2048];int a=snprintf(games,sizeof games,"%s/games",backend_data_root),b=snprintf(prepared,sizeof prepared,"%s/native-prepared",backend_data_root);if(a<0||(size_t)a>=sizeof games||b<0||(size_t)b>=sizeof prepared)return -1;
    /* This callback remains valid after Restore removes its own tombstone.
     * The only assertion is independent actual source/state/title absence. */
    for(unsigned i=0;i<2;i++)if(removal_restore_current(g)||removal_absent_parent(games,s->identity.binding.game)||removal_absent_parent(prepared,s->identity.binding.game)||removal_absent_parent(registration_environment.apps_root,s->identity.binding.title)||bw_registration_db_match(registration_environment.database,s->identity.binding.title,"BLACKWIRE_REMOVED_TITLE_ABSENCE")!=0||removal_restore_current(g))return -1;
    return 0;
#else
    (void)g;(void)s;return -1;
#endif
}
static int default_restore(void *opaque,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *context,struct bw_game_uninstall_result *out){
    (void)opaque;if(!out)return -1;memset(out,0,sizeof *out);
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
    struct removal_restore_guard g={selected,guard,context};if(removal_restore_current(&g)||removal_registration_clear(selected->identity.binding.title)){out->phase=BW_GAME_REMOVE_ERROR;out->reason="Restore is waiting for exact registration absence or its current authority changed.";return -1;}
    struct bw_removed_ops ops={&g,removal_restore_current,removal_restore_absent};struct bw_removed_result result={0};out->mutation_started=1;
    if(bw_removed_restore(backend_data_root,&selected->identity,selected->operation,&ops,&result)||result.state!=BW_REMOVED_RESTORED||removal_restore_current(&g)){out->phase=BW_GAME_REMOVE_UNKNOWN;out->reason="Restore could not be confirmed. Refresh status without submitting it again.";return -1;}
    out->phase=BW_GAME_REMOVE_DONE;out->registration_confirmed=out->source_absent=out->prepared_record_absent=1;out->reason="Source shown again. Send files to prepare a new console copy.";return 0;
#else
    (void)selected;(void)guard;(void)context;out->phase=BW_GAME_REMOVE_ERROR;out->reason="Restore is unavailable in this build.";return -1;
#endif
}
static int removal_cache(struct bw_native_backend *p,struct bw_native_library *library,const struct bw_share_config *config){
    if(!p->ops.removal_probe||!library->catalog.count)return 0;
    library->removal=calloc(library->catalog.count,sizeof *library->removal);if(!library->removal)return -1;
    for(size_t i=0;i<library->catalog.count;i++){
        const struct bw_catalog_item *item=library->catalog.items+i;if(!bw_source_catalog_available(item))continue;
        struct bw_provider_binding identity={0};struct bw_native_prepared absent={.state=BW_PREPARED_ABSENT};const struct bw_native_prepared *prepared=library->prepared?library->prepared+i:&absent;
        if(bw_source_catalog_binding(&identity,config,item)||p->ops.removal_probe(p->opaque,&identity,prepared,library->removal+i)){memset(library->removal+i,0,sizeof *library->removal);library->removal[i].reason="The exact owned-copy removal metadata is unavailable.";}
    }return 0;
}
static const char *removal_status(unsigned state){return state==REMOVAL_RUNNING?"running":state==REMOVAL_DONE?"done":state==REMOVAL_ERROR?"error":state==REMOVAL_UNKNOWN?"unknown":"idle";}
static void removal_state_json(struct bw_json *json,const struct bw_native_backend *p){
    char diagnostic[193];snprintf(diagnostic,sizeof diagnostic,"%.192s",p->removing.result.reason?p->removing.result.reason:"");
    size_t diagnostic_size=strlen(diagnostic);while(diagnostic_size&&!bw_json_utf8(diagnostic))diagnostic[--diagnostic_size]=0;
    bw_json_text(json,"\"removal\":{\"operationId\":");bw_json_string(json,p->removing.selected.operation);bw_json_text(json,",\"source\":");bw_json_string(json,p->removing.selected.identity.binding.source);bw_json_text(json,",\"titleId\":");bw_json_string(json,p->removing.selected.identity.binding.title);
    bw_json_text(json,",\"state\":");bw_json_string(json,removal_status(p->removing.state));bw_json_format(json,",\"phase\":%u,\"files\":%u,\"filesTotal\":%u,\"message\":",(unsigned)p->removing.phase,p->removing.files,p->removing.total);bw_json_string(json,p->removing.message);
    bw_json_text(json,",\"diagnosticReason\":");bw_json_string(json,diagnostic);
    bw_json_format(json,",\"preparationOnly\":%s",p->removing.startup_only?"true":"false");
    bw_json_format(json,",\"removed\":%s,\"releaseUnknown\":%s}",p->removing.state==REMOVAL_DONE&&!p->removing.restore&&!p->removing.startup_only?"true":"false",p->removing.result.release_unknown?"true":"false");
}
static int removal_current(void *opaque){
    struct bw_native_backend *p=opaque;struct bw_setup_generation expected={0};struct bw_native_removal_selection selected;uint64_t generation=0,epoch=0;uint32_t revision=0;
    pthread_mutex_lock(&p->lock);selected=p->removing.selected;expected=selected.connection;generation=p->removing.generation;epoch=p->removing.epoch;revision=p->removing.revision;
    int bad=!p->active||(p->worker_kind!=WORK_UNINSTALL&&p->worker_kind!=WORK_RESTORE)||p->removing.state!=REMOVAL_RUNNING||p->removing.unknown||strcmp(p->external_nonce,selected.operation)||p->generation!=generation||p->play_epoch!=epoch||p->settings_revision!=revision||p->stale;
    pthread_mutex_unlock(&p->lock);if(bad)return -1;
    struct bw_share_config config={0};struct bw_setup_generation actual={0};int status=p->ops.read_settings(p->opaque,&config,&actual);bw_share_clear(&config);
    if(status!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&expected,&actual)||p->ops.idle(p->opaque))return -1;
    pthread_mutex_lock(&p->lock);bad=!p->active||(p->worker_kind!=WORK_UNINSTALL&&p->worker_kind!=WORK_RESTORE)||p->removing.state!=REMOVAL_RUNNING||p->removing.unknown||memcmp(&p->removing.selected,&selected,sizeof selected)||strcmp(p->external_nonce,selected.operation)||p->generation!=generation||p->play_epoch!=epoch||p->settings_revision!=revision||p->stale;
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static void removal_observe(void *opaque,const struct bw_game_uninstall_progress *progress){
    struct bw_native_backend *p=opaque;if(!progress)return;pthread_mutex_lock(&p->lock);
    if(p->active&&(p->worker_kind==WORK_UNINSTALL||p->worker_kind==WORK_RESTORE)&&p->removing.state==REMOVAL_RUNNING){
        if((unsigned)progress->phase>BW_GAME_REMOVE_UNKNOWN||progress->files_done>progress->files_total){p->removing.unknown=1;p->removing.phase=BW_GAME_REMOVE_UNKNOWN;snprintf(p->removing.message,sizeof p->removing.message,"Removal progress could not be confirmed; no request will be repeated.");}
        else{p->removing.phase=progress->phase;p->removing.files=progress->files_done;p->removing.total=progress->files_total;snprintf(p->removing.message,sizeof p->removing.message,"%s",progress->message?progress->message:"Checking the selected console copy.");}}
    pthread_mutex_unlock(&p->lock);
}
static void *removal_worker(void *opaque){
    struct bw_native_backend *p=opaque;struct bw_game_uninstall_result result={0};struct bw_native_removal_selection selected=p->removing.selected;unsigned restore=p->removing.restore,startup_only=p->removing.startup_only;int admitted=!removal_current(p),status=-1;
    if(admitted)status=restore?p->ops.restore(p->opaque,&selected,removal_current,p,&result):startup_only?p->ops.remove_startup(p->opaque,&selected,removal_current,p,removal_observe,p,&result):p->ops.uninstall(p->opaque,&selected,removal_current,p,removal_observe,p,&result);
    if(result.release_unknown)status=-1;
    if(!status&&(result.phase!=BW_GAME_REMOVE_DONE||result.registration_confirmed!=(startup_only?0u:1u)||result.preparation_only!=startup_only||result.source_absent!=1||result.prepared_record_absent!=1||result.library_removed!=(restore||startup_only?0u:1u)||result.files_removed>result.files_total||removal_current(p)))status=-1;
    struct bw_game_uninstall_capability cap={0};
    if(!status&&p->ops.removal_probe){struct bw_native_prepared absent={.state=BW_PREPARED_ABSENT};if(p->ops.removal_probe(p->opaque,&selected.identity,&absent,&cap)||(!startup_only&&cap.removed==(restore?1u:0u))||(startup_only&&(cap.removed||cap.can_restore||cap.can_remove_startup||cap.can_uninstall))||(!restore&&!startup_only&&!cap.can_restore))status=-1;}
    pthread_mutex_lock(&p->lock);p->removing.result=result;if(result.release_unknown)p->control_release_unknown=1;
    if(!status){p->removing.state=REMOVAL_DONE;p->removing.phase=BW_GAME_REMOVE_DONE;p->removing.files=result.files_removed;p->removing.total=result.files_total;
        snprintf(p->removing.message,sizeof p->removing.message,"%s",restore?"Source shown again. Send files to prepare a new console copy.":startup_only?"Startup files removed. Existing console game metadata was preserved.":"Removed from PS5. PC backup and saved data preserved.");
        if(p->library.prepared)memset(p->library.prepared+p->removing.item,0,sizeof *p->library.prepared);
        if(p->library.removal)p->library.removal[p->removing.item]=cap;
        revalidation_clear(p,selected.identity.binding.game);p->generation++;
    }else{int unknown=admitted&&(result.mutation_started||result.release_unknown||result.phase==BW_GAME_REMOVE_UNKNOWN||p->removing.unknown);p->removing.unknown=unknown;p->removing.state=unknown?REMOVAL_UNKNOWN:REMOVAL_ERROR;p->removing.phase=unknown?BW_GAME_REMOVE_UNKNOWN:BW_GAME_REMOVE_ERROR;
        snprintf(p->removing.message,sizeof p->removing.message,"%s",result.release_unknown?"Removal checks could not be released safely. Controls remain blocked; do not repeat this action.":unknown?"Removal could not be confirmed. Refresh status without submitting it again.":result.reason?result.reason:"The selected removal authority changed before mutation.");}
    p->active=0;pthread_mutex_unlock(&p->lock);return NULL;
}
static int removal_action(struct bw_native_backend *p,const char *owner,const void *body,size_t size,const char *token,unsigned purpose,struct bw_native_response *out){
    unsigned restore=purpose==1,startup_only=purpose==2;
    if(!owner||!nonce_valid(owner)||!action_authorized(p,token))return text(out,403,"{\"error\":\"Use the current confirmed Dashboard action\"}");
    if(!p->ops.removal_probe||(restore?!p->ops.restore:startup_only?!p->ops.remove_startup:!p->ops.uninstall))return text(out,409,"{\"error\":\"Exact console removal is unavailable in this build\"}");
    struct bw_native_removal_request request={0};if(bw_native_removal_request_parse(body,size,restore,&request))return text(out,400,"{\"error\":\"Choose and confirm the current exact source\"}");
    if(bw_native_backend_external_current(p,owner))return text(out,409,"{\"error\":\"The queued source or settings authority changed\"}");
    struct bw_share_config config={0};struct bw_setup_generation connection={0};int loaded=p->ops.read_settings(p->opaque,&config,&connection);int idle=p->ops.idle(p->opaque);int status=409;const char *message="{\"error\":\"Refresh the exact source before removing it\"}";char accepted[256];
    pthread_mutex_lock(&p->lock);
    if(p->active||p->removing.unknown||!strcmp(p->removing.selected.operation,owner)||p->control_release_unknown||strcmp(p->external_nonce,owner)||p->phase!=BACKEND_READY||p->stale||p->setup_blocked||p->settings_status!=BW_SETUP_VALID||request.selected.library_generation!=p->generation||request.selected.settings_revision!=p->settings_revision||loaded!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&connection,&p->settings_generation)||idle)goto done;
    size_t item=catalog_selected(p,&request.selected);
    if(item==p->library.catalog.count||!p->library.removal||!bw_source_catalog_available(p->library.catalog.items+item)||registration_waiting(p,p->library.catalog.items[item].title_id)||!(restore?p->library.removal[item].can_restore:startup_only?p->library.removal[item].can_remove_startup:p->library.removal[item].can_uninstall))goto done;
    struct bw_native_removal_selection selected={0};const struct bw_catalog_item *entry=p->library.catalog.items+item;
    if((!restore&&strcmp(request.confirmed_title,entry->title_id))||bw_source_catalog_binding(&selected.identity,&config,entry))goto done;
    if(p->library.prepared)selected.prepared=p->library.prepared[item];if(!restore&&selected.prepared.state!=BW_PREPARED_VALID)goto done;
    selected.connection=connection;selected.startup_only=startup_only;memcpy(selected.operation,owner,33);if(!restore)memcpy(selected.confirmed_title,request.confirmed_title,10);
    if(p->joinable){if(pthread_join(p->worker,NULL)){status=503;goto done;}p->joinable=0;}
    memset(&p->removing,0,sizeof p->removing);p->removing.selected=selected;p->removing.item=item;p->removing.generation=p->generation;p->removing.epoch=p->play_epoch;p->removing.revision=p->settings_revision;p->removing.restore=restore;p->removing.startup_only=startup_only;p->removing.state=REMOVAL_RUNNING;p->removing.phase=restore?BW_GAME_REMOVE_COMMITTING:BW_GAME_REMOVE_CHECKING;
    snprintf(p->removing.message,sizeof p->removing.message,"%s",restore?"Restoring this source to the library.":"Checking the selected console copy before removal.");p->worker_kind=restore?WORK_RESTORE:WORK_UNINSTALL;p->active=1;
    if(worker_start(&p->worker,removal_worker,p)){p->active=0;p->removing.state=REMOVAL_ERROR;p->removing.phase=BW_GAME_REMOVE_ERROR;snprintf(p->removing.message,sizeof p->removing.message,"The removal worker could not start.");status=503;goto done;}
    p->joinable=1;status=202;snprintf(accepted,sizeof accepted,"{\"ok\":true,\"operationId\":\"%s\",\"message\":\"%s\"}",owner,restore?"Restoring the selected source visibility":startup_only?"Removing only the exact owned startup copy":"Removing the exact selected PS5 copy");message=accepted;
done:pthread_mutex_unlock(&p->lock);bw_share_clear(&config);bw_native_sensitive_clear(&request,sizeof request);return text(out,status,message);
}
#endif
