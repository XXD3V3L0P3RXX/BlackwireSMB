/* SPDX-License-Identifier: GPL-3.0-or-later
 * Included only by native_backend.c. One joined external-start owner, separate
 * from normal Sony launch/reader runtime. Never accepted from a LAN/browser. */
#include <errno.h>
#include <float.h>
#include <math.h>

struct home_worker {
    struct bw_native_backend *backend;
    struct bw_native_play_selection selected;
    struct bw_home_claim claim;
    struct bw_stop_identity birth;
    struct bw_session_ops process;
    struct bw_home_session session;
    struct bw_native_launch_admission *startup;
    uint64_t generation,epoch;uint32_t revision;
    double deadline,began;
    unsigned resolved;
};
static void home_phase(struct home_worker *h,unsigned phase,const char *message){
    double at=now();printf("[BWHOME1] phase=%u pid=%d elapsedMs=%.0f\n",phase,h->birth.pid,at>=h->began?(at-h->began)*1000:0);
    pthread_mutex_lock(&h->backend->lock);snprintf(h->backend->playing.message,sizeof h->backend->playing.message,"%s",message);pthread_mutex_unlock(&h->backend->lock);
}
static int home_lease(void *opaque){
    struct home_worker *h=opaque;struct bw_native_backend *p=h->backend;double at=now();
    if(at<0||at>=h->deadline)return -1;
    pthread_mutex_lock(&p->lock);
    int bad=!p->active||p->worker_kind!=WORK_PLAY||!p->home.active||!p->playing.home_adopted||p->generation!=h->generation||p->play_epoch!=h->epoch||
        p->settings_revision!=h->revision||p->stale||p->playing.changed||p->playing.close_requested||
        (p->home.claim.pid!=h->claim.pid||p->home.claim.deadline_us!=h->claim.deadline_us||memcmp(p->home.claim.nonce,h->claim.nonce,33)||memcmp(p->home.claim.map_sha256,h->claim.map_sha256,32))||bw_stop_identity_match(&p->home.birth,&h->birth)!=1||
        (h->resolved&&memcmp(&p->playing.selection,&h->selected,sizeof h->selected));
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static int home_app_current(void *opaque,const struct bw_native_session *game){(void)opaque;return bw_home_identity_current(game);}
static int home_consumer_uid(void *opaque,pid_t pid,uid_t *out){(void)opaque;return bw_home_identity_uid(pid,out);}
static int home_cleanup_lease(void *opaque){
    struct home_worker *h=opaque;struct bw_native_backend *p=h->backend;
    pthread_mutex_lock(&p->lock);int bad=!p->active||p->worker_kind!=WORK_PLAY||!p->home.active||p->play_epoch!=h->epoch||p->home.retained!=h;
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static int home_idle_after_exit(void *opaque){
    struct home_worker *h=opaque;
    /* Cleanup has its own bounded observations, not an expired startup lease.
     * Exclude a newly started different game before unlinking old inputs. */
    struct bw_startup_idle_request idle={.deadline=now()+2,.current=home_cleanup_lease,.opaque=h,.game_log=backend_game_log};
    return home_cleanup_lease(h)||h->backend->ops.idle(h->backend->opaque)||bw_startup_idle_ps5(&idle)||home_cleanup_lease(h)?-1:0;
}

static int home_settings(struct home_worker *h,struct bw_share_config *out){
    struct bw_share_config connection={0};struct bw_setup_generation actual={0};
    int bad=home_lease(h)||h->backend->ops.read_settings(h->backend->opaque,&connection,&actual)!=BW_SETUP_VALID||
        !bw_native_setup_generation_equal(&actual,&h->selected.connection)||home_lease(h);
    if(!bad&&out)*out=connection;bw_share_clear(&connection);return bad?-1:0;
}
/* The public current API requires the exact selected tuple, not NULL. */
static int home_startup_control(struct home_worker *h,const struct bw_reader_selection *selected){
    if(home_settings(h,NULL)||bw_home_session_current(&h->session))return -1;
    struct bw_startup_idle_request control={.deadline=h->deadline,.current=home_lease,.opaque=h,.game_log=backend_game_log};
    if(bw_startup_adopt_ps5(&control,&h->birth,h->session.game.title)||
       (h->startup&&bw_native_launch_admission_current(h->startup,backend_data_root,&h->selected.identity,selected,NULL)))return -1;
    return home_settings(h,NULL)||bw_home_session_current(&h->session)?-1:0;
}
static int home_source(struct home_worker *h,const struct bw_share_config *config,const struct bw_reader_selection *selected){
    struct bw_source_smb source={.deadline=h->deadline};uint8_t map[32],generation[32];const char *error=NULL;int result=-1;
    if(home_lease(h))return -1;
    source.smb=smb2_init_context();if(!source.smb)return -1;
    double remain=h->deadline-now();if(remain<=0)goto done;
    unsigned timeout=(unsigned)ceil(remain);if(timeout>10)timeout=10;if(!timeout)timeout=1;
    smb2_set_timeout(source.smb,(int)timeout);smb2_set_user(source.smb,config->user);smb2_set_password(source.smb,config->password);
    smb2_set_sign(source.smb,1);smb2_set_security_mode(source.smb,SMB2_NEGOTIATE_SIGNING_REQUIRED);
    if(smb2_connect_share(source.smb,config->server,config->share,config->user)<0||home_lease(h)||
       bw_native_launch_admission_source(h->startup,config,&source,selected,map,generation,&error)||memcmp(map,h->claim.map_sha256,32)||home_lease(h))goto done;
    result=0;
done:
    /* Source operations drain their same-worker async callbacks before return.
     * No callback or shared reader source escapes this bounded preflight. */
    if(source.smb)smb2_destroy_context(source.smb);memset(map,0,sizeof map);memset(generation,0,sizeof generation);return result;
}
static int home_select(struct home_worker *h,struct bw_share_config *config){
    struct bw_native_backend *p=h->backend;uint32_t app=0;char title[10];
    while(!home_lease(h)){
        if(!bw_home_identity_app(h->birth.pid,&app,title))break;
        struct bw_stop_identity observed;if(h->process.process(h->process.opaque,h->birth.pid,&observed)!=1||bw_stop_identity_match(&h->birth,&observed)!=1)return -1;
        struct timespec delay={0,20000000};if(nanosleep(&delay,NULL))return -1;
    }
    if(home_lease(h)||!app)return -1;
    h->session=(struct bw_home_session){.current=home_lease,.app_current=home_app_current,.consumer_uid=home_consumer_uid,.idle=home_idle_after_exit,.opaque=h,.deadline=h->deadline};
    if(bw_home_session_bind(&h->session,&h->birth,&h->process,h->selected.run,title,app))return -1;
    if(home_settings(h,config))return -1;
    size_t selected=SIZE_MAX;unsigned candidates=0;struct bw_provider_binding chosen={0};struct bw_native_prepared prepared={0};
    pthread_mutex_lock(&p->lock);size_t count=p->library.catalog.count;pthread_mutex_unlock(&p->lock);
    for(size_t i=0;i<count;i++){
        if(home_lease(h))return -1;
        struct bw_catalog_item item;struct bw_native_prepared candidate={0};int eligible=0;
        pthread_mutex_lock(&p->lock);item=p->library.catalog.items[i];
        if(p->library.prepared)candidate=p->library.prepared[i];
        eligible=!strcmp(item.title_id,title)&&bw_source_catalog_available(&item)&&
            p->library.prepared&&candidate.state==BW_PREPARED_VALID&&!candidate.prepared_policy&&!candidate.startup_upgrade_required&&
            !reader_update_reason(&candidate)&&!revalidation_required(p,&candidate)&&!removed_at(p,i)&&!registration_waiting(p,title);
        pthread_mutex_unlock(&p->lock);if(!eligible)continue;
        struct bw_provider_binding identity={0};struct bw_reader_selection registered={0};
        /* Two owned backups may share a title. Select by the exact real
         * registration/root before counting, not title alone. This bounded
         * metadata selection does not grant LOCAL/source or map-SHA admission;
         * the retained owner below independently proves those once. */
        if(bw_provider_binding_init(&identity,config,item.source,title,bw_source_catalog_kind(&item))||
           bw_native_launch_selection(backend_data_root,&identity,&candidate,h->selected.run,&registered)||
           bw_native_registration_verify(backend_data_root,"/user/app","/system_data/priv/mms/app.db",&registered))continue;
        selected=i;chosen=identity;prepared=candidate;candidates++;
    }
    if(candidates!=1||home_lease(h))return -1;
    pthread_mutex_lock(&p->lock);
    int bad=p->settings_revision!=h->revision||p->generation!=h->generation||p->play_epoch!=h->epoch||!p->active||!p->home.active;
    if(!bad){h->selected.identity=chosen;h->selected.prepared=prepared;p->playing.item=selected;p->playing.selection=h->selected;p->playing.pid=h->birth.pid;h->resolved=1;}
    pthread_mutex_unlock(&p->lock);return bad?-1:home_lease(h);
}
static void home_consumed_retire_locked(struct bw_native_backend *p,const struct bw_stop_identity *birth){
    for(size_t i=0;i<p->home_consumed_count;i++)if(bw_stop_identity_match(&p->home_consumed[i],birth)==1){
        p->home_consumed[i]=p->home_consumed[--p->home_consumed_count];return;
    }
}
static void *home_worker(void *opaque){
    struct bw_native_backend *p=opaque;struct home_worker *h=p->home.retained;
    if(!h)return NULL;h->backend=p;
    pthread_mutex_lock(&p->lock);h->claim=p->home.claim;h->birth=p->home.birth;h->generation=p->home.generation;h->epoch=p->home.epoch;h->revision=p->home.revision;h->selected=p->playing.selection;pthread_mutex_unlock(&p->lock);
    h->deadline=(double)h->claim.deadline_us/1000000.0;h->began=now();bw_native_session_ps5_ops(&h->process);
    struct bw_share_config config={0};struct bw_reader_selection selected={0};uint8_t map[32];const char *failure=NULL;int result=-1,release_unknown=0;
    home_phase(h,1,"Connecting the game started from the PS5 dashboard.");
    if(home_select(h,&config)){failure="The game could not be matched to exactly one prepared Blackwire source.";goto track;}
    home_phase(h,2,"Checking the exact PS5 dashboard game and prepared source.");
    if(home_startup_control(h,NULL)||bw_native_launch_admission_open(&h->startup,backend_data_root,&h->selected.identity,&h->selected.prepared,h->selected.run,home_lease,h,&failure)||
       bw_native_launch_admission_selection(h->startup,&selected)||bw_native_launch_admission_map_sha256(h->startup,map)||memcmp(map,h->claim.map_sha256,32)){
        if(!failure)failure="The running game's file map differs from its prepared receipt.";goto track;
    }
    home_phase(h,3,"Checking the prepared game reader.");
    char root[BW_NRC_PATH_BYTES];int n=snprintf(root,sizeof root,"%s/games/%s",backend_data_root,selected.game);
    struct bw_preload_bundle_request bundle={root,home_lease,h,h->deadline};struct bw_preload_bundle_result checked={0};
    if(n<0||(size_t)n>=sizeof root||bw_preload_bundle_verify_checked(&bundle,&checked)){
        release_unknown|=checked.release_unknown;failure="The dashboard reader needs setup. Start this game once from Blackwire Play; its startup files will be kept.";goto track;
    }
    home_phase(h,4,"Checking the current PC share for this game.");
    if(home_source(h,&config,&selected)){failure="The current PC source could not be verified for this dashboard game.";goto track;}
    home_phase(h,5,"Checking the game's existing dashboard registration.");
    if(home_startup_control(h,&selected)||bw_native_registration_verify(backend_data_root,"/user/app","/system_data/priv/mms/app.db",&selected)||home_startup_control(h,&selected)){
        failure="The PS5 dashboard registration no longer matches this prepared source.";goto track;
    }
    home_phase(h,6,"Starting the reader for the PS5 dashboard game.");
    if(bw_home_session_publish(&h->session,root,&selected,&config)||home_startup_control(h,&selected)){
        failure="The game's private startup inputs could not be assigned safely.";goto track;
    }
    result=0;home_phase(h,7,"PS5 dashboard game connected. Reader startup inputs delivered.");
track:
    bw_share_clear(&config);memset(map,0,sizeof map);
    if(bw_native_launch_admission_close(&h->startup))release_unknown=1;
    pthread_mutex_lock(&p->lock);
    if(failure){snprintf(p->playing.phase,sizeof p->playing.phase,"unknown");snprintf(p->playing.message,sizeof p->playing.message,"%s",failure);play_failure_locked(p,failure);}
    else{strcpy(p->playing.phase,"running");p->playing.launch_seen=1;p->playing.reader_started=1;if(p->playing.item<BW_CATALOG_MAX_ITEMS)p->home_ready[p->playing.item]=1;}
    pthread_mutex_unlock(&p->lock);
    /* There was already a game before this worker. A failed proof cannot
     * release its reservation until independent exact-PID absence. */
    for(;;){
        struct bw_stop_identity observed;int present=h->process.process(h->process.opaque,h->birth.pid,&observed);
        if(present==0&&h->process.process(h->process.opaque,h->birth.pid,&observed)==0)break;
        int exact=present==1&&bw_stop_identity_match(&h->birth,&observed)==1;
        pthread_mutex_lock(&p->lock);int close_requested=p->playing.close_requested;pthread_mutex_unlock(&p->lock);
        if(exact&&close_requested&&h->session.game.bound&&!h->session.game.close_called)(void)bw_home_session_close(&h->session,h->selected.run);
        struct timespec pause={0,100000000};(void)nanosleep(&pause,NULL);
    }
    home_phase(h,8,"Game exited. Releasing its private reader inputs.");
    if(h->session.game.bound){
        double ceiling=now()+30;
        while(bw_home_session_exited(&h->session)&&now()<ceiling){struct timespec pause={0,100000000};(void)nanosleep(&pause,NULL);}
        if(bw_home_session_release(&h->session,ceiling))release_unknown=1;
    }else if(h->session.private_inputs.initialized)release_unknown=1;
    pthread_mutex_lock(&p->lock);
    if(release_unknown)p->control_release_unknown=1;
    p->playing.pid=0;p->playing.finished=now();p->playing.timing_finished=1;
    snprintf(p->playing.phase,sizeof p->playing.phase,"%s",release_unknown?"unknown":result?"error":"stopped");
    if(release_unknown)snprintf(p->playing.message,sizeof p->playing.message,"The game's private inputs could not be released safely; no operation will be repeated.");
    else if(!result)snprintf(p->playing.message,sizeof p->playing.message,"Game exited. Its dashboard icon is ready for the next launch.");
    if(!release_unknown){
        home_consumed_retire_locked(p,&h->birth);
        p->home.retained=NULL;
    }
    p->home.active=0;p->active=0;pthread_mutex_unlock(&p->lock);if(!release_unknown)free(h);return NULL;
}
static int home_claim_deadline_current(const struct bw_home_claim *claim){
    double at=now();
    return !__builtin_isfinite(at)||at<0||(double)claim->deadline_us/1000000.0<=at?-1:0;
}
int bw_native_backend_home_claim(struct bw_native_backend *p,const void *body,size_t size,int loopback,int header,int origin,struct bw_native_response *out){
    if(!p||!out)return -1;
    if(!loopback||!header||origin)return text(out,403,"{\"error\":\"Native loopback reader startup required\"}");
    struct bw_home_claim claim;double at=now();if(bw_home_claim_parse(&claim,body,size)||!__builtin_isfinite(at)||at<0||
       (double)claim.deadline_us/1000000.0<=at||(double)claim.deadline_us/1000000.0>at+20.0)return text(out,400,"{\"error\":\"Invalid or expired reader startup request\"}");
    struct bw_session_ops process;bw_native_session_ps5_ops(&process);struct bw_stop_identity first,last;
    if(process.process(process.opaque,claim.pid,&first)!=1||strncmp(first.role,"eboot",5)||bw_stop_identity_admit(&first,process.wall_us(process.opaque))||
       process.process(process.opaque,claim.pid,&last)!=1||bw_stop_identity_match(&first,&last)!=1)return text(out,409,"{\"error\":\"Reader game identity unavailable\"}");
    if(pthread_mutex_lock(&p->dispatch_lock))return text(out,503,"{\"error\":\"Reader controller unavailable\"}");
    pthread_mutex_lock(&p->lock);
    int status=409;const char *reply="{\"error\":\"Blackwire is busy or needs setup\"}";char ack[BW_HOME_REQUEST_LIMIT];size_t used=0;
    struct home_worker *retained=NULL;
    unsigned char prunable[256]={0};size_t prunable_count=0;
    /* The same reader deadline includes waiting for either dispatch gate.
     * Historical pruning is committed only with a live accepted startup. */
    if(home_claim_deadline_current(&claim))goto expired;
    if(!p->active&&!p->home.retained){
        for(size_t i=0;i<p->home_consumed_count;i++){
            struct bw_stop_identity observed;
            int absent=process.process(process.opaque,p->home_consumed[i].pid,&observed)==0&&process.process(process.opaque,p->home_consumed[i].pid,&observed)==0;
            if(home_claim_deadline_current(&claim))goto expired;
            if(absent){prunable[i]=1;prunable_count++;}
        }
    }
    if(p->ops.play!=play||p->active||*p->external_nonce||p->control_release_unknown||p->phase!=BACKEND_READY||p->stale||p->setup_blocked||
       !p->settings_enabled||p->settings_status!=BW_SETUP_VALID||p->home_consumed_count-prunable_count==256||p->play_epoch==UINT64_MAX)goto done;
    for(size_t i=0;i<p->home_consumed_count;i++)if(!prunable[i]&&bw_stop_identity_match(&p->home_consumed[i],&first)==1){reply="{\"error\":\"This exact game startup was already claimed\"}";goto done;}
    if(p->joinable){int joined=pthread_join(p->worker,NULL);if(!joined)p->joinable=0;
        if(home_claim_deadline_current(&claim))goto expired;if(joined){status=503;goto done;}}
    if(home_claim_deadline_current(&claim))goto expired;
    char run[33];int nonce_result=p->ops.nonce(p->opaque,run);
    if(home_claim_deadline_current(&claim))goto expired;
    if(nonce_result||!nonce_valid(run)||!strcmp(run,p->playing.selection.run)||bw_home_ack_format(claim.nonce,run,ack,sizeof ack,&used)){status=503;goto done;}
    if(home_claim_deadline_current(&claim))goto expired;
    retained=calloc(1,sizeof *retained);
    if(home_claim_deadline_current(&claim))goto expired;
    if(!retained){status=503;goto done;}
    /* No blocking operation separates this final deadline fence from commit. */
    for(size_t i=p->home_consumed_count;i>0;i--)if(prunable[i-1])p->home_consumed[i-1]=p->home_consumed[--p->home_consumed_count];
    p->home_consumed[p->home_consumed_count++]=first;++p->play_epoch;
    memset(&p->playing,0,sizeof p->playing);memcpy(p->playing.selection.run,run,33);p->playing.selection.connection=p->settings_generation;
    p->playing.home_adopted=1;p->playing.pid=claim.pid;p->playing.started=at;p->playing.timing_started=1;strcpy(p->playing.phase,"preflight");
    p->home.claim=claim;p->home.birth=first;p->home.generation=p->generation;p->home.epoch=p->play_epoch;p->home.revision=p->settings_revision;p->home.active=1;p->home.retained=retained;
    p->worker_kind=WORK_PLAY;p->active=1;memset(&p->last_failure,0,sizeof p->last_failure);
    if(worker_start(&p->worker,home_worker,p)){p->active=0;p->home.active=0;p->home.retained=NULL;free(retained);status=503;reply="{\"error\":\"Reader startup worker unavailable\"}";goto done;}
    p->joinable=1;status=202;reply=ack;
    goto done;
expired:
    free(retained);status=400;reply="{\"error\":\"Invalid or expired reader startup request\"}";
done:
    pthread_mutex_unlock(&p->lock);pthread_mutex_unlock(&p->dispatch_lock);memset(&claim,0,sizeof claim);return text(out,status,reply);
}

/* First preparation provisions the SAME five-library reader and commits the
 * normal real-game registration. This never launches, recopies game data, or
 * creates a replacement/alias application. Already-Ready games also use normal
 * Play's existing bounded upgrade before their first direct dashboard start. */
struct home_prepare {struct bw_native_backend *p;struct bw_provider_binding identity;struct bw_native_prepared prepared;struct bw_reader_selection selection;struct bw_native_launch_admission *startup;uint64_t generation;uint32_t revision;double deadline;unsigned startup_invalid;};
static int home_prepare_lease(void *opaque){
    struct home_prepare *h=opaque;struct bw_native_backend *p=h->p;double t=now();if(t<0||t>=h->deadline)return -1;
    pthread_mutex_lock(&p->lock);int bad=!p->active||p->worker_kind!=WORK_PREPARE||p->control_release_unknown||p->generation!=h->generation||p->settings_revision!=h->revision||p->preparation_changed||
        memcmp(&p->sending.selection.identity,&h->identity,sizeof h->identity);pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static int home_prepare_control(void *opaque){
    struct home_prepare *h=opaque;
    if(home_prepare_lease(h)||prepare_current(h->p)||prepare_authority(h->p,BW_PREPARE_CONTROL))return -1;
    if(h->startup&&bw_native_launch_admission_current(h->startup,backend_data_root,&h->identity,&h->selection,NULL)){h->startup_invalid=1;return -1;}
    return home_prepare_lease(h)?-1:0;
}
static int home_prepare_registration(void *opaque,enum bw_nrl_control point){return point==BW_NRL_REGISTER?home_prepare_control(opaque):-1;}
struct home_prepare_failure {unsigned step;const char *where,*detail;int raw,error;};
static void home_prepare_first_failure(struct home_prepare_failure *f,unsigned step,const char *where,const char *detail,int raw,int error){
    if(!f->step)*f=(struct home_prepare_failure){step,where,detail,raw,error};
}
static int home_prepare_provision(struct bw_native_backend *p,const struct bw_native_prepared *prepared,unsigned *startup_invalid){
    if(startup_invalid)*startup_invalid=0;
    if(!prepared||prepared->state!=BW_PREPARED_VALID||prepared->prepared_policy||
       (p->sending.selection.identity.kind!=BW_PROVIDER_FOLDER&&p->sending.selection.identity.kind!=BW_PROVIDER_CONTAINER)){if(startup_invalid)*startup_invalid=1;return -1;}
    struct home_prepare h={.p=p,.identity=p->sending.selection.identity,.prepared=*prepared,.deadline=now()+120};
    pthread_mutex_lock(&p->lock);h.generation=p->generation;h.revision=p->settings_revision;pthread_mutex_unlock(&p->lock);
    char run[33];const char *failure=NULL;struct bw_nrl_owner *owner=NULL;int result=-1,unknown=0,step_result=0;
    struct home_prepare_failure first={0};
    step_result=p->ops.nonce(p->opaque,run);
    if(step_result||!nonce_valid(run)||!strcmp(run,prepared->run)){
        home_prepare_first_failure(&first,2,"nonce","fresh setup identity unavailable",step_result,step_result?errno:0);goto done;}
    if((step_result=home_prepare_control(&h))){home_prepare_first_failure(&first,3,"initial-control","current setup authority refused",step_result,errno);goto done;}
    step_result=bw_native_launch_admission_open(&h.startup,backend_data_root,&h.identity,prepared,run,home_prepare_lease,&h,&failure);
    if(step_result){h.startup_invalid=1;home_prepare_first_failure(&first,4,"startup-admission",failure?failure:"startup admission refused",step_result,errno);goto done;}
    if((step_result=bw_native_launch_admission_selection(h.startup,&h.selection))){h.startup_invalid=1;home_prepare_first_failure(&first,5,"startup-selection","held startup selection unavailable",step_result,errno);goto done;}
    char root[BW_NRC_PATH_BYTES];int n=snprintf(root,sizeof root,"%s/games/%s",backend_data_root,h.selection.game);
    struct bw_preload_bundle_upgrade_request upgrade={.bundle={root,home_prepare_lease,&h,now()+20},.data_root=backend_data_root,.game=h.selection.game,.prepared_run=prepared->run,.launch_run=run,.prepared_record_sha256=prepared->record_sha256};
    struct bw_preload_bundle_control split={home_prepare_control,&h};struct bw_preload_bundle_result bundled={0};
    if(n<0||(size_t)n>=sizeof root){home_prepare_first_failure(&first,6,"source-root","selected source path does not fit",n,0);goto done;}
    if((step_result=bw_preload_bundle_upgrade_controlled(&upgrade,&split,&bundled))){unknown|=bundled.release_unknown;
        home_prepare_first_failure(&first,7,bundled.stage?bundled.stage:"reader-bundle",bundled.reason?bundled.reason:"reader bundle refused",step_result,bundled.system_error);goto done;}
    struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;struct bw_nrl_request acquire={.environment=env,.selection=&h.selection,.current=home_prepare_registration,.opaque=&h};struct bw_nrc_result acquired={0};
    int began=bw_nrl_begin(&acquire,&owner,&acquired);if(began){unknown|=began==BW_NRC_RELEASE_UNCONFIRMED;
        home_prepare_first_failure(&first,8,acquired.stage?acquired.stage:"registration-owner",acquired.detail?acquired.detail:acquired.reason?acquired.reason:"registration owner admission refused",began,acquired.error_snapshot);goto done;}
    struct bw_native_register_request registration={.data_root=backend_data_root,.apps=env.apps_root,.database=env.database,.identity=&h.identity,.selected=&h.selection,.current=home_prepare_control,.guard=&h,.startup=h.startup};
    struct bw_register_result checked={0};int registered=bw_native_register(&registration,&checked);
    if(registered)home_prepare_first_failure(&first,9,"registration",checked.message?checked.message:"registration refused",registered,errno);
    struct bw_nrc_result committed={0};int committed_status=bw_nrl_registered(owner,registered,&checked,&committed);
    if(committed_status)home_prepare_first_failure(&first,10,committed.stage?committed.stage:"registration-commit",committed.detail?committed.detail:committed.reason?committed.reason:"ownership commit refused",committed_status,committed.error_snapshot);
    unknown|=committed_status==BW_NRC_RELEASE_UNCONFIRMED;
    if(registered||committed_status)goto done;
    if((step_result=home_prepare_control(&h))){home_prepare_first_failure(&first,11,"final-control","current setup authority refused",step_result,errno);goto done;}result=0;
done:;
    int registration_closed=bw_nrl_destroy(&owner),registration_error=errno;
    int startup_closed=bw_native_launch_admission_close(&h.startup),startup_error=errno;
    if(registration_closed)home_prepare_first_failure(&first,12,"registration-release","registration descriptor release unconfirmed",registration_closed,registration_error);
    if(startup_closed)home_prepare_first_failure(&first,13,"startup-release","startup descriptor release unconfirmed",startup_closed,startup_error);
    if(registration_closed||startup_closed)unknown=1;
    if(unknown)result=-1;
    (void)registration_pending_refresh(p);
    pthread_mutex_lock(&p->lock);if(unknown)p->control_release_unknown=1;
    if(p->sending.item<BW_CATALOG_MAX_ITEMS)p->home_ready[p->sending.item]=result?2:1;
    if(result){snprintf(p->home_setup_game,sizeof p->home_setup_game,"%s",prepared->game);
        snprintf(p->home_setup_reason,sizeof p->home_setup_reason,"Startup files kept. Setup step %u (%.48s): %.128s (raw=%d errnoSnapshot=%d).",
            first.step,first.where?first.where:"unknown",first.detail?first.detail:"setup did not finish",first.raw,first.error);}
    else if(!strcmp(p->home_setup_game,prepared->game)){memset(p->home_setup_game,0,sizeof p->home_setup_game);memset(p->home_setup_reason,0,sizeof p->home_setup_reason);}
    pthread_mutex_unlock(&p->lock);if(startup_invalid)*startup_invalid=h.startup_invalid;return result;
}

/* Explicit scan only: no provisioner, upgrade, ADD, scan request or launch.
 * Freshly bind this receipt/settings generation to the current registered
 * source, stable manual membership and exact already-qualified reader set. */
struct home_observation {struct bw_native_backend *p;struct bw_setup_generation settings;uint64_t generation;uint32_t revision;int worker_kind;double deadline;};
static int home_observation_current(void *opaque){
    struct home_observation *o=opaque;struct bw_native_backend *p=o->p;double at=now();if(at<0||at>=o->deadline)return -1;
    pthread_mutex_lock(&p->lock);int bad=!p->active||p->control_release_unknown||p->worker_kind!=o->worker_kind||
        (o->worker_kind!=WORK_SCAN&&o->worker_kind!=WORK_SETUP)||p->generation!=o->generation||p->settings_revision!=o->revision;
    pthread_mutex_unlock(&p->lock);if(bad)return -1;
    struct bw_share_config config={0};struct bw_setup_generation actual={0};
    bad=p->ops.read_settings(p->opaque,&config,&actual)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&actual,&o->settings);
    bw_share_clear(&config);return bad?-1:0;
}
static int home_ready_observe(struct bw_native_backend *p,const struct bw_provider_binding *identity,
    const struct bw_native_prepared *prepared,const struct bw_setup_generation *settings){
    if(!identity||!prepared||!settings||(identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER)||prepared->state!=BW_PREPARED_VALID||prepared->prepared_policy||prepared->startup_upgrade_required)return -1;
    pthread_mutex_lock(&p->lock);int refused=revalidation_required(p,prepared);pthread_mutex_unlock(&p->lock);
    if(refused||bw_preload_bundle_upgrade_workspace(backend_data_root,prepared->game)!=0)return -1;
    struct home_observation o={.p=p,.settings=*settings,.deadline=now()+20};
    pthread_mutex_lock(&p->lock);o.generation=p->generation;o.revision=p->settings_revision;o.worker_kind=p->worker_kind;pthread_mutex_unlock(&p->lock);
    struct bw_reader_selection first={0},last={0};char root[BW_NRC_PATH_BYTES];
    if(home_observation_current(&o)||bw_native_launch_selection(backend_data_root,identity,prepared,prepared->run,&first))return -1;
    int n=snprintf(root,sizeof root,"%s/games/%s",backend_data_root,first.game);if(n<0||(size_t)n>=sizeof root)return -1;
    struct bw_preload_bundle_request bundle={root,home_observation_current,&o,o.deadline};struct bw_preload_bundle_result checked={0};
    if(bw_preload_bundle_verify_checked(&bundle,&checked)||home_observation_current(&o)){
        if(checked.release_unknown){pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);}return -1;
    }
    struct bw_sm_connection connection={0,home_observation_current,&o};struct bw_sm_reply manual[2]={{0},{0}};
    for(unsigned i=0;i<2;i++)if(home_observation_current(&o)||
        bw_native_registration_state(backend_data_root,registration_environment.apps_root,registration_environment.database,&first)!=BW_REG_MATCH||
        bw_sm_post(&connection,BW_SM_MANUAL_LIST,root,manual+i)||manual[i].present!=1||home_observation_current(&o))return -1;
    if(manual[0].count!=manual[1].count||bw_native_launch_selection(backend_data_root,identity,prepared,prepared->run,&last)||
       memcmp(&first,&last,sizeof first)||home_observation_current(&o))return -1;
    return 0;
}

