#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_backend.h"
#ifdef BW_HOME_STARTUP
#include "home_protocol.h"
#include "home_session.h"
#include "home_identity_ps5.h"
#endif
#include "launch_timing.h"
#include "native_json.h"
#include "native_removal_request.h"
#ifdef BW_NATIVE_PRELOAD
#include "native_game_uninstall_remote.h"
#endif
#include "native_derived_context.h"
#include "native_batch_context.h"
#include "native_prepared_target.h"
#include "startup_runtime_control.h"
#include "startup_runtime_idle.h"
#ifdef BW_NATIVE_PRELOAD
#include "idle_log_cache.h"
#endif
#include "../streamreader/copy_sha256.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#ifdef BW_NATIVE_PLAYBACK
#include "native_launch.h"
#include "native_launch_admission.h"
#include "native_registration.h"
#include "native_register.h"
#include "native_owned_registration.h"
#include "native_registration_lifecycle.h"
#include "native_registration_removed.h"
#include "native_cleanup_remote.h"
#ifdef BW_NATIVE_PRELOAD
#include "preload_bundle.h"
#endif
#endif
#ifdef BW_NATIVE_BACKEND_HOST_TEST
static const char *backend_data_root="/data/blackwire",*backend_game_log="/data/shadowmount/debug.log";
void bw_native_backend_host_paths(const char *root,const char *log){backend_data_root=root;backend_game_log=log;}
#else
static const char *const backend_data_root="/data/blackwire",*const backend_game_log="/data/shadowmount/debug.log";
#endif
struct bw_native_backend {
    pthread_mutex_t lock,dispatch_lock;char external_nonce[33];pthread_t worker;int active,joinable,phase,stale,worker_kind;uint64_t generation,play_epoch;
    struct {unsigned valid;uint64_t generation,play_epoch;uint32_t revision,setup_operation;int settings_status,settings_known;struct bw_setup_generation settings;char sending[33],playing[33];} external_expected;
    int control_release_unknown;
#ifdef BW_HOME_STARTUP
    struct {struct bw_home_claim claim;struct bw_stop_identity birth;uint64_t generation,epoch;uint32_t revision;unsigned active;void *retained;} home;
    struct bw_stop_identity home_consumed[256];size_t home_consumed_count;
    unsigned char home_ready[BW_CATALOG_MAX_ITEMS]; /*0 unknown,1 qualified,2 known setup failure*/
    char home_setup_game[75],home_setup_reason[256];
#endif
    char server[128],share[128],share2[128],user[128];unsigned password_saved;struct bw_native_backend_ops ops;void *opaque;
    struct bw_native_library library;struct bw_native_jobs *jobs;
    int (*settings_idle)(void *,uint32_t);void *settings_idle_owner;
    int (*preparation_idle)(void *,const char[33],const char[33],enum bw_prepare_guard_point);void *preparation_idle_owner;
    int settings_enabled,settings_known,settings_status,setup_phase,setup_error,setup_blocked,setup_checked_only;
    uint32_t settings_revision,setup_operation;char setup_token[33];struct bw_setup_generation settings_generation;
    struct {struct bw_share_config config;struct bw_setup_generation expected;char nonce[33];int unchanged;} pending;
    struct {struct bw_share_config config;struct bw_setup_generation expected;struct bw_native_prepare_selection selection;struct bw_native_job_token token;size_t item;char authority[33];
        struct bw_native_prepared retained;unsigned setup_only;} sending;
    int preparation_blocked,preparation_changed;
    struct {struct bw_native_play_selection selection;size_t item;int home_adopted,changed,close_requested,prepare_needed,reader_started,launch_seen,preflight_failed,preflight_control_verified,retiring,cleanup_attention;
        struct bw_share_config prepare_config;struct bw_native_prepare_selection prepare_selection;struct bw_native_job_token prepare_token;
        char phase[24],message[256];
        /* Immutable accepted dispatch identity, owned by this joined worker.
         * The short outer reservation may be released after the 202 ACK. */
        char dispatch[33];
#ifdef BW_LAUNCH_TIMING
        char operation[33];
#endif
        int32_t pid;uint64_t network_bytes,hit_bytes,high_water;double started,finished;int timing_started,timing_finished;} playing;
    /* Reporting only: neither this record nor a terminal message releases an
     * owned worker. Keep the first failure until another Play is accepted. */
    struct {char run[33],game[BW_MANIFEST_MAX_PATH+1],message[256];} last_failure;
    /* Resident-only negative observations, independent of catalog rows.
     * Refresh cannot erase a known failure of this exact durable record. */
    struct {char game[75];uint8_t record[32];} revalidation[BW_CATALOG_MAX_ITEMS];
    size_t revalidation_count;
    /* Durable pending registration operations are separate from Ready receipt
     * validity. They block only their exact title; Send is not a cleanup retry. */
    struct {char title[10],game[75],run[33];unsigned retiring,removed;} registration_pending[256];
    size_t registration_pending_count;
    int registration_ledger_unknown;
    struct {struct bw_native_removal_selection selected;size_t item;uint64_t generation,epoch;uint32_t revision;
        unsigned restore,state,unknown;enum bw_game_uninstall_phase phase;uint32_t files,total;char message[256];
        struct bw_game_uninstall_result result;} removing;
};
static int registration_waiting(const struct bw_native_backend *p,const char *title){
    if(p->registration_ledger_unknown)return 1;
    for(size_t i=0;i<p->registration_pending_count;i++)if(!strcmp(p->registration_pending[i].title,title))return 1;
    return 0;
}
enum {BACKEND_INITIAL,BACKEND_SCANNING,BACKEND_READY,BACKEND_NEEDS_SETUP,BACKEND_FAILED,BACKEND_CONFIG_CHANGED};
enum {WORK_SCAN,WORK_SETUP,WORK_PREPARE,WORK_PLAY,WORK_UNINSTALL,WORK_RESTORE};
enum {REMOVAL_IDLE,REMOVAL_RUNNING,REMOVAL_DONE,REMOVAL_ERROR,REMOVAL_UNKNOWN};
static int default_removal_probe(void *,const struct bw_provider_binding *,const struct bw_native_prepared *,struct bw_game_uninstall_capability *);
static int default_uninstall(void *,const struct bw_native_removal_selection *,int (*)(void *),void *,void (*)(void *,const struct bw_game_uninstall_progress *),void *,struct bw_game_uninstall_result *);
static int default_restore(void *,const struct bw_native_removal_selection *,int (*)(void *),void *,struct bw_game_uninstall_result *);
static void removal_state_json(struct bw_json *,const struct bw_native_backend *);
static int removed_at(const struct bw_native_backend *p,size_t i){return p->library.removal&&i<p->library.catalog.count&&p->library.removal[i].removed;}
static int removal_cache(struct bw_native_backend *,struct bw_native_library *,const struct bw_share_config *);
static int revalidation_known(const struct bw_native_backend *p,const char *game){
    for(size_t i=0;i<p->revalidation_count;i++)if(!strcmp(p->revalidation[i].game,game))return 1;
    return 0;
}
static int revalidation_capacity(const struct bw_native_backend *p,const char *game){
    return p->revalidation_count<BW_CATALOG_MAX_ITEMS||revalidation_known(p,game);
}
static int revalidation_required(const struct bw_native_backend *p,const struct bw_native_prepared *prepared){
    if(!prepared||prepared->state!=BW_PREPARED_VALID||!memchr(prepared->game,0,sizeof prepared->game))return 0;
    for(size_t i=0;i<p->revalidation_count;i++)if(!strcmp(p->revalidation[i].game,prepared->game)&&
       !memcmp(p->revalidation[i].record,prepared->record_sha256,32))return 1;
    return 0;
}
static int revalidation_remember(struct bw_native_backend *p,const struct bw_native_play_selection *selected){
    const struct bw_native_prepared *prepared=&selected->prepared;
    if(prepared->state!=BW_PREPARED_VALID||!memchr(prepared->game,0,sizeof prepared->game)||
       strcmp(prepared->game,selected->identity.binding.game))return -1;
    size_t at=0;for(;at<p->revalidation_count;at++)if(!strcmp(p->revalidation[at].game,prepared->game))break;
    if(at==p->revalidation_count){if(at==BW_CATALOG_MAX_ITEMS)return -1;p->revalidation_count++;}
    memcpy(p->revalidation[at].game,prepared->game,sizeof prepared->game);
    memcpy(p->revalidation[at].record,prepared->record_sha256,32);return 0;
}
static void revalidation_clear(struct bw_native_backend *p,const char *game){
    for(size_t i=0;i<p->revalidation_count;i++)if(!strcmp(p->revalidation[i].game,game)){
        p->revalidation_count--;p->revalidation[i]=p->revalidation[p->revalidation_count];
        memset(&p->revalidation[p->revalidation_count],0,sizeof p->revalidation[0]);return;
    }
}

static int action_authorized(const struct bw_native_backend *,const char *);
static void *prepare_worker(void *);
static void *play_worker(void *);
static int prepare_current(void *);
static int prepare_authority(struct bw_native_backend *,enum bw_prepare_guard_point);
static int prepare_idle(void *opaque){return prepare_authority(opaque,BW_PREPARE_IO);}
static int prepare_publication(void *opaque){return prepare_authority(opaque,BW_PREPARE_CONTROL);}
static int start_prepare_current(void *);
static int play_current(void *);
#ifdef BW_HOME_STARTUP
static int home_prepare_provision(struct bw_native_backend *,const struct bw_native_prepared *,unsigned *);
static int home_ready_observe(struct bw_native_backend *,const struct bw_provider_binding *,const struct bw_native_prepared *,const struct bw_setup_generation *);
#endif
enum {SETUP_IDLE,SETUP_CHECKING,SETUP_SAVING,SETUP_SAVED,SETUP_FAILED,SETUP_UNCONFIRMED};
enum {SETUP_NO_ERROR,SETUP_CONNECTION_ERROR,SETUP_STALE_ERROR,SETUP_IDLE_ERROR,SETUP_SAVE_ERROR,SETUP_UNCONFIRMED_ERROR,SETUP_START_ERROR};
static double now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int worker_start(pthread_t *thread,void *(*entry)(void *),void *opaque){
    /* The copier can nest two 256 KiB scratch frames. Console defaults are
     * smaller than Linux's, so every owned worker gets one bounded stack. */
    pthread_attr_t attr;int result=pthread_attr_init(&attr);if(result)return result;
    result=pthread_attr_setstacksize(&attr,4u*1024u*1024u);
    if(!result)result=pthread_create(thread,&attr,entry,opaque);
    (void)pthread_attr_destroy(&attr);return result;
}
static int load(void *opaque,struct bw_share_config *share){(void)opaque;return bw_share_load(share);}
static int read_settings(void *opaque,struct bw_share_config *config,struct bw_setup_generation *generation){(void)opaque;return bw_native_setup_read(backend_data_root,config,generation);}
static int save_settings(void *opaque,const struct bw_share_config *config,const struct bw_setup_generation *generation,const char *nonce,int (*idle)(void *),void *guard,struct bw_setup_saved *saved){
    (void)opaque;return bw_native_setup_store(backend_data_root,config,generation,nonce,idle,guard,saved);
}
static int nonce(void *opaque,char out[33]){(void)opaque;return bw_native_setup_nonce(out);}
static int nonce_valid(const char value[33]){if(strnlen(value,33)!=32)return 0;unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",value[i]))return 0;nonzero|=value[i]!='0';}return nonzero!=0;}
static int valid_config(const struct bw_share_config *p){return bw_native_setup_config_valid(p);}
static int inspected(void *opaque,const struct bw_provider_binding *binding,struct bw_native_prepared *out){(void)opaque;return bw_native_prepared_inspect(backend_data_root,binding,out);}
static int metadata_matches(const struct bw_source_info *a,const struct bw_source_info *b){
    return a&&b&&a->size==b->size&&a->inode==b->inode&&a->mtime_ticks/10==b->mtime_ticks/10&&a->ctime_ticks/10==b->ctime_ticks/10&&a->attributes==b->attributes;
}
struct preparation_lease {
    struct bw_native_backend *owner;const struct bw_native_prepare_selection *accepted;
    const struct bw_native_job_token *token;struct bw_native_prepare_selection captured;
    struct bw_source_smb *source;struct bw_native_jobs *jobs;uint64_t generation;uint32_t revision;int worker_kind;
};
/* Called synchronously inside DATA callbacks. No settings, source/provider,
 * jobs snapshots, progress, filesystem or SDK calls are allowed here. */
static int preparation_data_current(void *opaque){
    const struct preparation_lease *lease=opaque;if(!lease||!lease->owner||!lease->accepted||!lease->source)return -1;
    struct bw_native_backend *p=lease->owner;pthread_mutex_lock(&p->lock);
    int bad=!p->active||p->worker_kind!=lease->worker_kind||p->generation!=lease->generation||p->settings_revision!=lease->revision||
        p->jobs!=lease->jobs||p->stale||p->preparation_changed||memcmp(lease->accepted,&lease->captured,sizeof lease->captured)||
        (lease->worker_kind==WORK_PREPARE?(lease->accepted!=&p->sending.selection||lease->token!=&p->sending.token):
         lease->worker_kind!=WORK_PLAY||lease->accepted!=&p->playing.prepare_selection||lease->token!=&p->playing.prepare_token||p->playing.changed||p->playing.close_requested);
    pthread_mutex_unlock(&p->lock);
    double at=now();return bad||at<0||at>=lease->source->deadline||lease->source->error||
        lease->source->content_bytes>BW_PROVIDER_MAX_LOCAL_BYTES*3u+UINT64_C(268435456)||lease->source->requests>524288u?-1:0;
}
static int preparation_lease_open(struct preparation_lease *lease,struct bw_native_backend *p,
    const struct bw_native_prepare_selection *selection,struct bw_source_smb *source,int (*outer)(void *),void *observer){
    if(!p||!selection||!source||(outer!=prepare_current&&outer!=start_prepare_current))return -1;
    pthread_mutex_lock(&p->lock);
    const struct bw_native_job_token *token=p->worker_kind==WORK_PREPARE?&p->sending.token:&p->playing.prepare_token;
    *lease=(struct preparation_lease){.owner=p,.accepted=selection,.token=token,.captured=*selection,.source=source,
        .jobs=p->jobs,.generation=p->generation,.revision=p->settings_revision,.worker_kind=p->worker_kind};
    int bad=observer!=token||token->owner!=p->jobs;pthread_mutex_unlock(&p->lock);
    return bad||preparation_data_current(lease)?-1:0;
}
struct preparation_upgrade {
    struct bw_startup_runtime_control control;
    struct preparation_lease *lease;
    struct bw_source_provider *provider;
    unsigned attempted;
};
static int preparation_upgrade_idle(void *opaque){
    struct preparation_lease *lease=opaque;
    if(preparation_data_current(lease))return -1;
    if(lease->worker_kind==WORK_PREPARE){
        pthread_mutex_lock(&lease->owner->lock);int scoped=lease->owner->sending.authority[0]!=0;pthread_mutex_unlock(&lease->owner->lock);
        if(scoped)return prepare_authority(lease->owner,BW_PREPARE_IO);
    }
    struct bw_startup_idle_request request={.deadline=lease->source->deadline,
        .current=preparation_data_current,.opaque=lease,.game_log=backend_game_log};
    return bw_startup_idle_ps5(&request);
}
/* Called only after normal Send has qualified the exact owned promotion.
 * Unaffected preparations never open this CONTROL session. The accepted
 * worker, provider and all callbacks remain joined within prepare(). */
static int preparation_upgrade_unmapped(void *opaque,const char *root,const char *title){
    struct preparation_upgrade *upgrade=opaque;
    if(!upgrade||!upgrade->provider||upgrade->provider->busy||preparation_data_current(upgrade->lease))return -1;
    if(!upgrade->attempted){
        upgrade->attempted=1;char expected[128];
        const struct bw_source_binding *binding=&upgrade->lease->captured.identity.binding;
        int n=snprintf(expected,sizeof expected,"%s/games/%s",backend_data_root,binding->game);
        if(n<0||(size_t)n>=sizeof expected||!root||!title||strcmp(root,expected)||strcmp(title,binding->title))return -1;
        struct bw_startup_runtime_request request={.root=expected,.title=binding->title,
            .deadline=upgrade->lease->source->deadline,.current=preparation_data_current,
            .idle=preparation_upgrade_idle,.opaque=upgrade->lease};
        if(bw_startup_runtime_open(&upgrade->control,&request))return -1;
    }
    return bw_startup_runtime_unmapped(&upgrade->control,root,title);
}
static int prepare(void *opaque,struct bw_share_config *config,const struct bw_native_prepare_selection *selection,int (*current)(void *),void *guard,bw_source_observer_fn observe,void *observer,struct bw_native_prepared *out){
    (void)opaque;int result=BW_SEND_FAILED;struct bw_source_smb source={.deadline=now()+1800,.observe=observe,.observe_opaque=observer};
    struct bw_source_provider provider={0};struct bw_container_smb *image=NULL;struct preparation_lease lease={0};struct bw_native_derived_context derived={0};
    struct bw_native_batch_context batch={0};
    struct preparation_upgrade upgrade={.lease=&lease,.provider=&provider};
    if(!selection||!current||current(guard)||observe!=bw_native_job_observe||
       preparation_lease_open(&lease,guard,selection,&source,current,observer))goto done;
    struct bw_provider_binding resolved={0};
    if(bw_provider_binding_select(&resolved,config,selection->identity.binding.source,selection->identity.binding.title,
        selection->identity.kind,selection->identity.binding.game)||memcmp(&resolved,&selection->identity,sizeof resolved))goto done;
    source.smb=smb2_init_context();if(!source.smb)goto done;
    smb2_set_timeout(source.smb,10);smb2_set_sign(source.smb,1);smb2_set_security_mode(source.smb,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);smb2_set_user(source.smb,config->user);smb2_set_password(source.smb,config->password);
    int connected=smb2_connect_share(source.smb,config->server,config->share,config->user);bw_share_clear(config);if(connected<0||current(guard))goto done;
    if(selection->identity.kind==BW_PROVIDER_CONTAINER){
        image=calloc(1,sizeof *image);if(!image)goto done;
        if(bw_container_smb_open_owned(image,&source.smb,selection->identity.binding.source))goto done;
        if(!bw_source_catalog_image_same(&selection->image,&image->admitted)||selection->container_kind!=image->container.kind){result=BW_BACKEND_SOURCE_CHANGED;goto done;}
    }else if(selection->identity.kind!=BW_PROVIDER_FOLDER)goto done;
    if(current(guard)||bw_source_provider_prepare(&provider,&selection->identity,&source,image,selection->run,BW_PROVIDER_MAX_LOCAL_BYTES,preparation_data_current,&lease))goto done;
    if(selection->identity.kind==BW_PROVIDER_FOLDER){
        if(!metadata_matches(&selection->boot,bw_source_snapshot_find(&provider.folder.snapshot,"eboot.bin"))||
           !metadata_matches(&selection->param,bw_source_snapshot_find(&provider.folder.snapshot,"sce_sys/param.json"))){result=BW_BACKEND_SOURCE_CHANGED;goto done;}
    }else{
        struct bw_manifest manifest;uint64_t boot=0;
        if(bw_manifest_open(&manifest,provider.bundle.map.bytes,provider.bundle.map.size))goto done;
        for(uint32_t i=0;i<manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&manifest,i,&row))goto done;if(!strcmp(row.path,"eboot.bin"))boot=row.size;}
        if(boot!=selection->logical_boot_size||!bw_source_catalog_image_same(&selection->image,&image->admitted)){result=BW_BACKEND_SOURCE_CHANGED;goto done;}
    }
    if(current(guard)||bw_source_provider_current(&provider))goto done;
    int plain_folder=0;
#ifdef BW_NATIVE_PRELOAD
    /* The early reader supplies original file ranges from first intercepted
     * reads. Do not expand engine archives into a different prepared policy.
     * A matching old partial/history remains an explicit migration blocker;
     * only the normal ordinary inspector can qualify this target. */
    if(selection->identity.kind==BW_PROVIDER_FOLDER){
        struct bw_native_prepared existing={0};
        int inspected=bw_native_prepared_inspect(backend_data_root,&selection->identity,&existing);
        if((inspected!=0&&!(inspected==BW_SEND_FAILED&&existing.state==BW_PREPARED_ABSENT))||
           (existing.state!=BW_PREPARED_ABSENT&&existing.state!=BW_PREPARED_VALID)||
           existing.prepared_policy){result=BW_SEND_UNCONFIRMED;goto done;}
        plain_folder=1;
    }
#endif
    if(selection->identity.kind==BW_PROVIDER_FOLDER&&!plain_folder){
        int supported=bw_native_batch_context_prepare(&batch,&provider,preparation_data_current,&lease,selection,lease.jobs,lease.token);
        if(supported<0){struct bw_source_progress progress=source.progress;progress.phase=BW_SOURCE_ERROR;observe(observer,&progress);goto done;}
        if(supported){
            struct bw_native_batch_prepared value={0};
            const struct bw_native_batch_send_files request={.data_root=backend_data_root,.game_log=backend_game_log,
                .plan=bw_native_batch_context_plan(&batch),.plan_request=bw_native_batch_context_request(&batch),
                .material=bw_native_batch_context_material(&batch),.current=current,.opaque=guard};
            result=current(guard)?BW_SEND_FAILED:bw_native_batch_send_files(&request,&value);
            if(!result&&(bw_native_prepared_from_batch(&value,out)||bw_native_batch_context_revalidate(&batch)))result=BW_SEND_UNCONFIRMED;
            goto done;
        }
        if(bw_native_batch_context_destroy(&batch))goto done;
    }
    if(selection->identity.kind==BW_PROVIDER_FOLDER&&!plain_folder){
        int supported=bw_native_derived_context_prepare(&derived,&provider,preparation_data_current,&lease,selection,lease.jobs,lease.token);
        if(supported<0){
            struct bw_source_progress progress=source.progress;progress.phase=BW_SOURCE_ERROR;observe(observer,&progress);goto done;
        }
        if(supported){
            struct bw_native_derived_prepared value={0};
            const struct bw_native_derived_send_files request={.data_root=backend_data_root,.game_log=backend_game_log,.material=&derived.material,
                .original=&provider,.project=derived.project,.local=&derived.local,.current=current,.opaque=guard};
            result=current(guard)?BW_SEND_FAILED:bw_native_derived_send_files(&request,&value);
            if(!result&&bw_native_prepared_from_derived(&value,out))result=BW_SEND_UNCONFIRMED;
            goto done;
        }
    }
    struct bw_native_send_files request={.data_root=backend_data_root,.game_log=backend_game_log,.provider=&provider,.current=current,.opaque=guard,
        .upgrade_unmapped=preparation_upgrade_unmapped,.upgrade_opaque=&upgrade};
    if(lease.worker_kind==WORK_PREPARE){
        pthread_mutex_lock(&lease.owner->lock);int scoped=lease.owner->sending.authority[0]!=0;pthread_mutex_unlock(&lease.owner->lock);
        if(scoped){request.idle=prepare_idle;request.publication_current=prepare_publication;}
    }
    result=bw_native_send_files(&request,out);
done:
    /* The preparation wrapper owns this borrowed view exclusively. Restore it
     * and close this session before returning; the selected reader opens a
     * fresh image and binds fresh tokens rather than borrowing these objects. */
    if(bw_startup_runtime_destroy(&upgrade.control)&&!result)result=BW_SEND_UNCONFIRMED;
    if(bw_native_batch_context_destroy(&batch)&&!result)result=BW_SEND_UNCONFIRMED;
    bw_share_clear(config);if(bw_native_derived_context_destroy(&derived)&&!result)result=BW_SEND_UNCONFIRMED;bw_source_provider_destroy(&provider);
    if(image){if(image->handle&&bw_container_smb_close(image)&&!result)result=BW_SEND_UNCONFIRMED;free(image);}
    if(source.smb)smb2_destroy_context(source.smb);return result;
}

#ifdef BW_NATIVE_PLAYBACK
struct play_context {
    int (*current)(void *);void *guard;struct bw_native_backend *owner;
    const struct bw_native_play_selection *selected;struct bw_native_play_selection captured;
    uint64_t generation,epoch;uint32_t revision;
    struct bw_native_launch_admission *startup;
    struct bw_reader_selection reader_selection;
    struct bw_native_reader_release release;
    char reservation[33];
    double retirement_deadline;
    unsigned startup_closed;
    const char *registration_failure;
#ifdef BW_NATIVE_PRELOAD
    struct bw_idle_log_cache *control_log;
    unsigned retirement_cache_scope;
#endif
};
#ifdef BW_NATIVE_PRELOAD
#ifdef BW_NATIVE_BACKEND_HOST_TEST
static struct bw_nrc_environment registration_environment={NULL,"/user/app","/system_data/priv/mms/app.db"};
#else
static const struct bw_nrc_environment registration_environment={NULL,"/user/app","/system_data/priv/mms/app.db"};
#endif
static int registration_pending_refresh(struct bw_native_backend *p){
    struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;
    struct bw_nrc_candidate *list=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *list);size_t count=0;
    int result=!list?-1:bw_nrc_candidates(&env,list,BW_NRC_CANDIDATE_LIMIT,&count);
    struct bw_removed_source *removed=NULL;size_t removed_count=0;
    if(!result&&count){removed=calloc(BW_REMOVED_LIMIT,sizeof *removed);if(!removed||bw_removed_list(backend_data_root,removed,BW_REMOVED_LIMIT,&removed_count))result=-1;}
    pthread_mutex_lock(&p->lock);
    p->registration_ledger_unknown=result!=0;
    if(!result){p->registration_pending_count=0;
        for(size_t i=0;i<count;i++){
            unsigned removed_owner=0;for(size_t j=0;j<removed_count;j++)if(!strcmp(list[i].subject.title,removed[j].identity.binding.title)&&!strcmp(list[i].subject.game,removed[j].identity.binding.game)){removed_owner=1;break;}
            if(!list[i].pending&&!list[i].retiring&&!removed_owner)continue;
            size_t at=p->registration_pending_count++;memcpy(p->registration_pending[at].title,list[i].subject.title,10);
            memcpy(p->registration_pending[at].game,list[i].subject.game,75);memcpy(p->registration_pending[at].run,list[i].subject.run,33);
            p->registration_pending[at].retiring=list[i].retiring;p->registration_pending[at].removed=removed_owner&&!list[i].pending&&!list[i].retiring;
        }
    }
    pthread_mutex_unlock(&p->lock);free(list);free(removed);return result;
}
#endif
static int play_data_current(struct play_context *lease){
    if(!lease||!lease->owner||!lease->selected)return -1;
    struct bw_native_backend *p=lease->owner;pthread_mutex_lock(&p->lock);
    int bad=!lease->epoch||!p->active||p->worker_kind!=WORK_PLAY||p->generation!=lease->generation||p->play_epoch!=lease->epoch||p->settings_revision!=lease->revision||
        p->stale||p->playing.changed||p->playing.close_requested||lease->selected!=&p->playing.selection||
        memcmp(lease->selected,&lease->captured,sizeof lease->captured);
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static int play_context_capture(struct play_context *context){
    if(!context||!context->owner)return -1;struct bw_native_backend *p=context->owner;
    pthread_mutex_lock(&p->lock);context->generation=p->generation;context->revision=p->settings_revision;context->epoch=p->play_epoch;
    memcpy(context->reservation,p->playing.dispatch,33);pthread_mutex_unlock(&p->lock);
    return play_data_current(context);
}
static int exact_registration(void *opaque,const struct bw_reader_selection *selected){
    struct play_context *ctx=opaque;
    return ctx->current(ctx->guard)||bw_native_setup_idle(NULL)||
        (ctx->startup&&bw_native_launch_admission_current(ctx->startup,backend_data_root,&ctx->captured.identity,selected,NULL))||
        bw_native_registration_verify(backend_data_root,"/user/app","/system_data/priv/mms/app.db",selected)||ctx->current(ctx->guard)?-1:0;
}
struct play_observer {struct play_context context;void (*observe)(void *,const struct bw_reader_event *);void *observer;};
static int play_current_adapter(void *opaque){struct play_observer *ctx=opaque;return play_data_current(&ctx->context)||ctx->context.current(ctx->context.guard)?-1:0;}
static int play_data_adapter(void *opaque){struct play_observer *ctx=opaque;return play_data_current(&ctx->context);}
static int play_registered_adapter(void *opaque,const struct bw_reader_selection *selected){struct play_observer *ctx=opaque;return exact_registration(&ctx->context,selected);}
static void play_observe_adapter(void *opaque,const struct bw_reader_event *event){struct play_observer *ctx=opaque;ctx->observe(ctx->observer,event);}
#ifdef BW_NATIVE_PRELOAD
static int play_retirement_lease(void *opaque){
    struct play_observer *o=opaque;struct play_context *c=&o->context;struct bw_native_backend *p=c->owner;
    if(!c->startup_closed||strcmp(c->release.run,c->captured.run)||!c->release.runtime_released||!c->release.io_released||!c->release.private_released)return -1;
    double at=now();if(at<0||at>=c->retirement_deadline)return -1;
    pthread_mutex_lock(&p->lock);
    int bad=!p->active||p->worker_kind!=WORK_PLAY||!p->playing.retiring||p->generation!=c->generation||p->play_epoch!=c->epoch||
        p->settings_revision!=c->revision||p->stale||p->playing.changed||c->selected!=&p->playing.selection||
        memcmp(c->selected,&c->captured,sizeof c->captured)||strcmp(p->playing.dispatch,c->reservation)||
        (p->external_nonce[0]&&strcmp(p->external_nonce,c->reservation));
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
/* These caches exist only inside existing bounded CONTROL sub-owners.
 * No cache survives bundle qualification or retirement, enters DATA, or
 * imposes a new deadline on the indefinite game/remaining REGISTER lifetime. */
static int play_control_log_begin(struct play_observer *o,double ceiling,int (*lease)(void *)){
    struct play_context *c=&o->context;
    struct bw_startup_idle_request idle={.deadline=now()+2.0,.current=lease,.opaque=o,.game_log=backend_game_log};
    if(bw_idle_log_cache_begin(&c->control_log,c->captured.run,ceiling,&idle)){
        if(!c->registration_failure)c->registration_failure="operation_log_cache_begin_refused";return -1;
    }return 0;
}
static int play_control_log_close(struct play_observer *o){
    struct play_context *c=&o->context;if(!c->control_log)return 0;
    struct bw_idle_log_cache_release report={0};int closed=bw_idle_log_cache_close(&c->control_log,&report);
    if(closed||report.release_unknown){
        pthread_mutex_lock(&c->owner->lock);c->owner->control_release_unknown=1;pthread_mutex_unlock(&c->owner->lock);
        if(!c->registration_failure)c->registration_failure="operation_log_cache_release_unknown";
        struct bw_reader_event event={.phase="error",.message="The operation's final checks could not be released. Refresh status without repeating the action."};
        o->observe(o->observer,&event);return -1;
    }return 0;
}
static int play_control_log_idle(struct play_observer *o,const struct bw_startup_idle_request *idle){
    return o->context.control_log?bw_startup_idle_ps5_cached(idle,o->context.control_log,o->context.captured.run):bw_startup_idle_ps5((void *)idle);
}
static int play_registration_control(void *opaque,enum bw_nrl_control point){
    struct play_observer *o=opaque;struct play_context *c=&o->context;struct bw_native_backend *p=c->owner;
    struct bw_startup_idle_request idle={.deadline=now()+2.0,.opaque=o,.game_log=backend_game_log};
    if(point==BW_NRL_REGISTER){
        idle.current=play_data_adapter;
        if(play_current_adapter(o)){if(!c->registration_failure)c->registration_failure="registration_current_before_refused";return -1;}
        /* The native cached evaluator already checks fresh processes and the
         * exact lifecycle log. Do not parse that same log again through the
         * default legacy callback. Custom callbacks and uncached scopes retain
         * their independent idle admission. */
        if((!c->control_log||p->ops.idle!=bw_native_setup_idle)&&p->ops.idle(p->opaque)){if(!c->registration_failure)c->registration_failure="registration_full_idle_refused";return -1;}
        if(play_control_log_idle(o,&idle)){if(!c->registration_failure)c->registration_failure="registration_game_log_idle_refused";return -1;}
        if(c->startup&&bw_native_launch_admission_current(c->startup,backend_data_root,&c->captured.identity,&c->reader_selection,NULL)){
            if(!c->registration_failure)c->registration_failure="registration_startup_current_refused";return -1;
        }
        if(play_current_adapter(o)){if(!c->registration_failure)c->registration_failure="registration_current_after_refused";return -1;}return 0;
    }
    /* This is a new CONTROL reservation within the still-owned joined worker.
     * The former Close flag is deliberately not DATA authority here. */
    if(point!=BW_NRL_RETIRE||play_retirement_lease(o))return -1;
    if(c->retirement_cache_scope&&!c->control_log&&play_control_log_begin(o,c->retirement_deadline,play_retirement_lease))return -1;
    struct bw_share_config config={0};struct bw_setup_generation generation={0};
    int bad=p->ops.read_settings(p->opaque,&config,&generation)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&generation,&c->captured.connection);
    idle.current=play_retirement_lease;
    bw_share_clear(&config);return bad||((!c->control_log||p->ops.idle!=bw_native_setup_idle)&&p->ops.idle(p->opaque))||play_control_log_idle(o,&idle)||play_retirement_lease(o)?-1:0;
}
static int play_registration_bundle_current(void *opaque){return play_registration_control(opaque,BW_NRL_REGISTER);}
#endif
static int play(void *opaque,const struct bw_native_play_selection *selected,int (*current)(void *),void *guard,void (*observe)(void *,const struct bw_reader_event *),void *observer){
    (void)opaque;if(current!=play_current||!guard||!selected)return -1;
#ifdef BW_NATIVE_PRELOAD
    /* Automatic early-folder play never adopts a cached old derived/batch
     * preparation. Explicit v6/v7 launch/preflight APIs remain unchanged. */
    if(selected->identity.kind==BW_PROVIDER_FOLDER&&selected->prepared.prepared_policy)return -1;
#endif
    struct bw_native_backend *p=guard;struct play_observer ctx={.context={.current=current,.guard=guard,.owner=p,.selected=selected,.captured=*selected},.observe=observe,.observer=observer};
    if(play_context_capture(&ctx.context))return -1;
    struct bw_native_reader_request request={.connection=selected->connection,.current=play_current_adapter,.before_launch=play_registered_adapter,.observe=play_observe_adapter,.opaque=&ctx,.data_current=play_data_adapter,.data_epoch=ctx.context.epoch};
    int result=-1;const char *failure=NULL;struct bw_native_reader_release released={0};
#ifdef BW_NATIVE_PRELOAD
    struct bw_nrl_owner *registration_owner=NULL;int ownership_mode=0,control_failure_scope=0,reader_entered=0;
#endif
    request.released=&released;
    if(current(guard)||bw_native_setup_idle(NULL))goto done;
#ifdef BW_NATIVE_PRELOAD
    if(selected->identity.kind==BW_PROVIDER_FOLDER||selected->identity.kind==BW_PROVIDER_CONTAINER){
        if(BW_LT_CALL(BW_LT_SELECT,bw_native_launch_admission_open(&ctx.context.startup,backend_data_root,&selected->identity,
                &selected->prepared,selected->run,play_current_adapter,&ctx,&failure))||
           bw_native_launch_admission_selection(ctx.context.startup,&request.selection)){
            struct bw_reader_event event={.phase="error",.message=failure?failure:"Required startup files changed; choose Send to repair them."};observe(observer,&event);goto done;
        }
        request.startup=ctx.context.startup;
    }else
#endif
    if(BW_LT_CALL(BW_LT_SELECT,bw_native_launch_selection(backend_data_root,&selected->identity,&selected->prepared,selected->run,&request.selection)))goto done;
    if(current(guard))goto done;
    ctx.context.reader_selection=request.selection;
#ifdef BW_NATIVE_PRELOAD
    if(request.startup){
        ownership_mode=1;
        control_failure_scope=1;
        char root[BW_NRC_PATH_BYTES];int n=snprintf(root,sizeof root,"%s/games/%s",backend_data_root,request.selection.game);
        struct bw_preload_bundle_request bundle={root,play_data_adapter,&ctx,now()+20.0};
        const struct bw_preload_bundle_control bundle_control={play_registration_bundle_current,&ctx};
        if(n<0||(size_t)n>=sizeof root){
            struct bw_reader_event event={.phase="error",.message="reader_bundle_root_path_invalid"};observe(observer,&event);goto done;
        }
        if(play_control_log_begin(&ctx,bundle.deadline,play_data_adapter)||play_registration_bundle_current(&ctx)){
            struct bw_reader_event event={.phase="error",.message=ctx.context.registration_failure?ctx.context.registration_failure:
                "reader_bundle_before_install_guard_refused"};observe(observer,&event);goto done;
        }
        struct bw_preload_bundle_result bundled={0};
        struct bw_preload_bundle_upgrade_request upgrade={.bundle=bundle,.data_root=backend_data_root,
            .game=request.selection.game,.prepared_run=selected->prepared.run,.launch_run=selected->run,
            .prepared_record_sha256=selected->prepared.record_sha256};
        int bundle_status=bw_preload_bundle_upgrade_controlled(&upgrade,&bundle_control,&bundled);
        if(bundle_status){
            /* A retained per-game upgrade checkpoint still refuses this title,
             * but known-closed material does not retain unrelated operations.
             * Only an ambiguous descriptor/lock release holds global CONTROL. */
            if(bundled.release_unknown){pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);}
            char message[256];snprintf(message,sizeof message,"%s (reason=%s detail=%s library=%s errno=%d mutation=%u)",bundled.stage?bundled.stage:
                "reader_bundle_qualification_unconfirmed",bundled.reason?bundled.reason:"none",ctx.context.registration_failure?ctx.context.registration_failure:"none",
                bundled.library?bundled.library:"none",bundled.system_error,bundled.mutation_started);
            struct bw_reader_event event={.phase="error",.message=message};observe(observer,&event);goto done;
        }
        if(play_control_log_close(&ctx))goto done;
        struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;
        struct bw_nrl_request acquire={.environment=env,.selection=&request.selection,.current=play_registration_control,.opaque=&ctx};
        struct bw_nrc_result acquired={0};
        int acquired_status=bw_nrl_begin(&acquire,&registration_owner,&acquired);
        if(acquired_status){
            if(acquired_status==BW_NRC_RELEASE_UNCONFIRMED){pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);}
            char message[256];snprintf(message,sizeof message,"%s (detail=%s raw=%d errnoSnapshot=%d)",acquired.reason?acquired.reason:
                "Registration ownership could not be retained; launch was stopped.",ctx.context.registration_failure?ctx.context.registration_failure:
                acquired.detail?acquired.detail:"none",acquired.raw_result,acquired.error_snapshot);
            struct bw_reader_event event={.phase="error",.message=message};observe(observer,&event);goto done;
        }
    }
#endif
    struct bw_native_register_request registration={.data_root=backend_data_root,.apps="/user/app",.database="/system_data/priv/mms/app.db",
        .identity=&selected->identity,.selected=&request.selection,.current=current,.guard=guard,.observe=observe,.observer=observer,.startup=request.startup};
    struct bw_native_owned_registration_result upgraded={0};
    if(BW_LT_CALL(BW_LT_OWNED_REGISTER,bw_native_owned_registration(&registration,&upgraded))){
        struct bw_reader_event event={.phase="error",.message=upgraded.message?upgraded.message:"The selected startup copy could not be registered."};observe(observer,&event);goto done;
    }
    struct bw_register_result checked={0};int registered=BW_LT_CALL(BW_LT_REGISTER,bw_native_register(&registration,&checked));
    int committed_status=0;const char *commit_failure=NULL;
#ifdef BW_NATIVE_PRELOAD
    struct bw_nrc_result committed={0};if(registration_owner)committed_status=bw_nrl_registered(registration_owner,registered,&checked,&committed);
    if(committed_status==BW_NRC_RELEASE_UNCONFIRMED){pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);}
    commit_failure=committed.reason;
#endif
    if(registered||committed_status){
        char message[256];const char *why=registered?(checked.message?checked.message:"The prepared copy could not be registered."):
            commit_failure?commit_failure:"Registration ownership could not be confirmed; no game was started.";
        int raw=0,error=0;const char *detail="none";
#ifdef BW_NATIVE_PRELOAD
        raw=committed.raw_result;error=committed.error_snapshot;if(ctx.context.registration_failure)detail=ctx.context.registration_failure;else if(committed.detail)detail=committed.detail;
#endif
        snprintf(message,sizeof message,"%s (detail=%s raw=%d errnoSnapshot=%d add=%u confirmed=%u scan=%u registered=%u)",why,detail,raw,error,
            checked.add_attempted,checked.add_confirmed,checked.scan_attempted,checked.registered);
        struct bw_reader_event event={.phase="error",.message=message};observe(observer,&event);goto done;
    }
#ifdef BW_NATIVE_PRELOAD
#ifdef BW_HOME_STARTUP
    pthread_mutex_lock(&p->lock);if(p->playing.item<BW_CATALOG_MAX_ITEMS)p->home_ready[p->playing.item]=1;
    if(!strcmp(p->home_setup_game,selected->prepared.game)){memset(p->home_setup_game,0,sizeof p->home_setup_game);memset(p->home_setup_reason,0,sizeof p->home_setup_reason);}
    pthread_mutex_unlock(&p->lock);
#endif
    reader_entered=1;
#endif
    result=bw_native_reader_run(&request);
    pthread_mutex_lock(&p->lock);p->control_release_unknown=!released.runtime_released||!released.io_released||!released.private_released;pthread_mutex_unlock(&p->lock);
done:;
    /* bw_native_reader_run retains unknown accepted game ownership and does
     * not return while borrowed callbacks or native I/O can still be live. */
    int cache_closed=0;
#ifdef BW_NATIVE_PRELOAD
    cache_closed=play_control_log_close(&ctx);
    /* A definite CONTROL failure does not invalidate startup data which this
     * same live owner freshly rechecks. Initial admission alone is not enough:
     * late drift/current loss, reader errors and ambiguous release stay strict. */
    int control_verified=result&&control_failure_scope&&!reader_entered&&ctx.context.startup&&!cache_closed&&
        !play_current_adapter(&ctx)&&!bw_native_launch_admission_current(ctx.context.startup,backend_data_root,
            &ctx.context.captured.identity,&ctx.context.reader_selection,NULL);
#endif
    int closed=bw_native_launch_admission_close(&ctx.context.startup);
    if(closed){pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);}
    if(closed||cache_closed)result=-1;
#ifdef BW_NATIVE_PRELOAD
    ctx.context.startup_closed=!closed&&!cache_closed;ctx.context.release=released;
    #ifndef BW_HOME_STARTUP
    if(registration_owner&&!closed&&!cache_closed&&released.runtime_released&&released.io_released&&released.private_released){
        pthread_mutex_lock(&p->lock);p->playing.retiring=1;pthread_mutex_unlock(&p->lock);
        ctx.context.retirement_deadline=now()+30.0;
        struct bw_nrc_retirement_result retired={0};
        ctx.context.retirement_cache_scope=1;
        /* One live release certificate permits the initial distinct phases;
         * durable markers consume each request. Bounded readback never resends
         * an earlier manual-remove/uninstall, even after unknown response. */
        for(unsigned i=0;i<4;i++){
            int cleanup=bw_nrl_retire(registration_owner,&released,&retired);
            if(cleanup){retired.registration.state=BW_NRC_UNKNOWN;break;}
            if(retired.registration.state!=BW_NRC_PENDING)break;
            struct timespec delay={0,100000000};if(nanosleep(&delay,NULL))break;
        }
        ctx.context.retirement_cache_scope=0;
        if(play_control_log_close(&ctx)){retired.registration.state=BW_NRC_UNKNOWN;result=-1;}
        if(retired.registration.state!=BW_NRC_ABSENT&&retired.registration.state!=BW_NRC_UNOWNED){
            pthread_mutex_lock(&p->lock);
            p->playing.cleanup_attention=1;
            snprintf(p->playing.message,sizeof p->playing.message,"Game exited. Registration cleanup is pending or needs attention; the prepared source was kept.");
            pthread_mutex_unlock(&p->lock);
        }
        pthread_mutex_lock(&p->lock);p->playing.retiring=0;pthread_mutex_unlock(&p->lock);
    }
#endif /* BW_HOME_STARTUP: committed Ready registration persists until Uninstall. */
    int registration_closed=bw_nrl_destroy(&registration_owner);
    if(registration_closed){
        pthread_mutex_lock(&p->lock);p->control_release_unknown=1;pthread_mutex_unlock(&p->lock);result=-1;
    }
    if(control_verified&&!closed&&!registration_closed){
        pthread_mutex_lock(&p->lock);
        if(!p->playing.changed&&!p->playing.close_requested&&!p->control_release_unknown)p->playing.preflight_control_verified=1;
        pthread_mutex_unlock(&p->lock);
    }
    if(ownership_mode)(void)registration_pending_refresh(p);
#endif
    return result;
}
static int close_game(void *opaque,const char *run){(void)opaque;return bw_native_reader_close(run);}
#endif
static int config_equal(const struct bw_share_config *a,const struct bw_share_config *b){return !strcmp(a->server,b->server)&&!strcmp(a->share,b->share)&&!strcmp(a->share2,b->share2)&&!strcmp(a->user,b->user)&&!strcmp(a->password,b->password);}
static void settings_adopt(struct bw_native_backend *p,int status,const struct bw_share_config *config,const struct bw_setup_generation *generation,int mark_stale){
    int changed=p->settings_known&&(p->settings_status!=status||!bw_native_setup_generation_equal(&p->settings_generation,generation));
    if(!p->settings_known)p->settings_revision=1;else if(changed){if(p->settings_revision<UINT32_MAX)p->settings_revision++;else p->settings_enabled=0;}
    p->settings_known=1;p->settings_status=status;p->settings_generation=*generation;
    if(status==BW_SETUP_VALID){memcpy(p->server,config->server,sizeof p->server);memcpy(p->share,config->share,sizeof p->share);
        memcpy(p->share2,config->share2,sizeof p->share2);
        memcpy(p->user,config->user,sizeof p->user);p->password_saved=config->password[0]!=0;}
    else{memset(p->server,0,sizeof p->server);memset(p->share,0,sizeof p->share);memset(p->share2,0,sizeof p->share2);memset(p->user,0,sizeof p->user);p->password_saved=0;}
    if(changed&&mark_stale&&p->generation){p->phase=BACKEND_CONFIG_CHANGED;p->stale=p->library.catalog.count>0;}
}
static void settings_sync(struct bw_native_backend *p){
    /* Caller owns the controller mutex; no slow network operations here. */
    if(!p->settings_enabled||p->active)return;struct bw_share_config config={0};struct bw_setup_generation generation={0};
    int status=p->ops.read_settings(p->opaque,&config,&generation);settings_adopt(p,status,&config,&generation,1);bw_share_clear(&config);
}
void bw_native_library_destroy(struct bw_native_library *p){
    if(!p)return;if(p->covers)for(size_t i=0;i<p->cover_count&&i<BW_CATALOG_MAX_ITEMS;i++)free(p->covers[i].bytes);
    free(p->covers);free(p->prepared);free(p->removal);bw_source_catalog_destroy(&p->catalog);memset(p,0,sizeof *p);
}
static int same_info(const struct bw_source_info *a,const struct bw_source_info *b){
    return a->size==b->size&&a->inode==b->inode&&a->mtime_ticks==b->mtime_ticks&&a->ctime_ticks==b->ctime_ticks&&a->attributes==b->attributes;
}
static int source_compare(const char *a,const char *b){
    while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return x<y?-1:1;}
    return *a?1:*b?-1:0;
}
static int catalog_order(const struct bw_catalog_item *a,const struct bw_catalog_item *b){
    int order=source_compare(a->source,b->source);return order?order:source_compare(a->source_share,b->source_share);
}
static size_t catalog_selected(const struct bw_native_backend *p,const struct bw_native_send_request *request){
    struct bw_share_config config={0};memcpy(config.server,p->server,sizeof config.server);
    memcpy(config.share,p->share,sizeof config.share);memcpy(config.share2,p->share2,sizeof config.share2);
    size_t found=p->library.catalog.count;unsigned matches=0;
    for(size_t i=0;i<p->library.catalog.count;i++){
        const struct bw_catalog_item *item=p->library.catalog.items+i;struct bw_provider_binding binding={0};
        if(strcmp(item->source,request->source))continue;
        if(*request->source_binding&&(bw_source_catalog_binding(&binding,&config,item)||strcmp(binding.binding.game,request->source_binding)))continue;
        found=i;matches++;
    }
    return matches==1?found:p->library.catalog.count;
}
static int valid_library(const struct bw_native_library *p){
    const struct bw_source_catalog *c=&p->catalog;
    if(c->error||c->count>BW_CATALOG_MAX_ITEMS||c->count>c->capacity||(c->count&&!c->items)||p->cover_bytes>BW_NATIVE_COVER_LIMIT||
        (p->covers?p->cover_count!=c->count:p->cover_count!=0))return 0;
    size_t covers=0;
    for(size_t i=0;i<c->count;i++){
        const struct bw_catalog_item *item=&c->items[i];
        if(!memchr(item->source,0,sizeof item->source)||!memchr(item->source_share,0,sizeof item->source_share)||strpbrk(item->source_share,"/\\:")||!bw_json_utf8(item->source_share)||!memchr(item->name,0,sizeof item->name)||!memchr(item->title,0,sizeof item->title)||
            !memchr(item->title_id,0,sizeof item->title_id)||(unsigned)item->state>BW_CATALOG_CONTAINER_READY||!item->source[0]||
            bw_manifest_validate_path(item->source,strlen(item->source))||!bw_json_utf8(item->title)||(i&&catalog_order(c->items+i-1,item)>=0))return 0;
        if(bw_source_catalog_available(item)){if(strlen(item->title_id)!=9||memcmp(item->title_id,"PPSA",4))return 0;
            for(unsigned j=4;j<9;j++)if(item->title_id[j]<'0'||item->title_id[j]>'9')return 0;
        }else if(*item->title_id)return 0;
        if(item->state==BW_CATALOG_CONTAINER_READY){
            static const struct bw_source_info empty={0};uint8_t generation[32];bw_source_catalog_image_hash(&item->image,generation);
            if(item->container_error||item->container_kind<BW_CONTAINER_PFS||item->container_kind>BW_CONTAINER_WRAPPED_EXFAT||
               !item->logical_boot_size||item->logical_boot_size>UINT64_C(2147483648)||item->image.smb2_type!=SMB2_TYPE_FILE||
               item->container_bytes<48||item->container_bytes>BW_PFS_MAX_VOLUME||item->image.smb2_size!=item->container_bytes||memcmp(generation,item->image_sha256,32)||
               memcmp(&item->boot,&empty,sizeof empty)||memcmp(&item->param,&empty,sizeof empty)||memcmp(&item->icon,&empty,sizeof empty)||
               item->logical_icon_size>BW_SOURCE_SMALL_FILE_LIMIT||item->cover!=(item->logical_icon_size!=0))return 0;
        }
        if(p->covers){
            const struct bw_native_cover *cover=&p->covers[i];
            if((!cover->bytes)!=(!cover->size)||cover->size>BW_SOURCE_SMALL_FILE_LIMIT||cover->size>BW_NATIVE_COVER_LIMIT-covers||
                (cover->size&&(!item->cover||
                 (item->state==BW_CATALOG_FOLDER?cover->size!=item->icon.size:
                  item->state!=BW_CATALOG_CONTAINER_READY||cover->size!=item->logical_icon_size))))return 0;
            covers+=cover->size;
        }
    }
    return covers==p->cover_bytes;
}
/* The caller holds the backend lock for this validated catalog generation.
 * Artwork may be shared by exact title ID; source/reader capabilities and
 * the source generation and reader capabilities remain independent of this lookup. */
static const struct bw_native_cover *cached_cover(const struct bw_native_library *p,size_t item){
    if(!p->covers||item>=p->catalog.count||item>=p->cover_count)return NULL;
    const struct bw_catalog_item *selected=&p->catalog.items[item];
    if(!bw_source_catalog_available(selected)||!selected->title_id[0])return NULL;
    if(p->covers[item].bytes&&p->covers[item].size)return &p->covers[item];
    /* Catalog order is stable. Prefer the selected source above, then the
     * first folder that already owns bounded cached artwork for this title. */
    for(size_t i=0;i<p->catalog.count&&i<p->cover_count;i++)
        if(p->catalog.items[i].state==BW_CATALOG_FOLDER&&p->covers[i].bytes&&p->covers[i].size&&
           !strcmp(selected->title_id,p->catalog.items[i].title_id))return &p->covers[i];
    return NULL;
}
static int discover_one(void *opaque,struct bw_share_config *share,struct bw_native_library *out,size_t cover_limit){
    (void)opaque;struct bw_source_smb source={0};double start=now();if(start<0)return -1;source.deadline=start+180;
    char share_name[128];memcpy(share_name,share->share,sizeof share_name);
    source.smb=smb2_init_context();if(!source.smb)return -1;
    smb2_set_timeout(source.smb,10);smb2_set_sign(source.smb,1);smb2_set_security_mode(source.smb,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);smb2_set_user(source.smb,share->user);smb2_set_password(source.smb,share->password);
    int result=smb2_connect_share(source.smb,share->server,share->share,share->user);bw_share_clear(share);if(result<0)goto done;
    result=bw_source_catalog_scan(&source,&out->catalog);if(result)goto done;
    if(out->catalog.count){out->covers=calloc(out->catalog.count,sizeof *out->covers);if(!out->covers){result=-1;goto done;}out->cover_count=out->catalog.count;}
    for(size_t i=0;i<out->catalog.count;i++){
        struct bw_catalog_item *item=&out->catalog.items[i];memcpy(item->source_share,share_name,sizeof item->source_share);
        if(item->state==BW_CATALOG_CONTAINER_READY){
            void *bytes=NULL;size_t size=0;
            int found=bw_source_catalog_container_cover(&source,item,cover_limit-out->cover_bytes,&bytes,&size);
            if(found<0){free(bytes);result=-1;break;}
            if(!found){out->covers[i]=(struct bw_native_cover){bytes,size};out->cover_bytes+=size;}
            continue;
        }
        if(!item->cover||!item->icon.size||item->icon.size>cover_limit-out->cover_bytes)continue;
        char path[1280];int n=snprintf(path,sizeof path,"%s/sce_sys/icon0.png",item->source);if(n<0||(size_t)n>=sizeof path){result=-1;break;}
        void *bytes=NULL;size_t size=0;struct bw_source_info info;
        int found=bw_source_smb_small_file(&source,path,BW_SOURCE_SMALL_FILE_LIMIT,&bytes,&size,&info);
        if(found<0){free(bytes);result=-1;break;}
        if(!found&&size==item->icon.size&&same_info(&info,&item->icon)){
            out->covers[i]=(struct bw_native_cover){bytes,size};out->cover_bytes+=size;bytes=NULL;
        }free(bytes);
    }
done:
    out->requests=source.requests;out->entries=source.entries;out->content_bytes=source.content_bytes;
    if(source.smb)smb2_destroy_context(source.smb);return result;
}
/* Merge metadata and owned artwork together. Relative paths may repeat across
 * shares; no title/path-only deduplication can replace their source binding. */
static int library_merge(struct bw_native_library *out,struct bw_native_library parts[2]){
    size_t total=parts[0].catalog.count+parts[1].catalog.count;
    if(total>BW_CATALOG_MAX_ITEMS||parts[0].cover_bytes>BW_NATIVE_COVER_LIMIT||
       parts[1].cover_bytes>BW_NATIVE_COVER_LIMIT-parts[0].cover_bytes||
       parts[1].requests>UINT32_MAX-parts[0].requests||parts[1].entries>UINT32_MAX-parts[0].entries||
       parts[1].content_bytes>UINT64_MAX-parts[0].content_bytes)return -1;
    uint32_t *dst[]={&out->catalog.collections,&out->catalog.entries,&out->catalog.skipped,&out->catalog.unsearched};
    uint32_t a[]={parts[0].catalog.collections,parts[0].catalog.entries,parts[0].catalog.skipped,parts[0].catalog.unsearched};
    uint32_t b[]={parts[1].catalog.collections,parts[1].catalog.entries,parts[1].catalog.skipped,parts[1].catalog.unsearched};
    for(unsigned i=0;i<4;i++){if(b[i]>UINT32_MAX-a[i])return -1;*dst[i]=a[i]+b[i];}
    if(total){out->catalog.items=calloc(total,sizeof *out->catalog.items);out->covers=calloc(total,sizeof *out->covers);
        if(!out->catalog.items||!out->covers)return -1;}
    out->catalog.count=out->catalog.capacity=out->cover_count=total;
    size_t index[2]={0,0};
    for(size_t i=0;i<total;i++){
        unsigned from;
        if(index[0]==parts[0].catalog.count)from=1;
        else if(index[1]==parts[1].catalog.count)from=0;
        else{int order=catalog_order(parts[0].catalog.items+index[0],parts[1].catalog.items+index[1]);if(!order)return -1;from=order>0;}
        struct bw_native_library *part=parts+from;size_t at=index[from]++;
        out->catalog.items[i]=part->catalog.items[at];
        if(part->covers){out->covers[i]=part->covers[at];memset(part->covers+at,0,sizeof *part->covers);}
    }
    out->cover_bytes=parts[0].cover_bytes+parts[1].cover_bytes;
    out->requests=parts[0].requests+parts[1].requests;out->entries=parts[0].entries+parts[1].entries;
    out->content_bytes=parts[0].content_bytes+parts[1].content_bytes;return 0;
}
static int discover(void *opaque,struct bw_share_config *configured,struct bw_native_library *out){
    struct bw_native_library parts[2]={{0},{0}};struct bw_share_config connection={0};int result=-1;
    for(unsigned i=0;i<2;i++){
        if(i&&!*configured->share2)break;
        if(bw_share_select(&connection,configured,i?configured->share2:NULL)||
           discover_one(opaque,&connection,parts+i,BW_NATIVE_COVER_LIMIT-parts[0].cover_bytes)||!valid_library(parts+i))goto done;
    }
    result=library_merge(out,parts);
done:
    bw_share_clear(&connection);bw_share_clear(configured);
    for(unsigned i=0;i<2;i++)bw_native_library_destroy(parts+i);return result;
}
static int settings_idle(void *opaque){
    struct bw_native_backend *p=opaque;int (*guard)(void *,uint32_t)=NULL;void *owner=NULL;uint32_t operation=0;int eligible=0;
    if(pthread_mutex_lock(&p->lock))return -1;
    guard=p->settings_idle;owner=p->settings_idle_owner;operation=p->setup_operation;eligible=p->active&&p->worker_kind==WORK_SETUP&&operation;
    pthread_mutex_unlock(&p->lock);
    return guard?(eligible?guard(owner,operation):-1):p->ops.idle(p->opaque);
}
static void *scan(void *opaque){
    struct bw_native_backend *p=opaque;struct bw_share_config config={0},after={0};struct bw_native_library next={0},old={0};
#ifdef BW_HOME_STARTUP
    unsigned char observed_ready[BW_CATALOG_MAX_ITEMS]={0};
#endif
    struct bw_setup_generation before_generation={0},after_generation={0};uint8_t before_digest[32],after_digest[32];
    int phase=BACKEND_NEEDS_SETUP,error=SETUP_NO_ERROR,setup=p->worker_kind==WORK_SETUP,success=0;
    if(setup){
        if((!p->pending.unchanged||p->settings_idle)&&settings_idle(p)){error=SETUP_IDLE_ERROR;phase=BACKEND_FAILED;goto done;}
        config=p->pending.config;
    }else{
        int loaded=p->settings_enabled?p->ops.read_settings(p->opaque,&config,&before_generation):p->ops.load(p->opaque,&config);
        if(loaded||!valid_config(&config))goto done;
        cp_sha_bytes(&config,sizeof config,before_digest);
        if(!p->settings_enabled){pthread_mutex_lock(&p->lock);memcpy(p->server,config.server,sizeof p->server);memcpy(p->share,config.share,sizeof p->share);memcpy(p->share2,config.share2,sizeof p->share2);pthread_mutex_unlock(&p->lock);}
    }
    phase=BACKEND_FAILED;
    if(p->ops.discover(p->opaque,&config,&next)||!valid_library(&next)){error=SETUP_CONNECTION_ERROR;goto done;}
    if(setup){
        if(p->settings_idle&&settings_idle(p)){error=SETUP_IDLE_ERROR;goto done;}
        if(!p->pending.unchanged){
            pthread_mutex_lock(&p->lock);p->setup_phase=SETUP_SAVING;pthread_mutex_unlock(&p->lock);struct bw_setup_saved saved={0};
            int stored=p->ops.save_settings(p->opaque,&p->pending.config,&p->pending.expected,p->pending.nonce,settings_idle,p,&saved);
            if(stored||!saved.published||!saved.verified){error=stored==BW_SETUP_SAVE_UNCONFIRMED||saved.published?SETUP_UNCONFIRMED_ERROR:stored==BW_SETUP_SAVE_STALE?SETUP_STALE_ERROR:stored==BW_SETUP_SAVE_BUSY?SETUP_IDLE_ERROR:SETUP_SAVE_ERROR;goto done;}
        }
        if(p->ops.read_settings(p->opaque,&after,&after_generation)!=BW_SETUP_VALID||!config_equal(&after,&p->pending.config)||
            (p->pending.unchanged&&!bw_native_setup_generation_equal(&p->pending.expected,&after_generation))){error=p->pending.unchanged?SETUP_STALE_ERROR:SETUP_UNCONFIRMED_ERROR;goto done;}
    }else{
        phase=BACKEND_CONFIG_CHANGED;
        int loaded=p->settings_enabled?p->ops.read_settings(p->opaque,&after,&after_generation):p->ops.load(p->opaque,&after);
        if(loaded||!valid_config(&after))goto done;
        cp_sha_bytes(&after,sizeof after,after_digest);
        if(memcmp(before_digest,after_digest,sizeof before_digest)||(p->settings_enabled&&!bw_native_setup_generation_equal(&before_generation,&after_generation)))goto done;
    }
    if(p->settings_enabled&&p->ops.inspect&&next.catalog.count){
        next.prepared=calloc(next.catalog.count,sizeof *next.prepared);if(!next.prepared){phase=BACKEND_FAILED;goto done;}
        for(size_t i=0;i<next.catalog.count;i++){
            const struct bw_catalog_item *item=&next.catalog.items[i];if(!bw_source_catalog_available(item))continue;struct bw_provider_binding binding;
            if(bw_source_catalog_binding(&binding,&after,item)||p->ops.inspect(p->opaque,&binding,&next.prepared[i]))next.prepared[i].state=BW_PREPARED_UNKNOWN;
#ifdef BW_HOME_STARTUP
            else if(p->ops.prepare==prepare&&i<BW_CATALOG_MAX_ITEMS&&next.prepared[i].state==BW_PREPARED_VALID&&
                !home_ready_observe(p,&binding,next.prepared+i,&after_generation))observed_ready[i]=1;
#endif
        }
        struct bw_share_config checked={0};struct bw_setup_generation check_generation={0};
        int changed=p->ops.read_settings(p->opaque,&checked,&check_generation)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&after_generation,&check_generation);
        bw_share_clear(&checked);if(changed){phase=BACKEND_CONFIG_CHANGED;goto done;}
    }
    if(removal_cache(p,&next,&after)){phase=BACKEND_FAILED;goto done;}
    phase=BACKEND_READY;success=1;
done:
    pthread_mutex_lock(&p->lock);p->phase=phase;
    if(success){
        old=p->library;p->library=next;memset(&next,0,sizeof next);p->generation++;
#ifdef BW_HOME_STARTUP
        /* Qualification is freshly re-observed for this exact new binding,
         * receipt and settings generation; catalog indices are not authority. */
        memcpy(p->home_ready,observed_ready,sizeof p->home_ready);
#endif
        p->stale=0;
        if(p->settings_enabled)settings_adopt(p,BW_SETUP_VALID,&after,&after_generation,0);
        if(setup){p->setup_phase=SETUP_SAVED;p->setup_checked_only=p->pending.unchanged;p->setup_error=SETUP_NO_ERROR;}
        else{p->setup_phase=SETUP_IDLE;p->setup_error=SETUP_NO_ERROR;}
        p->setup_blocked=0;p->preparation_blocked=0;
    }
    else p->stale=p->library.catalog.count>0;
    if(setup&&!success){p->setup_error=error?error:SETUP_CONNECTION_ERROR;p->setup_phase=error==SETUP_UNCONFIRMED_ERROR?SETUP_UNCONFIRMED:SETUP_FAILED;if(error==SETUP_UNCONFIRMED_ERROR)p->setup_blocked=1;}
    bw_native_sensitive_clear(&p->pending,sizeof p->pending);bw_share_clear(&config);bw_share_clear(&after);
    p->active=0;pthread_mutex_unlock(&p->lock);bw_native_library_destroy(&next);bw_native_library_destroy(&old);return NULL;
}
struct bw_native_backend *bw_native_backend_create(const struct bw_native_backend_ops *ops,void *opaque){
    struct bw_native_backend *p=calloc(1,sizeof *p);if(!p)return NULL;
    if(pthread_mutex_init(&p->lock,NULL)){free(p);return NULL;}
    if(pthread_mutex_init(&p->dispatch_lock,NULL)){pthread_mutex_destroy(&p->lock);free(p);return NULL;}
    p->jobs=bw_native_jobs_create();
    if(!p->jobs){pthread_mutex_destroy(&p->dispatch_lock);pthread_mutex_destroy(&p->lock);free(p);return NULL;}
    p->ops=(struct bw_native_backend_ops){.load=ops&&ops->load?ops->load:load,.discover=ops&&ops->discover?ops->discover:discover,
        .read_settings=ops?ops->read_settings:read_settings,.save_settings=ops?ops->save_settings:save_settings,
        .idle=ops&&ops->idle?ops->idle:bw_native_setup_idle,.nonce=ops&&ops->nonce?ops->nonce:nonce};p->opaque=opaque;
    p->ops.prepare=ops?(ops->prepare?ops->prepare:ops->native_preparation?prepare:NULL):prepare;
    p->ops.inspect=ops?(ops->inspect?ops->inspect:ops->native_preparation?inspected:NULL):inspected;
    p->ops.play=ops?ops->play:NULL;p->ops.close=ops?ops->close:NULL;
    p->ops.removal_probe=ops?ops->removal_probe:NULL;p->ops.uninstall=ops?ops->uninstall:NULL;p->ops.restore=ops?ops->restore:NULL;
#ifdef BW_NATIVE_PLAYBACK
    if(!ops){p->ops.play=play;p->ops.close=close_game;}
#ifdef BW_NATIVE_PRELOAD
    if(!ops){(void)registration_pending_refresh(p);p->ops.removal_probe=default_removal_probe;p->ops.uninstall=default_uninstall;p->ops.restore=default_restore;}
#endif
#endif
    p->settings_status=BW_SETUP_UNSAFE;p->settings_enabled=p->ops.read_settings&&p->ops.save_settings;
    if(p->settings_enabled&&(p->ops.nonce(p->opaque,p->setup_token)||!nonce_valid(p->setup_token))){p->settings_enabled=0;memset(p->setup_token,0,sizeof p->setup_token);}return p;
}
static int refresh_unlocked(struct bw_native_backend *p){
    if(!p||pthread_mutex_lock(&p->lock))return -1;if(p->active){pthread_mutex_unlock(&p->lock);return 1;}
    if(p->joinable){if(pthread_join(p->worker,NULL)){pthread_mutex_unlock(&p->lock);return -1;}p->joinable=0;}
    p->active=1;p->worker_kind=WORK_SCAN;p->phase=BACKEND_SCANNING;p->stale=p->library.catalog.count>0;
    int result=worker_start(&p->worker,scan,p);
    if(result){p->active=0;p->phase=BACKEND_FAILED;}else p->joinable=1;pthread_mutex_unlock(&p->lock);return result?-1:0;
}
int bw_native_backend_refresh(struct bw_native_backend *p){
    if(!p||pthread_mutex_lock(&p->dispatch_lock))return -1;
    int result=*p->external_nonce?1:refresh_unlocked(p);pthread_mutex_unlock(&p->dispatch_lock);return result;
}
static int settings_idle_change(struct bw_native_backend *p,int (*guard)(void *,uint32_t),void *owner,int install){
    if(!p||!guard||pthread_mutex_lock(&p->dispatch_lock))return -1;
    if(pthread_mutex_lock(&p->lock)){pthread_mutex_unlock(&p->dispatch_lock);return -1;}
    int result=0;
    if(p->active||*p->external_nonce)result=1;
    else if(p->settings_idle&&(p->settings_idle!=guard||p->settings_idle_owner!=owner))result=-1;
    else {p->settings_idle=install?guard:NULL;p->settings_idle_owner=install?owner:NULL;}
    pthread_mutex_unlock(&p->lock);pthread_mutex_unlock(&p->dispatch_lock);return result;
}
int bw_native_backend_set_settings_idle(struct bw_native_backend *p,int (*guard)(void *,uint32_t),void *owner){return settings_idle_change(p,guard,owner,1);}
int bw_native_backend_clear_settings_idle(struct bw_native_backend *p,int (*guard)(void *,uint32_t),void *owner){return settings_idle_change(p,guard,owner,0);}
static int preparation_idle_change(struct bw_native_backend *p,int (*guard)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *owner,int install){
    if(!p||!guard||!owner||pthread_mutex_lock(&p->dispatch_lock))return -1;
    if(pthread_mutex_lock(&p->lock)){pthread_mutex_unlock(&p->dispatch_lock);return -1;}
    int result=0;
    if(p->active||*p->external_nonce)result=1;
    else if(p->preparation_idle&&(p->preparation_idle!=guard||p->preparation_idle_owner!=owner))result=-1;
    else {p->preparation_idle=install?guard:NULL;p->preparation_idle_owner=install?owner:NULL;}
    pthread_mutex_unlock(&p->lock);pthread_mutex_unlock(&p->dispatch_lock);return result;
}
int bw_native_backend_set_preparation_idle(struct bw_native_backend *p,int (*guard)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *owner){return preparation_idle_change(p,guard,owner,1);}
int bw_native_backend_clear_preparation_idle(struct bw_native_backend *p,int (*guard)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *owner){return preparation_idle_change(p,guard,owner,0);}
int bw_native_backend_busy(struct bw_native_backend *p){if(!p||pthread_mutex_lock(&p->lock))return -1;int result=p->active||*p->external_nonce;pthread_mutex_unlock(&p->lock);return result;}
int bw_native_backend_destroy(struct bw_native_backend *p){
    if(!p||pthread_mutex_lock(&p->lock))return -1;if(p->active||*p->external_nonce||p->settings_idle||p->preparation_idle){pthread_mutex_unlock(&p->lock);return 1;}
#ifdef BW_HOME_STARTUP
    if(p->home.retained){pthread_mutex_unlock(&p->lock);return 1;}
#endif
    if(p->joinable&&pthread_join(p->worker,NULL)){pthread_mutex_unlock(&p->lock);return -1;}
    pthread_mutex_unlock(&p->lock);if(bw_native_jobs_destroy(p->jobs))return -1;
    bw_native_library_destroy(&p->library);pthread_mutex_destroy(&p->dispatch_lock);pthread_mutex_destroy(&p->lock);bw_native_sensitive_clear(p,sizeof *p);free(p);return 0;
}
static const char *phase_name(int phase){static const char *names[]={"initial","scanning","ready","needs_setup","error","configuration_changed"};return names[phase];}
static const char *phase_message(int phase){
    switch(phase){
        case BACKEND_SCANNING:return "Reading the library directly from the PC share.";
        case BACKEND_READY:return "Library loaded from the PC. Prepare startup files before checking playback support.";
        case BACKEND_NEEDS_SETUP:return "Set up the saved PC share connection to load the library.";
        case BACKEND_CONFIG_CHANGED:return "The saved PC connection changed during discovery. Refresh to read the current source.";
        case BACKEND_FAILED:return "The PC library could not be read. Check the PC and its shared folder, then refresh.";
        default:return "Refresh to read the PC library.";
    }
}
static const char *setup_phase_name(int phase){static const char *names[]={"idle","checking","saving","saved","error","unconfirmed"};return names[phase];}
static const char *setup_message(const struct bw_native_backend *p){
    if(p->setup_phase==SETUP_CHECKING)return "Checking the PC share before saving.";
    if(p->setup_phase==SETUP_SAVING)return "Saving the checked connection settings.";
    if(p->setup_phase==SETUP_SAVED)return p->setup_checked_only?"PC connection checked. Existing settings were kept.":"PC connection saved and checked.";
    switch(p->setup_error){
        case SETUP_CONNECTION_ERROR:return "The new PC connection failed. Check the share and sign-in details; saved settings were not replaced.";
        case SETUP_STALE_ERROR:return "Connection settings changed during the check. Refresh before saving again.";
        case SETUP_IDLE_ERROR:return "Close running games and check console status before changing the connection.";
        case SETUP_SAVE_ERROR:return "The checked connection could not be saved. Check Blackwire's data folder before retrying.";
        case SETUP_UNCONFIRMED_ERROR:return "The settings save needs a fresh check. Refresh the connection before trying again.";
        case SETUP_START_ERROR:return "The connection check could not start. Refresh status before retrying.";
        default:break;
    }
    if(p->settings_enabled&&p->settings_known&&p->settings_status<0)return "Connection settings are linked or unavailable. Check Blackwire's data folder.";
    if(p->settings_enabled&&p->phase==BACKEND_NEEDS_SETUP)return "Enter the PC address, shared-folder name and sign-in details, then save and check.";
    return phase_message(p->phase);
}
/* Format validation is separate from this build's reader capability. */
static const char *reader_update_reason(const struct bw_native_prepared *prepared){
#ifdef BW_NATIVE_PRELOAD
    if(prepared&&prepared->state==BW_PREPARED_VALID){
        int state=bw_preload_bundle_upgrade_workspace(backend_data_root,prepared->game);
        if(state>0)return "A previous reader update is unresolved. Play and Send are blocked; the startup files were kept.";
        if(state<0)return "The reader update state could not be checked. Play and Send are blocked; the startup files were kept.";
    }
#else
    (void)prepared;
#endif
    return NULL;
}
static const char *source_build_reason(const struct bw_catalog_item *item){
#ifdef BW_NATIVE_PRELOAD
    if(bw_source_catalog_kind(item)==BW_PROVIDER_CONTAINER&&item->state!=BW_CATALOG_CONTAINER_READY)
        return "This compressed image format is unsupported or could not be verified.";
#else
    (void)item;
#endif
    return NULL;
}
/* Cached positive preparations may outlive a manual folder deletion. This
 * bounded metadata observation grants no file or registration authority. The
 * ordinary Send inspector remains responsible for all retained durable state.
 * Caller owns lock; no worker or external mutation reservation may be live. */
static int prepared_parent_same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&
        a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void prepared_absence_poll(struct bw_native_backend *p){
    if(p->ops.prepare!=prepare||p->active||p->external_nonce[0]||p->control_release_unknown||
       p->phase!=BACKEND_READY||p->stale||p->preparation_blocked||p->setup_blocked||
       !p->settings_enabled||!p->settings_known||p->settings_status!=BW_SETUP_VALID||
       !p->library.prepared||p->generation==UINT64_MAX)return;
#ifdef BW_HOME_STARTUP
    if(p->home.active)return;
#endif
    char parent[1152],path[1280],resolved[PATH_MAX];struct stat before,after,named;
    int length=snprintf(parent,sizeof parent,"%s/games",backend_data_root);
    if(length<0||(size_t)length>=sizeof parent||!realpath(parent,resolved)||strcmp(parent,resolved)||
       lstat(parent,&before)||!S_ISDIR(before.st_mode))return;
    struct bw_share_config connection={0};
    snprintf(connection.server,sizeof connection.server,"%s",p->server);
    snprintf(connection.share,sizeof connection.share,"%s",p->share);
    snprintf(connection.share2,sizeof connection.share2,"%s",p->share2);
    unsigned changed=0;
    for(size_t i=0;i<p->library.catalog.count;i++){
        const struct bw_catalog_item *item=p->library.catalog.items+i;
        struct bw_native_prepared *cached=p->library.prepared+i;struct bw_provider_binding binding={0};
        if(!bw_source_catalog_available(item)||
           cached->state!=BW_PREPARED_VALID||cached->prepared_policy||!nonce_valid(cached->run)||
           cached->retirement!=BW_RETIRE_NONE||removed_at(p,i)||registration_waiting(p,item->title_id)||
           bw_source_catalog_binding(&binding,&connection,item)||
           !memchr(cached->game,0,sizeof cached->game)||strcmp(cached->game,binding.binding.game)||
           (p->removing.unknown&&!strcmp(p->removing.selected.identity.binding.game,cached->game)))continue;
        length=snprintf(path,sizeof path,"%s/%s",parent,binding.binding.game);
        if(length<0||(size_t)length>=sizeof path||!lstat(path,&named)||(errno!=ENOENT&&errno!=ENOTDIR))continue;
        /* An intermediate replacement, link, inaccessible parent or raced
         * reappearance cannot turn an unknown observation into absence. */
        if(lstat(parent,&after)||!S_ISDIR(after.st_mode)||!prepared_parent_same(&before,&after)||
           !lstat(path,&named)||(errno!=ENOENT&&errno!=ENOTDIR)||
           lstat(parent,&after)||!prepared_parent_same(&before,&after))continue;
        memset(cached,0,sizeof *cached);cached->state=BW_PREPARED_ABSENT;
        if(p->library.removal)memset(p->library.removal+i,0,sizeof *p->library.removal);
#ifdef BW_HOME_STARTUP
        if(i<BW_CATALOG_MAX_ITEMS)p->home_ready[i]=0;
        if(!strcmp(p->home_setup_game,binding.binding.game)){
            memset(p->home_setup_game,0,sizeof p->home_setup_game);
            memset(p->home_setup_reason,0,sizeof p->home_setup_reason);
        }
#endif
        changed=1;
    }
    if(changed)p->generation++;
}
static int state(struct bw_native_backend *p,struct bw_native_response *out){
    struct bw_json json={.maximum=16u*1024u*1024u};char *jobs=NULL;size_t job_size=0;pthread_mutex_lock(&p->lock);
    settings_sync(p);
    prepared_absence_poll(p);
    size_t visible_count=p->settings_enabled&&p->settings_known&&
        (p->settings_status==BW_SETUP_MISSING||p->settings_status==BW_SETUP_INVALID)?0:p->library.catalog.count;
    if(bw_native_jobs_json(p->jobs,&jobs,&job_size)){pthread_mutex_unlock(&p->lock);return -1;}
    bw_json_text(&json,"{\"backend\":\"native\",\"settings\":{\"gamesFolder\":\"\",\"ps5Address\":\"\",\"server\":");bw_json_string(&json,p->server);
    bw_json_text(&json,",\"share\":");bw_json_string(&json,p->share);bw_json_text(&json,",\"share2\":");bw_json_string(&json,p->share2);bw_json_text(&json,",\"user\":");bw_json_string(&json,p->user);
    bw_json_format(&json,",\"passwordSaved\":%s},\"pc\":{\"address\":",p->password_saved?"true":"false");bw_json_string(&json,p->server);
    bw_json_format(&json,"},\"share\":{\"shared\":%s,\"name\":",p->phase==BACKEND_READY?"true":"false");bw_json_string(&json,p->share);
    bw_json_text(&json,"},\"ps5\":{\"reachable\":true,\"fileManager\":false,\"payloadManager\":false},\"blackwire\":\"running\",\"stream\":{\"phase\":");
    bw_json_string(&json,*p->playing.phase?p->playing.phase:"unverified");bw_json_text(&json,",\"message\":");bw_json_string(&json,*p->playing.message?p->playing.message:"Game session status has not been checked.");
    bw_json_text(&json,",\"game\":");bw_json_string(&json,p->playing.selection.identity.binding.source);bw_json_text(&json,",\"runId\":");bw_json_string(&json,p->playing.selection.run);
    bw_json_text(&json,",\"sourceBinding\":");bw_json_string(&json,p->playing.selection.identity.binding.game);
    bw_json_text(&json,",\"elapsedMs\":");
    double elapsed=p->playing.timing_started?(p->playing.timing_finished?p->playing.finished:now())-p->playing.started:-1;
    if(elapsed>=0&&elapsed<=9007199254740.0)bw_json_format(&json,"%llu",(unsigned long long)(elapsed*1000.0));else bw_json_text(&json,"null");
    bw_json_text(&json,",\"lastFailure\":");
    if(*p->last_failure.message){
        bw_json_text(&json,"{\"runId\":");bw_json_string(&json,p->last_failure.run);
        bw_json_text(&json,",\"game\":");bw_json_string(&json,p->last_failure.game);
        bw_json_text(&json,",\"message\":");bw_json_string(&json,p->last_failure.message);bw_json_text(&json,"}");
    }else bw_json_text(&json,"null");
    int early=0;
#ifdef BW_NATIVE_PRELOAD
    early=p->playing.selection.identity.kind==BW_PROVIDER_FOLDER||p->playing.selection.identity.kind==BW_PROVIDER_CONTAINER;
#endif
    int live_controller=p->active&&p->worker_kind==WORK_PLAY;
    bw_json_format(&json,",\"gamePid\":%d,\"readerPid\":%d,\"controllerPid\":%d,\"canClose\":%s,\"readerMode\":",
        p->playing.pid>0?p->playing.pid:0,live_controller?(early?(p->playing.pid>0?p->playing.pid:0):(int)getpid()):0,
        live_controller?(int)getpid():0,live_controller&&p->ops.close&&!p->playing.close_requested?"true":"false");
    if(early){
        /* No game-to-controller data witness is integrated yet. Preserve an
         * explicit unknown instead of converting a missing counter to 0 B. */
        bw_json_text(&json,"\"preloaded\",\"dataCountersObserved\":false,\"networkBytes\":null,\"cacheHitBytes\":null,\"stageHighWater\":0,\"cacheBytes\":null,\"memoryCacheBudget\":67108864,\"stageBytes\":0");
    }else bw_json_format(&json,"\"late-native\",\"dataCountersObserved\":true,\"networkBytes\":%llu,\"cacheHitBytes\":%llu,\"stageHighWater\":%llu,\"cacheBytes\":67108864,\"stageBytes\":268435456",
        (unsigned long long)p->playing.network_bytes,(unsigned long long)p->playing.hit_bytes,(unsigned long long)p->playing.high_water);
    bw_json_text(&json,"},");removal_state_json(&json,p);bw_json_text(&json,",\"setup\":{\"message\":");bw_json_string(&json,setup_message(p));
    int editable=p->settings_enabled&&p->settings_known&&p->settings_status>=0&&!p->active&&!p->setup_blocked;
    bw_json_format(&json,",\"editable\":%s,\"supported\":%s,\"busy\":%s,\"configured\":%s,\"hasSavedCredentials\":%s,\"revision\":%u,\"operation\":%u,\"phase\":",editable?"true":"false",p->settings_enabled?"true":"false",p->active&&p->worker_kind==WORK_SETUP?"true":"false",
        p->settings_enabled?p->settings_known&&p->settings_status==BW_SETUP_VALID?"true":"false":*p->server&&*p->share?"true":"false",p->settings_enabled&&p->settings_known&&p->settings_status==BW_SETUP_VALID?"true":"false",p->settings_revision,p->setup_operation);
    bw_json_string(&json,setup_phase_name(p->setup_phase));bw_json_text(&json,",\"token\":");bw_json_string(&json,p->setup_token);
    bw_json_text(&json,",\"checks\":[]},\"library\":{\"phase\":");bw_json_string(&json,phase_name(p->phase));
    bw_json_format(&json,",\"generation\":%llu,\"stale\":%s,\"items\":%zu,\"collections\":%u,\"entries\":%u,\"unsearched\":%u,\"coverBytes\":%zu,\"coverLimit\":%u,\"contentBytes\":%llu,\"requests\":%u},\"games\":[",
        (unsigned long long)p->generation,p->stale?"true":"false",visible_count,p->library.catalog.collections,p->library.catalog.entries,p->library.catalog.unsearched,
        p->library.cover_bytes,BW_NATIVE_COVER_LIMIT,(unsigned long long)p->library.content_bytes,p->library.requests);
    struct bw_share_config public_binding_connection={0};
    snprintf(public_binding_connection.server,sizeof public_binding_connection.server,"%s",p->server);
    snprintf(public_binding_connection.share,sizeof public_binding_connection.share,"%s",p->share);
    snprintf(public_binding_connection.share2,sizeof public_binding_connection.share2,"%s",p->share2);
    for(size_t i=0;i<visible_count;i++){
        const struct bw_catalog_item *item=&p->library.catalog.items[i];const struct bw_native_prepared *prepared=p->library.prepared&&bw_source_catalog_available(item)?&p->library.prepared[i]:NULL;
        int owned_prepared=prepared&&prepared->state==BW_PREPARED_VALID;
        int needs_revalidation=revalidation_required(p,prepared);
        int present=owned_prepared&&!prepared->startup_upgrade_required&&!needs_revalidation;
        const char *build_reason=source_build_reason(item),*reader_reason=reader_update_reason(prepared);
        int prepare_supported=!build_reason&&p->settings_enabled&&p->ops.prepare&&bw_source_catalog_available(item);
        int cleanup_waiting=registration_waiting(p,item->title_id),removed=removed_at(p,i);
        int registration_ready=1;
#ifdef BW_HOME_STARTUP
        registration_ready=i<BW_CATALOG_MAX_ITEMS&&p->home_ready[i]==1;
#endif
        int can_prepare=prepare_supported&&!reader_reason&&!cleanup_waiting&&!removed&&(!present||!registration_ready)&&p->phase==BACKEND_READY&&!p->stale&&!p->active&&!p->control_release_unknown&&!p->preparation_blocked&&!p->setup_blocked&&p->settings_status==BW_SETUP_VALID&&
            (!prepared||prepared->state==BW_PREPARED_ABSENT||prepared->state==BW_PREPARED_VALID);
        int startup_ready=!build_reason&&!reader_reason&&!cleanup_waiting&&!removed&&p->ops.play&&p->ops.close&&present&&registration_ready&&p->phase==BACKEND_READY&&!p->stale&&!p->control_release_unknown&&!p->setup_blocked&&p->settings_status==BW_SETUP_VALID;
        int can_play=startup_ready&&!reader_reason&&!p->active;
        int can_start=can_play||(!needs_revalidation&&can_prepare&&p->ops.play&&p->ops.close);
        const char *reason=bw_source_catalog_available(item)?"Preparation is unavailable in this development build.":
            item->state==BW_CATALOG_CONTAINER?"This source image is unsupported or its logical game metadata is invalid.":item->state==BW_CATALOG_BAD_PARAM?"The game metadata is missing or invalid.":"The source is linked or unavailable.";
        if(prepare_supported)reason=can_prepare?"":present?"Startup files are already ready. Choose Play.":p->active?"Wait for the current operation to finish.":p->preparation_blocked?"Refresh to check the unconfirmed preparation before trying again.":
            prepared&&prepared->state==BW_PREPARED_INVALID?"The Blackwire console copy needs repair before it can be prepared again.":"Refresh the library and console copy status before preparing.";
        if(build_reason)reason=build_reason;
        if(reader_reason)reason=reader_reason;
        if(cleanup_waiting)reason="Registration cleanup is pending or needs attention. Startup files are retained; Send does not repeat cleanup.";
        bw_json_text(&json,i?",{\"name\":":"{\"name\":");bw_json_string(&json,item->source);bw_json_text(&json,",\"source\":");bw_json_string(&json,item->source);
        struct bw_provider_binding public_binding={0};
        bw_json_text(&json,",\"sourceBinding\":");bw_json_string(&json,bw_source_catalog_available(item)&&
            !bw_source_catalog_binding(&public_binding,&public_binding_connection,item)?public_binding.binding.game:"");
        bw_json_text(&json,",\"sourceShare\":");bw_json_string(&json,*item->source_share?item->source_share:p->share);
        bw_json_text(&json,",\"title\":");bw_json_string(&json,*item->title?item->title:item->name);bw_json_text(&json,",\"titleId\":");bw_json_string(&json,item->title_id);
        bw_json_text(&json,",\"sourceKind\":");bw_json_string(&json,bw_source_catalog_kind(item)==BW_PROVIDER_CONTAINER?"container":"folder");
        bw_json_format(&json,",\"sourcePolicy\":%u,\"formatValidated\":%s",bw_source_catalog_available(item)?BW_PROVIDER_POLICY_V1:0,item->state==BW_CATALOG_CONTAINER_READY?"true":"false");
        bw_json_text(&json,",\"containerFormat\":");bw_json_string(&json,bw_source_catalog_format(item));
        char image_generation[65]={0};if(item->state==BW_CATALOG_CONTAINER_READY)for(unsigned j=0;j<32;j++)snprintf(image_generation+2*j,3,"%02x",item->image_sha256[j]);
        bw_json_text(&json,",\"imageGenerationSha256\":");bw_json_string(&json,image_generation);
        
        bw_json_text(&json,",\"status\":");bw_json_string(&json,bw_source_catalog_status(item));
        bw_json_format(&json,",\"bootBytes\":%llu,\"containerBytes\":%llu",(unsigned long long)bw_source_catalog_boot_bytes(item),(unsigned long long)item->container_bytes);
        bw_json_format(&json,",\"cover\":%s,\"sizeKnown\":%s,\"onConsole\":%s,\"prepared\":%s,\"canPrepare\":%s,\"canPlay\":%s,\"canStart\":%s,\"startupReady\":%s,\"preparationReason\":",cached_cover(&p->library,i)?"true":"false",present?"true":"false",
            owned_prepared?"true":prepared&&prepared->state==BW_PREPARED_ABSENT?"false":"null",present?"true":"false",can_prepare?"true":"false",can_play?"true":"false",can_start?"true":"false",startup_ready?"true":"false");bw_json_string(&json,reason);
        bw_json_format(&json,",\"startupUpgradeRequired\":%s",owned_prepared&&prepared->startup_upgrade_required?"true":"false");
        bw_json_format(&json,",\"registrationCleanupPending\":%s",cleanup_waiting?"true":"false");
        bw_json_format(&json,",\"setupOnly\":%s",present&&!registration_ready?"true":"false");
#ifdef BW_HOME_STARTUP
        unsigned home_ready=i<BW_CATALOG_MAX_ITEMS?p->home_ready[i]:0;
        bw_json_format(&json,",\"dashboardLaunchReady\":%s",startup_ready&&home_ready==1?"true":home_ready==2||!startup_ready?"false":"null");
        bw_json_text(&json,",\"dashboardLaunchReason\":");
        bw_json_string(&json,prepared&&!strcmp(prepared->game,p->home_setup_game)?p->home_setup_reason:home_ready==1?"":
            "Start this prepared game once with Blackwire Play to qualify or update its dashboard reader without resending files.");
#endif
        const struct bw_game_uninstall_capability *cap=p->library.removal?p->library.removal+i:NULL;
        int removal_eligible=!build_reason&&!cleanup_waiting&&p->settings_status==BW_SETUP_VALID&&p->phase==BACKEND_READY&&!p->stale&&bw_source_catalog_available(item);
        bw_json_format(&json,",\"canUninstall\":%s,\"removed\":%s,\"canRestore\":%s,\"uninstallReason\":",removal_eligible&&cap&&cap->can_uninstall&&p->ops.uninstall?"true":"false",removed?"true":"false",removal_eligible&&cap&&cap->can_restore&&p->ops.restore?"true":"false");
        bw_json_string(&json,cap&&cap->reason?cap->reason:"Exact owned-copy removal is unavailable.");bw_json_text(&json,",\"restoreReason\":");bw_json_string(&json,cap&&cap->reason?cap->reason:"No durable removed source is available.");
        if(owned_prepared){
            bw_json_format(&json,",\"sendBytes\":%llu,\"stayBytes\":%llu,\"consoleAllocatedBytes\":%llu,\"storageVerifiedAt\":\"preparation\",\"preparationBytesVerified\":%s,\"testStatus\":",
                (unsigned long long)prepared->local_bytes,(unsigned long long)prepared->remote_bytes,(unsigned long long)prepared->allocated_bytes,prepared->bytes_verified?"true":"false");
            bw_json_string(&json,prepared->bytes_verified?"Startup files verified; gameplay is unverified.":"Prepared metadata checked; startup bytes need verification before launch.");
        }
        bw_json_text(&json,",\"playReason\":");bw_json_string(&json,cleanup_waiting?"Registration cleanup is pending or needs attention; no operation will be repeated.":reader_reason?reader_reason:build_reason?build_reason:can_play?"":!p->ops.play?"Playback is unavailable in this development build.":p->active?"Wait for the current operation to finish.":!present?"Prepare startup files before playing.":"Refresh the PC library and connection before playing.");
        bw_json_text(&json,",\"scanMessage\":\"Sizes are checked during preparation.\"}");
    }
    bw_json_text(&json,"],\"jobs\":");bw_json_append(&json,jobs,job_size);bw_json_text(&json,"}");pthread_mutex_unlock(&p->lock);free(jobs);
    if(json.bad){free(json.bytes);return -1;}*out=(struct bw_native_response){200,"application/json; charset=utf-8",json.bytes,json.used};return 0;
}
static int text(struct bw_native_response *out,int status,const char *body){
    size_t size=strlen(body);char *memory=malloc(size+1);if(!memory)return -1;memcpy(memory,body,size+1);*out=(struct bw_native_response){status,"application/json; charset=utf-8",memory,size};return 0;
}
static int prepare_current(void *opaque){
    struct bw_native_backend *p=opaque;struct bw_setup_generation expected;
    pthread_mutex_lock(&p->lock);int active=p->active&&p->worker_kind==WORK_PREPARE&&!p->control_release_unknown;expected=p->sending.expected;pthread_mutex_unlock(&p->lock);
    if(!active)return -1;struct bw_share_config config={0};struct bw_setup_generation generation={0};
    int changed=p->ops.read_settings(p->opaque,&config,&generation)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&expected,&generation);bw_share_clear(&config);
    if(changed){pthread_mutex_lock(&p->lock);p->preparation_changed=1;p->stale=1;p->phase=BACKEND_CONFIG_CHANGED;pthread_mutex_unlock(&p->lock);return -1;}
    return prepare_authority(p,BW_PREPARE_IO);
}
static int prepare_authority(struct bw_native_backend *p,enum bw_prepare_guard_point point){
    char operation[33],run[33];int (*guard)(void *,const char[33],const char[33],enum bw_prepare_guard_point)=NULL;void *owner=NULL;
    if(!p||pthread_mutex_lock(&p->lock))return -1;
    int active=p->active&&p->worker_kind==WORK_PREPARE&&(point==BW_PREPARE_RELEASE||!p->control_release_unknown);memcpy(operation,p->sending.authority,33);memcpy(run,p->sending.selection.run,33);
    guard=p->preparation_idle;owner=p->preparation_idle_owner;pthread_mutex_unlock(&p->lock);
    if(!active)return -1;
    if(operation[0])return guard&&nonce_valid(operation)&&nonce_valid(run)?guard(owner,operation,run,point):-1;
    return point==BW_PREPARE_RELEASE?0:p->ops.idle(p->opaque);
}
static int prepared_valid(const struct bw_native_prepared *value,const struct bw_native_prepare_selection *selection){
    if(value->startup_upgrade_required)return 0;
#ifdef BW_NATIVE_PRELOAD
    if(selection->identity.kind==BW_PROVIDER_FOLDER&&value->prepared_policy)return 0;
#endif
    if(value->prepared_policy){
        uint64_t provenance_limit=value->prepared_policy==BW_BATCH_POLICY?BW_BATCH_PROVENANCE_LIMIT:BW_DERIVED_PROVENANCE_LIMIT;
        if((value->prepared_policy!=BW_DERIVED_PLAN_POLICY&&value->prepared_policy!=BW_BATCH_POLICY)||selection->identity.kind!=BW_PROVIDER_FOLDER||
           strnlen(value->prepared_component,sizeof value->prepared_component)!=74||
           memcmp(value->prepared_component,selection->identity.binding.title,9)||value->prepared_component[9]!='_'||
           !strcmp(value->prepared_component,selection->identity.binding.game)||!value->provenance_size||value->provenance_size>provenance_limit)return 0;
        for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",value->prepared_component[i]))return 0;
    }else{
        static const char component[75]={0};static const uint8_t hash[32]={0};
        if(memcmp(value->prepared_component,component,75)||memcmp(value->provenance_sha256,hash,32)||value->provenance_size)return 0;
    }
    return value->state==BW_PREPARED_VALID&&value->bytes_verified==1&&memchr(value->game,0,sizeof value->game)&&memchr(value->run,0,sizeof value->run)&&
        !strcmp(value->game,selection->identity.binding.game)&&!strcmp(value->run,selection->run)&&value->local_count&&value->local_count<=BW_MANIFEST_MAX_FILES&&
        value->stream_count<=BW_MANIFEST_MAX_FILES-value->local_count&&value->local_bytes<=UINT64_C(9007199254740991)&&
        value->remote_bytes<=UINT64_C(9007199254740991)-value->local_bytes&&value->allocated_bytes<=UINT64_C(9007199254740991)&&value->entries<=262144;
}
static void *prepare_worker(void *opaque){
    struct bw_native_backend *p=opaque;struct bw_share_config config=p->sending.config;struct bw_native_prepared prepared={0};
    int files_valid=0,keep_files=0;
#ifdef BW_HOME_STARTUP
    int setup_failed=0;
#endif
    int result=prepare_current(p)?BW_SEND_FAILED:0;
    if(!result){
        if(p->sending.setup_only)prepared=p->sending.retained;
        else result=p->ops.prepare(p->opaque,&config,&p->sending.selection,prepare_current,p,bw_native_job_observe,&p->sending.token,&prepared);
    }
    bw_share_clear(&config);
    if(!result&&((!p->sending.setup_only&&!prepared_valid(&prepared,&p->sending.selection))||prepare_current(p)))result=BW_SEND_UNCONFIRMED;
    if(!result)files_valid=1;
    if(!result&&prepare_authority(p,BW_PREPARE_CONTROL))result=BW_SEND_UNCONFIRMED;
#ifdef BW_HOME_STARTUP
    /* The existing provisioner verifies the same receipt/bundle and commits
     * ordinary registration without launching. File validity is separate:
     * setup failure keeps those files but must not finish as Ready. */
    if(!result&&p->ops.prepare==prepare){
        unsigned startup_invalid=0;
        (void)bw_native_job_setup_message(&p->sending.token,"Startup files kept. Verifying the reader and registering the PS5 dashboard icon.");
        setup_failed=home_prepare_provision(p,&prepared,&startup_invalid)!=0;
        if(startup_invalid){
            /* A metadata-only Refresh must not erase this exact receipt's
             * negative byte observation or admit another setup-only attempt. */
            struct bw_native_play_selection refused={.identity=p->sending.selection.identity,.prepared=prepared};
            pthread_mutex_lock(&p->lock);
            if(revalidation_remember(p,&refused))p->control_release_unknown=1;
            pthread_mutex_unlock(&p->lock);
            files_valid=0;result=BW_SEND_UNCONFIRMED;
        }
        if(!setup_failed&&p->sending.setup_only&&bw_native_job_reuse_prepared(&p->sending.token,prepared.local_count,prepared.local_bytes))result=BW_SEND_UNCONFIRMED;
    }
#endif
    if(prepare_authority(p,BW_PREPARE_RELEASE)&&!result)result=BW_SEND_UNCONFIRMED;
    struct bw_game_uninstall_capability removal={0};if(!result&&p->ops.removal_probe&&p->ops.removal_probe(p->opaque,&p->sending.selection.identity,&prepared,&removal)){memset(&removal,0,sizeof removal);removal.reason="Exact owned-copy removal metadata is unavailable.";}
    pthread_mutex_lock(&p->lock);
#ifdef BW_HOME_STARTUP
    if(p->ops.prepare==prepare){
        keep_files=files_valid;
        int registered=!result&&!setup_failed&&!p->control_release_unknown;
        const char *message=registered?"Startup files, reader and PS5 dashboard registration are ready.":
            files_valid?(*p->home_setup_reason&&!strcmp(p->home_setup_game,prepared.game)?p->home_setup_reason:"Startup files were kept. Reader or dashboard setup is unconfirmed; choose Send to finish setup without copying them again."):
            "Startup preparation did not finish. Inspect the source and console copy before retrying.";
        if(bw_native_job_finish_setup(&p->sending.token,files_valid,registered,message)){result=BW_SEND_UNCONFIRMED;registered=0;}
        if(!registered&&p->sending.item<BW_CATALOG_MAX_ITEMS)p->home_ready[p->sending.item]=2;
        if(p->control_release_unknown)p->preparation_blocked=1;
    }else
#endif
    if(!result&&bw_native_job_finish(&p->sending.token,BW_JOB_SUCCESS))result=BW_SEND_UNCONFIRMED;
    if(result){
        enum bw_native_job_failure failure=p->preparation_changed||result==BW_BACKEND_SOURCE_CHANGED?BW_JOB_SOURCE_CHANGED:result==BW_SEND_BUSY?BW_JOB_DESTINATION_CONFLICT:BW_JOB_FAILED;
        (void)bw_native_job_finish(&p->sending.token,failure);
        if(result==BW_SEND_UNCONFIRMED)p->preparation_blocked=1;
        if(result==BW_BACKEND_SOURCE_CHANGED){p->phase=BACKEND_CONFIG_CHANGED;p->stale=1;}
        if(p->library.prepared&&!keep_files)p->library.prepared[p->sending.item].state=BW_PREPARED_UNKNOWN;
    }
    if((!result||keep_files)&&files_valid&&p->library.prepared){
        p->library.prepared[p->sending.item]=prepared;if(p->library.removal)p->library.removal[p->sending.item]=removal;
        int clear_negative=!result&&!p->sending.setup_only;
#ifdef BW_HOME_STARTUP
        clear_negative=clear_negative&&!setup_failed&&!p->control_release_unknown;
#endif
        if(clear_negative)revalidation_clear(p,prepared.game);
    }
    bw_native_sensitive_clear(&p->sending,sizeof p->sending);p->active=0;pthread_mutex_unlock(&p->lock);return NULL;
}
static int send_owned(struct bw_native_backend *p,const char *authority,const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!p->ops.prepare||!p->settings_enabled)return text(out,409,"{\"error\":\"Native preparation is unavailable in this build\"}");
    unsigned difference=0;if(!token||strlen(token)!=32)return text(out,403,"{\"error\":\"Refresh Blackwire before preparing a game\"}");
    for(unsigned i=0;i<32;i++)difference|=(unsigned char)token[i]^(unsigned char)p->setup_token[i];if(difference)return text(out,403,"{\"error\":\"Refresh Blackwire before preparing a game\"}");
    struct bw_native_send_request request;if(bw_native_send_request_parse(body,size,&request))return text(out,400,"{\"error\":\"Choose a game from the current library\"}");
    struct bw_share_config config={0};struct bw_setup_generation generation={0};char accepted[192];int status=409,setup_only=0;const char *reply="{\"error\":\"Refresh the library before preparing this game\"}";
    pthread_mutex_lock(&p->lock);settings_sync(p);
    if(p->active){reply="{\"error\":\"Wait for the current operation to finish\"}";goto done;}
    if(p->control_release_unknown){reply="{\"error\":\"A previous operation has an unconfirmed release. Resolve its status before Send.\"}";goto done;}
    if(p->phase!=BACKEND_READY||p->stale||p->setup_blocked||p->preparation_blocked||p->settings_status!=BW_SETUP_VALID||request.library_generation!=p->generation||request.settings_revision!=p->settings_revision)goto done;
    size_t item=catalog_selected(p,&request);
    if(item==p->library.catalog.count||!bw_source_catalog_available(&p->library.catalog.items[item])){status=400;reply="{\"error\":\"Choose an available preserved source\"}";goto done;}
    if(registration_waiting(p,p->library.catalog.items[item].title_id)){reply="{\"error\":\"Registration cleanup is pending or needs attention; Send will not repeat it\"}";goto done;}
    if(removed_at(p,item)){reply="{\"error\":\"Restore this removed source before preparing it again\"}";goto done;}
    if(source_build_reason(&p->library.catalog.items[item])){reply="{\"error\":\"This compressed image format is unsupported or could not be verified.\"}";goto done;}
    if(p->library.prepared&&reader_update_reason(&p->library.prepared[item])){reply="{\"error\":\"The reader update is unresolved. Play and Send are blocked; startup files were kept.\"}";goto done;}
    if(p->library.prepared&&(p->library.prepared[item].state==BW_PREPARED_INVALID||p->library.prepared[item].state==BW_PREPARED_UNKNOWN)){reply="{\"error\":\"Refresh and inspect this console copy before preparing it again\"}";goto done;}
    if(p->library.prepared&&p->library.prepared[item].state==BW_PREPARED_VALID&&!p->library.prepared[item].startup_upgrade_required&&
       !revalidation_required(p,&p->library.prepared[item])){
#ifdef BW_HOME_STARTUP
        if(p->ops.prepare==prepare&&item<BW_CATALOG_MAX_ITEMS&&p->home_ready[item]!=1)setup_only=1;
        else
#endif
        {reply="{\"error\":\"Startup files and dashboard setup are already ready. Choose Play.\"}";goto done;}
    }
    int loaded=p->ops.read_settings(p->opaque,&config,&generation);settings_adopt(p,loaded,&config,&generation,1);
    if(loaded!=BW_SETUP_VALID||request.settings_revision!=p->settings_revision||!bw_native_setup_generation_equal(&generation,&p->settings_generation))goto done;
    if(p->joinable){if(pthread_join(p->worker,NULL)){status=503;goto done;}p->joinable=0;}
    if(!p->library.prepared){p->library.prepared=calloc(p->library.catalog.count,sizeof *p->library.prepared);if(!p->library.prepared){status=503;goto done;}}
    struct bw_native_prepare_selection selection={0};const struct bw_catalog_item *selected=&p->library.catalog.items[item];
    if(bw_source_catalog_binding(&selection.identity,&config,selected)||p->ops.nonce(p->opaque,selection.run)||!nonce_valid(selection.run)){status=503;goto done;}
    selection.boot=selected->boot;selection.param=selected->param;selection.image=selected->image;selection.logical_boot_size=selected->logical_boot_size;selection.container_kind=selected->container_kind;struct bw_native_job_token job={0};
    if(bw_native_job_begin(p->jobs,selection.run,selection.identity.binding.source,&job)||bw_native_job_start(&job)){status=503;reply="{\"error\":\"A private preparation operation could not start\"}";goto done;}
    p->sending.config=config;p->sending.expected=generation;p->sending.selection=selection;p->sending.token=job;p->sending.item=item;p->preparation_changed=0;p->worker_kind=WORK_PREPARE;p->active=1;
    p->sending.setup_only=(unsigned)setup_only;
    if(setup_only)p->sending.retained=p->library.prepared[item];
    memset(p->sending.authority,0,33);if(authority&&p->preparation_idle)memcpy(p->sending.authority,authority,33);
    if(worker_start(&p->worker,prepare_worker,p)){(void)bw_native_job_finish(&job,BW_JOB_FAILED);p->active=0;bw_native_sensitive_clear(&p->sending,sizeof p->sending);status=503;reply="{\"error\":\"The preparation worker could not start\"}";goto done;}
    p->joinable=1;snprintf(accepted,sizeof accepted,"{\"ok\":true,\"jobId\":\"%s\",\"message\":\"%s\"}",selection.run,
        setup_only?"Keeping startup files; finishing reader and PS5 dashboard setup":"Preparing startup files directly from the PC");status=202;reply=accepted;
done:
    pthread_mutex_unlock(&p->lock);bw_share_clear(&config);bw_native_sensitive_clear(&request,sizeof request);return text(out,status,reply);
}
static int send(struct bw_native_backend *p,const void *body,size_t size,const char *token,struct bw_native_response *out){return send_owned(p,NULL,body,size,token,out);}
#include "native_backend_removal.h"
static int action_authorized(const struct bw_native_backend *p,const char *token){
    if(!token||strlen(token)!=32)return 0;unsigned different=0;for(unsigned i=0;i<32;i++)different|=(unsigned char)token[i]^(unsigned char)p->setup_token[i];return different==0;
}
static int play_current(void *opaque){
    BW_LT_COUNT(BW_LT_BACKEND_GUARDS,1);
    struct bw_native_backend *p=opaque;struct bw_setup_generation expected;
    pthread_mutex_lock(&p->lock);int active=p->active&&p->worker_kind==WORK_PLAY&&!p->playing.close_requested;expected=p->playing.selection.connection;pthread_mutex_unlock(&p->lock);
    if(!active)return -1;struct bw_share_config config={0};struct bw_setup_generation actual={0};
    int changed=p->ops.read_settings(p->opaque,&config,&actual)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&actual,&expected);bw_share_clear(&config);
    if(changed){pthread_mutex_lock(&p->lock);p->playing.changed=1;p->stale=1;p->phase=BACKEND_CONFIG_CHANGED;pthread_mutex_unlock(&p->lock);return -1;}return 0;
}
static void play_message(char out[256],const char *message){
    snprintf(out,256,"%s",message?message:"");
    /* Truncation must not split a UTF-8 character and break /api/state. An
     * invalid diagnostic is reduced to its valid prefix, never interpreted. */
    size_t n=strlen(out);while(n&&!bw_json_utf8(out))out[--n]=0;
}
static void play_failure_locked(struct bw_native_backend *p,const char *message){
    if(*p->last_failure.message)return;
    play_message(p->last_failure.message,message);
    if(!*p->last_failure.message)strcpy(p->last_failure.message,"The game could not start or continue. Check the PS5 and the shared folder before retrying.");
    memcpy(p->last_failure.run,p->playing.selection.run,sizeof p->last_failure.run);
    snprintf(p->last_failure.game,sizeof p->last_failure.game,"%s",p->playing.selection.identity.binding.source);
}
static void play_observe(void *opaque,const struct bw_reader_event *event){
    struct bw_native_backend *p=opaque;if(!event||!event->phase||!event->message)return;
    pthread_mutex_lock(&p->lock);
    if(p->active&&p->worker_kind==WORK_PLAY){
        /* Every maintained Sony launch reports launching before the call.
         * Latch it, including unknown/no-PID outcomes, before phase mapping. */
        if(event->pid>0||!strcmp(event->phase,"launching")||!strcmp(event->phase,"running"))p->playing.launch_seen=1;
        if(!strcmp(event->phase,"error")&&!strcmp(p->playing.phase,"preflight")&&
           event->pid<=0&&p->playing.pid<=0&&!p->playing.launch_seen&&!p->playing.close_requested&&!p->playing.changed)
            p->playing.preflight_failed=1;
        /* A reader error may still retain an in-flight game and cache. Only
         * the joined worker's return establishes that ownership was released. */
        const char *phase=!strcmp(event->phase,"error")?"unknown":p->playing.close_requested?"closing":event->phase;
        if(strcmp(phase,"preflight")&&strcmp(phase,"launching")&&strcmp(phase,"running")&&strcmp(phase,"stopped")&&strcmp(phase,"closing"))phase="unknown";
        snprintf(p->playing.phase,sizeof p->playing.phase,"%s",phase);play_message(p->playing.message,event->message);
        if(!strcmp(event->phase,"error")&&!p->playing.close_requested)play_failure_locked(p,p->playing.message);
        p->playing.pid=event->pid;p->playing.network_bytes=event->network_bytes;p->playing.hit_bytes=event->cache_hit_bytes;p->playing.high_water=event->stage_high_water;
    }
    pthread_mutex_unlock(&p->lock);
}
static int start_prepare_current(void *opaque){
    struct bw_native_backend *p=opaque;
    return play_current(p)||p->ops.idle(p->opaque)?-1:0;
}
static int start_prepare(struct bw_native_backend *p){
    /* The console worker owns both steps. No browser poll, second request or
     * disconnected client can start a second preparation or skip preflight. */
    struct bw_share_config config=p->playing.prepare_config;struct bw_native_prepared prepared={0};
    bw_share_clear(&p->playing.prepare_config);
    int result=start_prepare_current(p)?BW_SEND_FAILED:p->ops.prepare(p->opaque,&config,&p->playing.prepare_selection,
        start_prepare_current,p,bw_native_job_observe,&p->playing.prepare_token,&prepared);
    bw_share_clear(&config);
    if(!result&&(!prepared_valid(&prepared,&p->playing.prepare_selection)||start_prepare_current(p)))result=BW_SEND_UNCONFIRMED;
    pthread_mutex_lock(&p->lock);
    if(!result&&bw_native_job_finish(&p->playing.prepare_token,BW_JOB_SUCCESS))result=BW_SEND_UNCONFIRMED;
    if(result){
        enum bw_native_job_failure failure=p->playing.close_requested?BW_JOB_CANCELLED:
            p->playing.changed||result==BW_BACKEND_SOURCE_CHANGED?BW_JOB_SOURCE_CHANGED:result==BW_SEND_BUSY?BW_JOB_DESTINATION_CONFLICT:BW_JOB_FAILED;
        (void)bw_native_job_finish(&p->playing.prepare_token,failure);
        if(result==BW_SEND_UNCONFIRMED)p->preparation_blocked=1;
        if(result==BW_BACKEND_SOURCE_CHANGED){p->phase=BACKEND_CONFIG_CHANGED;p->stale=1;}
        p->library.prepared[p->playing.item].state=BW_PREPARED_UNKNOWN;
        snprintf(p->playing.message,sizeof p->playing.message,"Startup preparation did not finish safely. Refresh and inspect the console copy before retrying.");
    }else{
        p->library.prepared[p->playing.item]=prepared;p->playing.selection.prepared=prepared;revalidation_clear(p,prepared.game);
        strcpy(p->playing.phase,"preflight");strcpy(p->playing.message,"Checking the selected PC and console files.");
    }
    p->playing.prepare_needed=0;pthread_mutex_unlock(&p->lock);return result;
}
static void *play_worker(void *opaque){
    struct bw_native_backend *p=opaque;
    BW_LT_SCOPE(launch_timing,p->playing.selection.run,p->playing.operation);
    int result=p->playing.prepare_needed?start_prepare(p):0;
    if(!result){
        result=play_current(p)||p->ops.idle(p->opaque)?-1:0;
        if(!result){p->playing.reader_started=1;result=p->ops.play(p->opaque,&p->playing.selection,play_current,p,play_observe,p);}
    }
    pthread_mutex_lock(&p->lock);
    /* play() must not return while game/native I/O can still own reader state. */
    /* A requested Close can interrupt a pending read. The worker may report
     * that diagnostic, but its return still proves exact process/cache release.
     * Keep ownership and an unknown/closing state until this return. */
    int closed=p->playing.close_requested&&!p->playing.changed;
    snprintf(p->playing.phase,sizeof p->playing.phase,"%s",result&&!closed?"error":"stopped");
    if(closed)snprintf(p->playing.message,sizeof p->playing.message,"%s",!p->playing.reader_started?"Launch canceled; no game was started.":result?"Game closed; reader and cache released after an interrupted operation.":"Game closed; reader and cache released.");
    else if(!result)snprintf(p->playing.message,sizeof p->playing.message,"%s","Game exited; reader and cache released.");
    else if(p->playing.changed)snprintf(p->playing.message,sizeof p->playing.message,"The saved PC connection changed; refresh the library.");
    else if(!strcmp(p->playing.message,"Checking the selected PC and console files."))snprintf(p->playing.message,sizeof p->playing.message,"Launch checks failed. Check the prepared files and exact title registration.");
    else if(result&&!closed&&p->playing.reader_started&&!*p->last_failure.message)
        snprintf(p->playing.message,sizeof p->playing.message,"The game reader stopped without a failure detail. Check the PS5 and shared folder before retrying.");
    if(p->playing.cleanup_attention)snprintf(p->playing.message,sizeof p->playing.message,"Game exited. Registration cleanup is pending or needs attention; the prepared source was kept.");
    if(result&&!closed)play_failure_locked(p,p->playing.message);
    /* No marker while an owned reader is active. Its joined return, a
     * definite preflight error and no launch/cancel/change are all needed.
     * Keep the VALID receipt so strict Send can reverify it; metadata-only
     * Refresh still sees the negative marker for this exact record. */
    if(result&&!closed&&!p->playing.changed&&p->playing.reader_started&&p->playing.preflight_failed&&!p->playing.preflight_control_verified&&
       !p->playing.launch_seen&&p->playing.pid<=0&&revalidation_remember(p,&p->playing.selection))
        p->preparation_blocked=1;
    if(p->control_release_unknown){
        result=-1;snprintf(p->playing.phase,sizeof p->playing.phase,"%s","unknown");
        snprintf(p->playing.message,sizeof p->playing.message,"The operation's final checks could not be released. Refresh status without repeating the action.");
    }
    p->playing.finished=now();p->playing.timing_finished=1;
    bw_share_clear(&p->playing.prepare_config);p->playing.pid=0;if(p->library.prepared&&p->playing.reader_started)p->library.prepared[p->playing.item].bytes_verified=0;p->active=0;pthread_mutex_unlock(&p->lock);BW_LT_LEAVE(launch_timing,result);return NULL;
}
static int start_play(struct bw_native_backend *p,const void *body,size_t size,const char *token,int prepare_missing,struct bw_native_response *out){
    if(!p->ops.play||!p->ops.close||!p->settings_enabled)return text(out,409,"{\"error\":\"Native playback is unavailable in this build\"}");
    if(!action_authorized(p,token))return text(out,403,"{\"error\":\"Refresh Blackwire before starting a game\"}");
    struct bw_native_send_request request;if(bw_native_send_request_parse(body,size,&request))return text(out,400,"{\"error\":\"Choose a prepared game from the current library\"}");
    struct bw_share_config config={0};struct bw_setup_generation generation={0};int status=409;const char *reply="{\"error\":\"Refresh the library before starting this game\"}";char accepted[192];
    pthread_mutex_lock(&p->lock);settings_sync(p);
    if(p->active){reply="{\"error\":\"Close the active game or wait for the current operation\"}";goto done;}
    if(p->phase!=BACKEND_READY||p->stale||p->setup_blocked||p->settings_status!=BW_SETUP_VALID||request.library_generation!=p->generation||request.settings_revision!=p->settings_revision)goto done;
    size_t item=catalog_selected(p,&request);
    if(item==p->library.catalog.count||!bw_source_catalog_available(&p->library.catalog.items[item])){reply="{\"error\":\"Choose an available preserved source\"}";goto done;}
    if(registration_waiting(p,p->library.catalog.items[item].title_id)){reply="{\"error\":\"Registration cleanup is pending or needs attention; no launch or cleanup request was repeated\"}";goto done;}
    if(removed_at(p,item)){reply="{\"error\":\"Restore this removed source and Send files before starting it again\"}";goto done;}
    if(source_build_reason(&p->library.catalog.items[item])){reply="{\"error\":\"This compressed image format is unsupported or could not be verified.\"}";goto done;}
    if(p->library.prepared&&reader_update_reason(&p->library.prepared[item])){reply="{\"error\":\"The reader update is unresolved. Play and Send are blocked; startup files were kept.\"}";goto done;}
    int upgrade=p->library.prepared&&p->library.prepared[item].state==BW_PREPARED_VALID&&p->library.prepared[item].startup_upgrade_required;
    int present=p->library.prepared&&p->library.prepared[item].state==BW_PREPARED_VALID&&!upgrade&&!revalidation_required(p,&p->library.prepared[item]);
    int prepare_needed=!present&&prepare_missing&&p->ops.prepare&&!p->preparation_blocked&&
        (!p->library.prepared||p->library.prepared[item].state==BW_PREPARED_ABSENT||upgrade);
    if(!present&&!prepare_needed){reply="{\"error\":\"Prepare or inspect startup files before playing this source\"}";goto done;}
    int loaded=p->ops.read_settings(p->opaque,&config,&generation);settings_adopt(p,loaded,&config,&generation,1);
    if(loaded!=BW_SETUP_VALID||request.settings_revision!=p->settings_revision||!bw_native_setup_generation_equal(&generation,&p->settings_generation))goto done;
    if(p->ops.idle(p->opaque)){reply="{\"error\":\"Close running games before starting this source\"}";goto done;}
    if(p->joinable){if(pthread_join(p->worker,NULL)){status=503;goto done;}p->joinable=0;}
    if(!p->library.prepared){p->library.prepared=calloc(p->library.catalog.count,sizeof *p->library.prepared);if(!p->library.prepared){status=503;goto done;}}
    struct bw_native_play_selection selection={.prepared=p->library.prepared[item],.connection=generation};
    if(bw_source_catalog_binding(&selection.identity,&config,p->library.catalog.items+item)||
       p->ops.nonce(p->opaque,selection.run)||!nonce_valid(selection.run)||!strcmp(selection.run,p->playing.selection.run)){status=503;reply="{\"error\":\"A fresh private game session could not start\"}";goto done;}
    /* The bounded failure history is never evicted to admit a new launch. */
    if(!revalidation_capacity(p,selection.identity.binding.game)){status=503;reply="{\"error\":\"Revalidate a failed startup copy before another new source can launch\"}";goto done;}
    struct bw_native_prepare_selection preparation={.identity=selection.identity,.boot=p->library.catalog.items[item].boot,.param=p->library.catalog.items[item].param,.image=p->library.catalog.items[item].image,.logical_boot_size=p->library.catalog.items[item].logical_boot_size,.container_kind=p->library.catalog.items[item].container_kind};
    struct bw_native_job_token job={0};
    if(p->play_epoch==UINT64_MAX){status=503;reply="{\"error\":\"Restart Blackwire before starting another session\"}";goto done;}
    if(prepare_needed&&(p->ops.nonce(p->opaque,preparation.run)||!nonce_valid(preparation.run)||!strcmp(preparation.run,selection.run)||
       bw_native_job_begin(p->jobs,preparation.run,selection.identity.binding.source,&job)||bw_native_job_start(&job))){status=503;reply="{\"error\":\"A private startup preparation could not begin\"}";goto done;}
    ++p->play_epoch; /* A failed worker creation consumes this epoch as well. */
    memset(&p->playing,0,sizeof p->playing);p->playing.selection=selection;p->playing.item=item;p->playing.prepare_needed=prepare_needed;
    memcpy(p->playing.dispatch,p->external_nonce,33);
#ifdef BW_LAUNCH_TIMING
    if(nonce_valid(p->external_nonce))memcpy(p->playing.operation,p->external_nonce,33);
#endif
    if(prepare_needed){p->playing.prepare_config=config;p->playing.prepare_selection=preparation;p->playing.prepare_token=job;}
    strcpy(p->playing.phase,prepare_needed?"preparing":"preflight");strcpy(p->playing.message,prepare_needed?"Fetching required startup files from the PC before launch.":"Checking the selected PC and console files.");p->worker_kind=WORK_PLAY;p->active=1;
    if(worker_start(&p->worker,play_worker,p)){if(prepare_needed)(void)bw_native_job_finish(&job,BW_JOB_FAILED);bw_share_clear(&p->playing.prepare_config);p->active=0;strcpy(p->playing.phase,"error");status=503;reply="{\"error\":\"The reader worker could not start\"}";goto done;}
    p->playing.started=now();p->playing.timing_started=p->playing.started>=0;
    memset(&p->last_failure,0,sizeof p->last_failure);
    p->joinable=1;snprintf(accepted,sizeof accepted,"{\"ok\":true,\"runId\":\"%s\",\"message\":\"Checking and starting the selected game\"}",selection.run);status=202;reply=accepted;
done:
    pthread_mutex_unlock(&p->lock);bw_share_clear(&config);bw_native_sensitive_clear(&request,sizeof request);return text(out,status,reply);
}
static int stop_play(struct bw_native_backend *p,const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!p->ops.close)return text(out,409,"{\"error\":\"Native session Close is unavailable in this build\"}");
    if(!action_authorized(p,token))return text(out,403,"{\"error\":\"Refresh Blackwire before closing a game\"}");
    /* One JSON string containing the exact private run nonce. No filesystem,
     * app/PID or client-supplied title selection is accepted by Close. */
    char run[33];if(!body||size!=34||((const char *)body)[0]!='"'||((const char *)body)[33]!='"')return text(out,400,"{\"error\":\"Choose the current game session\"}");
    memcpy(run,(const char *)body+1,32);run[32]=0;if(!nonce_valid(run))return text(out,400,"{\"error\":\"Choose the current game session\"}");
    pthread_mutex_lock(&p->lock);int status=409;const char *reply="{\"error\":\"That game session is no longer current\"}";
    if(p->active&&p->worker_kind==WORK_PLAY&&!p->playing.retiring&&!strcmp(run,p->playing.selection.run)&&!p->playing.close_requested){
        /* Cancel the controller's pre-launch work too: the reader may not
         * have entered its runtime yet while loading the pinned file map. */
        p->playing.close_requested=1;
        int closed;
#ifdef BW_HOME_STARTUP
        if(p->playing.home_adopted)closed=BW_SESSION_CLOSE_REQUESTED; /* joined owner performs one exact Close */
        else
#endif
        closed=p->ops.close(p->opaque,run);
        if(closed==BW_SESSION_CLOSE_REFUSED&&p->playing.pid<=0)closed=BW_SESSION_CLOSE_REQUESTED;
        if(closed!=BW_SESSION_CLOSE_REFUSED){
            strcpy(p->playing.phase,closed==BW_SESSION_CLOSE_UNKNOWN?"unknown":"closing");
            snprintf(p->playing.message,sizeof p->playing.message,"%s",closed==BW_SESSION_CLOSE_UNKNOWN?"Close could not be confirmed. Check the PS5 dashboard; reader ownership is retained.":"Closing this game; waiting for its process to exit.");
            status=202;reply="{\"ok\":true,\"message\":\"Waiting for the current game to stop\"}";
        }else p->playing.close_requested=0;
    }
    pthread_mutex_unlock(&p->lock);return text(out,status,reply);
}
static int setup(struct bw_native_backend *p,const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!p->settings_enabled)return text(out,409,"{\"error\":\"Connection editing is unavailable in this native build\"}");
    unsigned difference=0;if(!token||strlen(token)!=32)return text(out,403,"{\"error\":\"Refresh Blackwire before saving connection settings\"}");
    for(unsigned i=0;i<32;i++)difference|=(unsigned char)token[i]^(unsigned char)p->setup_token[i];
    if(difference)return text(out,403,"{\"error\":\"Refresh Blackwire before saving connection settings\"}");
    struct bw_native_setup_request request;struct bw_share_config old={0};struct bw_setup_generation generation={0};char accepted[192]={0};int status=400;const char *reply="{\"error\":\"Enter a valid PC address, share and sign-in selection\"}";
    if(bw_native_setup_request_parse(body,size,&request))return text(out,400,reply);
    pthread_mutex_lock(&p->lock);
    if(p->active){status=409;reply="{\"error\":\"Wait for the current library or connection operation\"}";goto done;}
    settings_sync(p);
    if(p->setup_blocked){status=409;reply="{\"error\":\"Refresh the unconfirmed connection before another save\"}";goto done;}
    if(request.revision!=p->settings_revision){status=409;reply="{\"error\":\"Saved settings changed. Refresh before saving\"}";goto done;}
    int loaded=p->ops.read_settings(p->opaque,&old,&generation);
    if(loaded<0){status=409;reply="{\"error\":\"Private connection settings are linked or unavailable\"}";goto done;}
    settings_adopt(p,loaded,&old,&generation,1);
    if(request.revision!=p->settings_revision){status=409;reply="{\"error\":\"Saved settings changed. Refresh before saving\"}";goto done;}
    if(request.keep_credentials){
        if(loaded!=BW_SETUP_VALID||strcmp(request.values.server,old.server)){reply="{\"error\":\"Enter sign-in details when connecting to a different PC\"}";goto done;}
        memcpy(request.values.user,old.user,sizeof old.user);memcpy(request.values.password,old.password,sizeof old.password);
    }
    if(!request.share2_present&&loaded==BW_SETUP_VALID)memcpy(request.values.share2,old.share2,sizeof old.share2);
    if(!valid_config(&request.values))goto done;
    if(p->setup_operation==UINT32_MAX){status=503;reply="{\"error\":\"Reopen Blackwire before another connection check\"}";goto done;}
    if(p->joinable){if(pthread_join(p->worker,NULL)){status=503;reply="{\"error\":\"The prior connection worker needs a fresh check\"}";goto done;}p->joinable=0;}
    p->pending.config=request.values;p->pending.expected=generation;p->pending.unchanged=loaded==BW_SETUP_VALID&&config_equal(&old,&request.values);
    if(!p->pending.unchanged&&(p->ops.nonce(p->opaque,p->pending.nonce)||!nonce_valid(p->pending.nonce))){bw_native_sensitive_clear(&p->pending,sizeof p->pending);status=503;reply="{\"error\":\"A private settings transaction could not start\"}";goto done;}
    p->setup_operation++;p->active=1;p->worker_kind=WORK_SETUP;p->setup_phase=SETUP_CHECKING;p->setup_error=SETUP_NO_ERROR;p->phase=BACKEND_SCANNING;p->stale=p->library.catalog.count>0;
    if(worker_start(&p->worker,scan,p)){p->active=0;p->phase=BACKEND_FAILED;p->setup_phase=SETUP_FAILED;p->setup_error=SETUP_START_ERROR;bw_native_sensitive_clear(&p->pending,sizeof p->pending);status=503;reply="{\"error\":\"The connection check could not start\"}";goto done;}
    p->joinable=1;snprintf(accepted,sizeof accepted,"{\"ok\":true,\"pending\":true,\"operation\":%u,\"message\":\"Checking the PC share before saving\"}",p->setup_operation);status=202;reply=accepted;
done:
    pthread_mutex_unlock(&p->lock);bw_share_clear(&old);bw_native_sensitive_clear(&request,sizeof request);return text(out,status,reply);
}
static int unhex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int cover(struct bw_native_backend *p,const char *query,struct bw_native_response *out){
    if(strncmp(query,"name=",5))return text(out,400,"{\"error\":\"Invalid cover selection\"}");
    const char *end=strchr(query,'&');uint64_t generation=0;int versioned=end!=NULL;
    if(end){const unsigned char *at=(const unsigned char *)end+1;if(*at++!='v'||*at++!='='||!*at)return text(out,400,"{\"error\":\"Invalid cover selection\"}");
        for(;*at;at++){if(*at<'0'||*at>'9'||generation>(UINT64_MAX-(*at-'0'))/10)return text(out,400,"{\"error\":\"Invalid cover selection\"}");generation=generation*10+(*at-'0');}}
    else end=query+strlen(query);
    char source[BW_MANIFEST_MAX_PATH+1];size_t used=0;
    for(const unsigned char *at=(const unsigned char *)query+5;at<(const unsigned char *)end;at++){
        unsigned char c=*at;if(c=='%'){if(end-(const char *)at<3||unhex(at[1])<0||unhex(at[2])<0)return text(out,400,"{\"error\":\"Invalid cover selection\"}");c=(unsigned char)(16*unhex(at[1])+unhex(at[2]));at+=2;}else if(c=='+')c=' ';
        if(!c||used>=BW_MANIFEST_MAX_PATH)return text(out,400,"{\"error\":\"Invalid cover selection\"}");source[used++]=(char)c;
    }source[used]=0;if(!used||bw_manifest_validate_path(source,used))return text(out,400,"{\"error\":\"Invalid cover selection\"}");
    pthread_mutex_lock(&p->lock);int result=1;
    if(versioned&&generation!=p->generation){pthread_mutex_unlock(&p->lock);return text(out,409,"{\"error\":\"Cover generation changed; refresh the library status\"}");}
    for(size_t i=0;i<p->library.catalog.count;i++)if(!strcmp(source,p->library.catalog.items[i].source)){
        const struct bw_native_cover *image=cached_cover(&p->library,i);if(!image)break;
        void *bytes=malloc(image->size);if(!bytes){result=-1;break;}
        memcpy(bytes,image->bytes,image->size);*out=(struct bw_native_response){200,"image/png",bytes,image->size};result=0;break;
    }
    pthread_mutex_unlock(&p->lock);return result==1?text(out,404,"{\"error\":\"Cover unavailable\"}"):result;
}
static int handle_core(struct bw_native_backend *p,const char *owner,const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!p||!method||!url||!out||(!body&&size))return -1;memset(out,0,sizeof *out);
    if(!strcmp(method,"GET")&&!strcmp(url,"/api/state")){if(state(p,out))return text(out,503,"{\"error\":\"Library status could not be serialized\"}");return 0;}
    if(!strcmp(method,"GET")&&!strncmp(url,"/cover?",7))return cover(p,url+7,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/setup"))return setup(p,body,size,token,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/send"))return owner?send_owned(p,owner,body,size,token,out):send(p,body,size,token,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/uninstall"))return removal_action(p,owner,body,size,token,0,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/restore"))return removal_action(p,owner,body,size,token,1,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/play"))return start_play(p,body,size,token,0,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/start"))return start_play(p,body,size,token,1,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/close"))return stop_play(p,body,size,token,out);
    if(!strcmp(method,"POST")&&!strcmp(url,"/api/refresh")){
        if(size&&(size!=2||memcmp(body,"{}",2)))return text(out,400,"{\"error\":\"Refresh takes no source selection\"}");
        int result=refresh_unlocked(p);return text(out,result==0?202:result==1?409:503,result==0?"{\"ok\":true,\"message\":\"Reading the PC library\"}":result==1?"{\"error\":\"Library discovery is already running\"}":"{\"error\":\"Library discovery could not start\"}");
    }
    if(!strcmp(method,"POST")&&(!strcmp(url,"/api/send")||!strcmp(url,"/api/play")||!strcmp(url,"/api/close")||!strcmp(url,"/api/setup")||!strcmp(url,"/api/settings")))
        return text(out,409,"{\"error\":\"This action is unavailable in this native development build\"}");
    return text(out,!strcmp(method,"GET")||!strcmp(method,"POST")?404:405,"{\"error\":\"Unsupported native route\"}");
}
/* Dashboard reservation and ordinary POST dispatch share this short gate.
 * No mutex is retained across threads or the game lifetime; only the nonce is.
 * Existing handlers/worker guards remain the authority for every BW action. */
int bw_native_backend_external_reserve(struct bw_native_backend *p,const char nonce[33],const char token[33]){
    if(!p||!nonce_valid(nonce)||pthread_mutex_lock(&p->dispatch_lock))return -1;
    pthread_mutex_lock(&p->lock);int result=-1;
    if(!action_authorized(p,token))result=-1;
    else if(p->active||*p->external_nonce)result=1;
    else{memcpy(p->external_nonce,nonce,33);
        p->external_expected.valid=1;p->external_expected.generation=p->generation;p->external_expected.play_epoch=p->play_epoch;
        p->external_expected.revision=p->settings_revision;p->external_expected.setup_operation=p->setup_operation;
        p->external_expected.settings_status=p->settings_status;p->external_expected.settings_known=p->settings_known;p->external_expected.settings=p->settings_generation;
        memcpy(p->external_expected.sending,p->sending.selection.run,33);memcpy(p->external_expected.playing,p->playing.selection.run,33);result=0;}
    pthread_mutex_unlock(&p->lock);pthread_mutex_unlock(&p->dispatch_lock);return result;
}
int bw_native_backend_external_release(struct bw_native_backend *p,const char nonce[33]){
    if(!p||!nonce_valid(nonce)||pthread_mutex_lock(&p->dispatch_lock))return -1;
    pthread_mutex_lock(&p->lock);int result=-1;
    if(!strcmp(p->external_nonce,nonce)){memset(p->external_nonce,0,33);memset(&p->external_expected,0,sizeof p->external_expected);result=0;}
    pthread_mutex_unlock(&p->lock);pthread_mutex_unlock(&p->dispatch_lock);return result;
}
/* Background observations own no active game/mutation. The caller owns both
 * dispatch_lock and lock; consume only its exact, known inactive reservation. */
static int external_idle_release_locked(struct bw_native_backend *p,const char nonce[33]){
    if(p->active||p->control_release_unknown||!p->external_expected.valid||strcmp(p->external_nonce,nonce))return 1;
    memset(p->external_nonce,0,33);memset(&p->external_expected,0,sizeof p->external_expected);return 0;
}
int bw_native_backend_external_release_idle(struct bw_native_backend *p,const char nonce[33]){
    if(!p||!nonce||!nonce_valid(nonce)||pthread_mutex_lock(&p->dispatch_lock))return -1;
    int result=-1;
    if(!pthread_mutex_lock(&p->lock)){result=external_idle_release_locked(p,nonce);pthread_mutex_unlock(&p->lock);}
    pthread_mutex_unlock(&p->dispatch_lock);return result;
}
static int external_current_locked(const struct bw_native_backend *p,const char nonce[33]){
    return !p->external_expected.valid||p->active||p->control_release_unknown||strcmp(p->external_nonce,nonce)||
        p->generation!=p->external_expected.generation||p->play_epoch!=p->external_expected.play_epoch||
        p->settings_revision!=p->external_expected.revision||p->setup_operation!=p->external_expected.setup_operation||
        p->settings_status!=p->external_expected.settings_status||p->settings_known!=p->external_expected.settings_known||
        !bw_native_setup_generation_equal(&p->settings_generation,&p->external_expected.settings)||
        strcmp(p->sending.selection.run,p->external_expected.sending)||strcmp(p->playing.selection.run,p->external_expected.playing)?-1:0;
}
int bw_native_backend_external_current(struct bw_native_backend *p,const char nonce[33]){
    if(!p||!nonce||!nonce_valid(nonce)||!p->ops.read_settings)return -1;
    pthread_mutex_lock(&p->lock);int bad=external_current_locked(p,nonce);struct bw_setup_generation expected=p->external_expected.settings;int status=p->external_expected.settings_status;pthread_mutex_unlock(&p->lock);
    if(bad)return -1;struct bw_share_config config={0};struct bw_setup_generation actual={0};
    int loaded=p->ops.read_settings(p->opaque,&config,&actual);bw_share_clear(&config);
    pthread_mutex_lock(&p->lock);bad=loaded<0||loaded!=status||!bw_native_setup_generation_equal(&expected,&actual)||external_current_locked(p,nonce);pthread_mutex_unlock(&p->lock);
    return bad?-1:0;
}
int bw_native_backend_registration_pending(struct bw_native_backend *p,const char title[10]){
    if(!p||!title||strnlen(title,10)!=9)return -1;
    pthread_mutex_lock(&p->lock);int value=p->registration_ledger_unknown?-1:registration_waiting(p,title);pthread_mutex_unlock(&p->lock);return value;
}
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
struct registration_observation_guard {struct bw_native_backend *owner;char nonce[33];uint64_t generation;uint32_t revision;struct bw_setup_generation settings;double deadline;};
static int registration_observation_current(void *opaque){
    struct registration_observation_guard *g=opaque;struct bw_native_backend *p=g->owner;
    double at=now();if(at<0||at>=g->deadline)return -1;
    pthread_mutex_lock(&p->lock);
    int bad=p->active||p->control_release_unknown||strcmp(p->external_nonce,g->nonce)||p->generation!=g->generation||p->settings_revision!=g->revision||
        p->settings_status!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&p->settings_generation,&g->settings);
    pthread_mutex_unlock(&p->lock);return bad?-1:0;
}
static int registration_observation_control(void *opaque,enum bw_nrl_control point){
    struct registration_observation_guard *g=opaque;struct bw_native_backend *p=g->owner;
    if(point!=BW_NRL_RETIRE||registration_observation_current(g))return -1;
    struct bw_share_config config={0};struct bw_setup_generation actual={0};
    int bad=p->ops.read_settings(p->opaque,&config,&actual)!=BW_SETUP_VALID||!bw_native_setup_generation_equal(&actual,&g->settings);bw_share_clear(&config);
    struct bw_startup_idle_request idle={.deadline=g->deadline,.current=registration_observation_current,.opaque=g,.game_log=backend_game_log};
    return bad||p->ops.idle(p->opaque)||bw_startup_idle_ps5(&idle)||registration_observation_current(g)?-1:0;
}
struct registration_removed_observation {struct registration_observation_guard *guard;struct bw_nrc_remote remote;};
static int registration_removed_current(void *opaque){struct registration_removed_observation *o=opaque;return registration_observation_control(o->guard,BW_NRL_RETIRE);}
static int registration_removed_sources(void *opaque,const struct bw_nrc_subject *subject){
    struct registration_removed_observation *o=opaque;struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;
    return registration_removed_current(o)||bw_nrl_absent_sources(&env,&o->remote,subject)||registration_removed_current(o)?-1:0;
}
static int registration_removed_manual(void *opaque,const struct bw_nrc_subject *subject){
    struct registration_removed_observation *o=opaque;struct bw_sm_reply a={0},b={0};
    if(registration_removed_current(o)||bw_sm_post(&o->remote.connection,BW_SM_MANUAL_LIST,subject->source,&a)||
       bw_sm_post(&o->remote.connection,BW_SM_MANUAL_LIST,subject->source,&b)||a.present!=b.present||a.count!=b.count||registration_removed_current(o))return -1;
    return a.present?1:0;
}
static int registration_observe_removed(struct bw_native_backend *p,struct registration_observation_guard *guard,
    const struct bw_nrc_environment *env,struct bw_native_cleanup_observation *out){
    struct bw_nrc_candidate *owners=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *owners);struct bw_removed_source *removed=calloc(BW_REMOVED_LIMIT,sizeof *removed);
    size_t count=0,removed_count=0;int result=-1;
    if(!owners||!removed||bw_nrc_candidates(env,owners,BW_NRC_CANDIDATE_LIMIT,&count)||
       bw_removed_list(backend_data_root,removed,BW_REMOVED_LIMIT,&removed_count))goto done;
    for(size_t i=0;i<count;i++)if(!owners[i].pending&&!owners[i].retiring)for(size_t j=0;j<removed_count;j++)
        if(!strcmp(owners[i].subject.title,removed[j].identity.binding.title)&&!strcmp(owners[i].subject.game,removed[j].identity.binding.game)){
            memcpy(out->title,owners[i].subject.title,10);memcpy(out->run,owners[i].subject.run,33);out->pending=1;
            struct registration_removed_observation context={.guard=guard};
            context.remote.connection=(struct bw_sm_connection){0,registration_observation_current,guard};
            struct bw_nrc_removed_observer ops={&context,registration_removed_current,registration_removed_sources,registration_removed_manual};struct bw_nrc_result observed={0};
            result=bw_nrc_observe_removed(env,out->title,out->run,removed+j,&ops,&observed);
            out->absent=!result&&observed.state==BW_NRC_ABSENT;out->pending=!out->absent;out->reason=observed.reason;
            if(out->absent&&!registration_observation_control(guard,BW_NRL_RETIRE)){
                struct bw_native_prepared absent={.state=BW_PREPARED_ABSENT};struct bw_game_uninstall_capability cap={0};
                if(default_removal_probe(p,&removed[j].identity,&absent,&cap)||!cap.removed||!cap.can_restore){result=-1;out->absent=0;out->pending=1;goto done;}
                pthread_mutex_lock(&p->lock);
                for(size_t k=0;k<p->library.catalog.count;k++)if(!strcmp(p->library.catalog.items[k].source,removed[j].identity.binding.source)&&
                    !strcmp(p->library.catalog.items[k].title_id,removed[j].identity.binding.title)){
                    if(p->library.removal)p->library.removal[k]=cap;if(p->library.prepared)memset(p->library.prepared+k,0,sizeof *p->library.prepared);
                }
                if(p->removing.state==REMOVAL_UNKNOWN&&!p->removing.restore&&!strcmp(p->removing.selected.operation,removed[j].operation)&&
                   !strcmp(p->removing.selected.identity.binding.game,removed[j].identity.binding.game)&&
                   !strcmp(p->removing.selected.identity.binding.source,removed[j].identity.binding.source)&&
                   !memcmp(p->removing.selected.prepared.record_sha256,removed[j].prepared_sha256,32)){
                    p->removing.state=REMOVAL_DONE;p->removing.phase=BW_GAME_REMOVE_DONE;p->removing.unknown=0;
                    snprintf(p->removing.message,sizeof p->removing.message,"Removed from PS5. PC backup and saved data preserved.");
                }
                p->generation++;
#ifdef BW_HOME_STARTUP
        memset(p->home_ready,0,sizeof p->home_ready); /* Catalog indices grant no cross-generation capability. */
#endif
                pthread_mutex_unlock(&p->lock);
            }else if(out->absent){result=-1;out->absent=0;out->pending=1;}
            goto done;
        }
    result=1;
done:free(owners);free(removed);return result;
}
#endif
static int registration_observe(struct bw_native_backend *p,const char nonce[33],struct bw_native_cleanup_observation *out,unsigned *released){
    if(out)memset(out,0,sizeof *out);
    if(released)*released=0;
    if(!p||!out||!nonce||!nonce_valid(nonce))return -1;
#if defined(BW_NATIVE_PLAYBACK) && defined(BW_NATIVE_PRELOAD)
    if(pthread_mutex_lock(&p->dispatch_lock))return -1;
    pthread_mutex_lock(&p->lock);int allowed=!p->active&&!strcmp(p->external_nonce,nonce)&&p->ops.play==play&&p->settings_status==BW_SETUP_VALID;
    struct registration_observation_guard guard={.owner=p,.generation=p->generation,.revision=p->settings_revision,.settings=p->settings_generation,.deadline=now()+20.0};
    memcpy(guard.nonce,nonce,33);pthread_mutex_unlock(&p->lock);int result=-1;
    if(!allowed||registration_observation_control(&guard,BW_NRL_RETIRE)||registration_pending_refresh(p))goto done;
    pthread_mutex_lock(&p->lock);size_t i=0;for(;i<p->registration_pending_count;i++)if(p->registration_pending[i].retiring)break;
    if(i<p->registration_pending_count){memcpy(out->title,p->registration_pending[i].title,10);memcpy(out->run,p->registration_pending[i].run,33);out->pending=1;}
    pthread_mutex_unlock(&p->lock);
    struct bw_nrc_environment env=registration_environment;env.data_root=backend_data_root;struct bw_nrc_retirement_result observed={0};
    if(!out->pending){result=registration_observe_removed(p,&guard,&env,out);if(registration_pending_refresh(p))result=-1;goto done;}
    result=bw_nrl_observe(&env,out->title,out->run,0,registration_observation_control,&guard,&observed);
    out->absent=!result&&observed.registration.state==BW_NRC_ABSENT;out->pending=!out->absent;out->reason=observed.registration.reason;
    if(registration_pending_refresh(p))result=-1;
done:
    /* No callback remains live here. Release this background owner's lease
     * before queued foreground requests can acquire the same dispatch gate.
     * A refused read is not release uncertainty; its durable title hold stays. */
    if(released){
        if(!pthread_mutex_lock(&p->lock)){
            if(!external_idle_release_locked(p,nonce))*released=1;
            pthread_mutex_unlock(&p->lock);
        }else result=-1;
    }
    pthread_mutex_unlock(&p->dispatch_lock);return result;
#else
    return 1;
#endif
}
int bw_native_backend_registration_observe(struct bw_native_backend *p,const char nonce[33],struct bw_native_cleanup_observation *out){
    return registration_observe(p,nonce,out,NULL);
}
int bw_native_backend_registration_observe_idle(struct bw_native_backend *p,const char nonce[33],struct bw_native_cleanup_observation *out,unsigned *released){
    if(!released)return -1;return registration_observe(p,nonce,out,released);
}
static int dispatch_request(struct bw_native_backend *p,const char *owner,const char *method,const char *url,
    const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!p||!method||!url||!out||(!body&&size))return -1;
    if(strcmp(method,"POST"))return owner?-1:handle_core(p,NULL,method,url,body,size,token,out);
#ifdef BW_LAUNCH_TIMING
    int timed=!strcmp(url,"/api/start")||!strcmp(url,"/api/play");struct bw_lt_request timing_request={0};
    if(timed)bw_lt_request_begin(&timing_request,owner,!strcmp(url,"/api/start")?BW_LT_START_ROUTE:BW_LT_PLAY_ROUTE,NULL);
#endif
    if(pthread_mutex_lock(&p->dispatch_lock)){
#ifdef BW_LAUNCH_TIMING
        if(timed)bw_lt_request_end(&timing_request,NULL,0,-1);
#endif
        return -1;
    }
    int admitted=owner?nonce_valid(owner)&&!strcmp(owner,p->external_nonce):!*p->external_nonce;
    int result=admitted?handle_core(p,owner,method,url,body,size,token,out):
        text(out,409,"{\"error\":\"The Dashboard owns a queued or retained operation\"}");
#ifdef BW_LAUNCH_TIMING
    char observed_run[33]={0};
    /* The dispatch gate prevents another request replacing this accepted run.
     * The observation acquires no launch authority and never reads a token. */
    if(timed&&admitted&&!result&&out->status==202){
        pthread_mutex_lock(&p->lock);
        if(p->worker_kind==WORK_PLAY&&nonce_valid(p->playing.selection.run))memcpy(observed_run,p->playing.selection.run,33);
        pthread_mutex_unlock(&p->lock);
    }
#endif
    pthread_mutex_unlock(&p->dispatch_lock);
#ifdef BW_LAUNCH_TIMING
    if(timed)bw_lt_request_end(&timing_request,observed_run,result?0:out->status,result);
#endif
    return result;
}
int bw_native_backend_handle_authenticated(struct bw_native_backend *p,const char *method,const char *url,
    const void *body,size_t size,const char *token,struct bw_native_response *out){
    return dispatch_request(p,NULL,method,url,body,size,token,out);
}
int bw_native_backend_handle_reserved(struct bw_native_backend *p,const char owner[33],const char *method,const char *url,
    const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!nonce_valid(owner)||!url||(strcmp(url,"/api/start")&&strcmp(url,"/api/send")&&strcmp(url,"/api/uninstall")&&strcmp(url,"/api/restore"))||!method||strcmp(method,"POST"))return -1;
    return dispatch_request(p,owner,method,url,body,size,token,out);
}
int bw_native_backend_handle(struct bw_native_backend *p,const char *method,const char *url,const void *body,size_t size,struct bw_native_response *out){
    return bw_native_backend_handle_authenticated(p,method,url,body,size,NULL,out);
}
void bw_native_response_clear(struct bw_native_response *p){if(p){free(p->bytes);memset(p,0,sizeof *p);}}

#ifdef BW_HOME_STARTUP
#include "native_backend_home.h"
#endif

