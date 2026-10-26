/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual default Play orchestration with narrow ownership spies. Actual
 * receipt/filesystem verification is tested by test_fast_ready_play.c. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#include "native_backend.c"
#include <assert.h>
static unsigned checks,cases,opened,closed,selected_calls,strict_calls,register_calls,reader_calls,owned_calls,errors,setting_calls;
static int fail_open,fail_register,fail_reader,settings_changed,foreign_registration;
static unsigned bundle_calls,begin_calls,commit_calls,retire_calls,destroy_calls,observer_calls,idle_calls;
static unsigned bundle_checkpoint_calls,total_bundle_checkpoint_calls;
static int fail_bundle,fail_begin,fail_commit,release_flags,cancelled_reader,pending_cleanup,unqualified_close,new_game,change_reservation,log_busy,startup_changed;
static int settings_status=BW_SETUP_VALID,query_mutation;
static unsigned removed_cleanup,removed_observers,removed_manual_queries,removed_sources_queries;
static int removed_observe_unknown,removed_source_unknown;
static struct bw_removed_source removed_record;
static struct bw_nrl_owner *retained_registration;
static const struct bw_nrl_request *borrowed_request;
static struct bw_nrl_request copied_request;
static struct bw_native_backend owner;
static struct bw_reader_selection selection;
static struct bw_native_launch_admission *live;
static char last_error[256];
static unsigned final_admission_checks;
static int late_startup_drift,late_settings_drift,admission_close_failure,cache_close_failure,workspace_pending,reader_error_mode,owned_failure,registration_close_failure,allow_locked_settings,begin_release_unknown,commit_release_unknown;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"fast routing case%u line%d: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
#include "source_ampr_admission_model.h"
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
int bw_native_setup_idle(void *p){(void)p;return 0;}
static int settings(void *p,struct bw_share_config *c,struct bw_setup_generation *g){CHECK(p==&owner);int locked=pthread_mutex_trylock(&owner.lock);CHECK(!locked||allow_locked_settings);if(!locked)CHECK(!pthread_mutex_unlock(&owner.lock));setting_calls++;memset(c,0,sizeof *c);*g=owner.playing.selection.connection;if(settings_changed)g->sha256[0]^=1;if(query_mutation)owner.setup_operation++;return settings_status;}
static int idle(void *p){CHECK(p==&owner&&!pthread_mutex_trylock(&owner.lock));CHECK(!pthread_mutex_unlock(&owner.lock));idle_calls++;if(change_reservation)owner.external_nonce[0]='f';return new_game?-1:0;}
int bw_startup_idle_ps5(void *opaque){struct bw_startup_idle_request *q=opaque;CHECK(q&&q->current&&q->game_log);return new_game||log_busy||q->current(q->opaque)?-1:0;}
int bw_native_launch_admission_open(struct bw_native_launch_admission **p,const char *root,const struct bw_provider_binding *id,const struct bw_native_prepared *expected,const char *run,int (*guard)(void *),void *opaque,const char **error){
    CHECK(p&&!live&&!*p&&root&&id==&owner.playing.selection.identity&&expected==&owner.playing.selection.prepared&&run&&!guard(opaque));opened++;
    if(fail_open){*error="Required startup receipt changed; choose Send to repair it.";return -1;}
    live=*p=(struct bw_native_launch_admission *)(uintptr_t)0x1234;ampr_model_open(live);return 0;
}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *out){CHECK(live&&p==live&&out);selected_calls++;*out=selection;return 0;}
int bw_native_launch_admission_current(struct bw_native_launch_admission *p,const char *root,const struct bw_provider_binding *id,const struct bw_reader_selection *s,struct bw_native_prepared *out){
    CHECK(live&&p==live&&root&&id&&s&&!strcmp(s->run,selection.run));if(begin_calls&&fail_begin)final_admission_checks++;if(out)*out=owner.playing.selection.prepared;return settings_changed||startup_changed?-1:0;
}
int bw_native_launch_admission_close(struct bw_native_launch_admission **p){CHECK(p);if(*p){CHECK(live==*p);closed++;live=NULL;*p=NULL;}return admission_close_failure?-1:0;}
int bw_native_launch_selection(const char *a,const struct bw_provider_binding *b,const struct bw_native_prepared *c,const char *d,struct bw_reader_selection *e){(void)a;(void)b;(void)c;(void)d;(void)e;strict_calls++;CHECK(0);return -1;}
int bw_native_owned_registration(const struct bw_native_register_request *r,struct bw_native_owned_registration_result *out){CHECK(r&&r->startup==live&&out);owned_calls++;return owned_failure?-1:0;}
int bw_native_register(const struct bw_native_register_request *r,struct bw_register_result *out){CHECK(r&&r->startup==live&&out&&!r->current(r->guard)&&begin_calls==1);register_calls++;*out=(struct bw_register_result){.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=!fail_register,.message="Registered source mismatch"};return fail_register?-1:0;}
int bw_native_registration_verify(const char *root,const char *apps,const char *db,const struct bw_reader_selection *s){CHECK(root&&apps&&db&&s);return foreign_registration?-1:0;}
int bw_native_reader_run(const struct bw_native_reader_request *r){
    CHECK(live&&r&&r->startup==live&&!r->current(r->opaque)&&!r->data_current(r->opaque));reader_calls++;
    if(foreign_registration){CHECK(r->before_launch(r->opaque,&r->selection)!=0);return -1;}
    CHECK(!r->before_launch(r->opaque,&r->selection));CHECK(commit_calls==1&&r->released);
    if(reader_error_mode){
        struct bw_reader_event e={.phase="launching",.message="Actual reader stage",.pid=0};
        if(reader_error_mode==2)r->observe(r->opaque,&e);
        e.phase="error";e.message="Required startup input changed during reader preflight";r->observe(r->opaque,&e);
    }
    if(cancelled_reader)owner.playing.close_requested=1;
    memcpy(r->released->run,selection.run,33);r->released->runtime_released=!!(release_flags&1);r->released->io_released=!!(release_flags&2);r->released->private_released=!!(release_flags&4);
    if(unqualified_close)owner.playing.changed=1;return fail_reader||reader_error_mode?-1:0;
}
int bw_preload_bundle_upgrade_workspace(const char *data,const char *game){
    CHECK(data&&!strcmp(data,backend_data_root)&&game&&!strcmp(game,owner.playing.selection.prepared.game));return workspace_pending;
}
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request *upgrade,const struct bw_preload_bundle_control *control,struct bw_preload_bundle_result *r){
    CHECK(upgrade&&upgrade->data_root&&!strcmp(upgrade->data_root,backend_data_root)&&upgrade->game&&
        !strcmp(upgrade->game,selection.game)&&!strcmp(upgrade->game,owner.playing.selection.prepared.game)&&
        upgrade->prepared_run&&!strcmp(upgrade->prepared_run,owner.playing.selection.prepared.run)&&
        upgrade->launch_run&&!strcmp(upgrade->launch_run,selection.run)&&!strcmp(upgrade->launch_run,owner.playing.selection.run)&&
        upgrade->prepared_record_sha256==owner.playing.selection.prepared.record_sha256);
    const struct bw_preload_bundle_request *q=&upgrade->bundle;
    char expected_root[1152];CHECK(snprintf(expected_root,sizeof expected_root,"%s/games/%s",backend_data_root,selection.game)>0&&
        q->root&&!strcmp(q->root,expected_root));
    CHECK(q&&r&&live&&!register_calls&&!begin_calls&&q->current&&!q->current(q->opaque)&&control&&control->checkpoint&&control->opaque==q->opaque&&q->deadline>now());
    bundle_calls++;bundle_checkpoint_calls++;total_bundle_checkpoint_calls++;
    if(control->checkpoint(control->opaque)){r->stage="bundle_fixture_checkpoint_refused";r->reason="bundle_authority_changed";return -1;}
    if(fail_bundle){r->stage="bundle_fixture_exact_bytes_refused";r->reason="bundle_fixture_named_identity_differs";return fail_bundle==2?-1:-2;}
    CHECK(!q->current(q->opaque));bundle_checkpoint_calls++;total_bundle_checkpoint_calls++;
    if(control->checkpoint(control->opaque)){r->stage="bundle_fixture_checkpoint_refused";r->reason="bundle_authority_changed";return -1;}
    return 0;
}
int bw_nrl_begin(const struct bw_nrl_request *q,struct bw_nrl_owner **out,struct bw_nrc_result *r){
    CHECK(q&&q->selection&&live&&bundle_calls==1&&bundle_checkpoint_calls==2&&!register_calls&&!q->current(q->opaque,BW_NRL_REGISTER));begin_calls++;
    if(fail_begin){*r=(struct bw_nrc_result){.state=BW_NRC_UNKNOWN,.reason="ownership_intent_current_sources"};if(late_startup_drift)startup_changed=1;if(late_settings_drift)settings_changed=1;return begin_release_unknown?BW_NRC_RELEASE_UNCONFIRMED:-1;}
    copied_request=*q;borrowed_request=&copied_request;retained_registration=*out=(struct bw_nrl_owner *)(uintptr_t)0x5678;return 0;
}
int bw_nrl_registered(struct bw_nrl_owner *p,int status,const struct bw_register_result *r,struct bw_nrc_result *out){CHECK(p==retained_registration&&register_calls==1&&r&&out);commit_calls++;if(fail_commit)out->reason="ownership_commit_fixture_refused";
    if(commit_release_unknown){registration_close_failure=1;out->state=BW_NRC_UNKNOWN;out->reason="registration_ticket_release_unconfirmed";return BW_NRC_RELEASE_UNCONFIRMED;}return fail_commit||status?-1:0;}
int bw_nrl_retire(struct bw_nrl_owner *p,const struct bw_native_reader_release *r,struct bw_nrc_retirement_result *out){
    CHECK(p==retained_registration&&r&&!live&&closed==1&&reader_calls==1&&owner.playing.retiring);retire_calls++;
    if(borrowed_request->current(borrowed_request->opaque,BW_NRL_RETIRE))return -1;
    CHECK(r->runtime_released&&r->io_released&&r->private_released&&!strcmp(r->run,selection.run));
    *out=(struct bw_nrc_retirement_result){.registration={.state=pending_cleanup?BW_NRC_PENDING:BW_NRC_ABSENT}};return 0;
}
int bw_nrl_destroy(struct bw_nrl_owner **p){CHECK(p);if(*p){CHECK(*p==retained_registration);destroy_calls++;*p=NULL;retained_registration=NULL;borrowed_request=NULL;}return registration_close_failure?-1:0;}
int bw_nrc_candidates(const struct bw_nrc_environment *e,struct bw_nrc_candidate *out,size_t cap,size_t *n){CHECK(e&&out&&cap&&n);*n=0;if(pending_cleanup||removed_cleanup){*n=1;strcpy(out[0].subject.title,"PPSA99999");strcpy(out[0].subject.game,removed_cleanup?removed_record.identity.binding.game:"fixture");strcpy(out[0].subject.run,selection.run);out[0].pending=out[0].retiring=!!pending_cleanup;}return 0;}
int bw_removed_list(const char *data,struct bw_removed_source *out,size_t cap,size_t *n){CHECK(data&&out&&cap&&n);*n=0;if(removed_record.operation[0]){*n=1;*out=removed_record;}return 0;}
int bw_nrl_absent_sources(const struct bw_nrc_environment *env,struct bw_nrc_remote *r,const struct bw_nrc_subject *s){CHECK(env&&env->apps_root&&env->database&&r&&s&&!r->connection.current(r->connection.opaque));removed_sources_queries++;return removed_source_unknown?-1:0;}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation op,const char *path,struct bw_sm_reply *reply){CHECK(c&&c->current&&!c->current(c->opaque)&&op==BW_SM_MANUAL_LIST&&path&&reply);memset(reply,0,sizeof *reply);removed_manual_queries++;return 0;}
int bw_native_game_uninstall_probe(const struct bw_nrc_environment *e,const struct bw_provider_binding *id,const struct bw_native_prepared *prepared,struct bw_game_uninstall_capability *out){CHECK(e&&id&&prepared&&out);*out=(struct bw_game_uninstall_capability){.removed=1,.can_restore=1};return 0;}
int bw_nrc_observe_removed(const struct bw_nrc_environment *e,const char title[10],const char run[33],const struct bw_removed_source *removed,const struct bw_nrc_removed_observer *ops,struct bw_nrc_result *out){
    CHECK(e&&title&&run&&removed&&ops&&out&&!strcmp(title,removed_record.identity.binding.title)&&!strcmp(run,selection.run));removed_observers++;
    struct bw_nrc_subject s={0};strcpy(s.title,title);strcpy(s.source,"/fixture/selected");
    if(removed_observe_unknown||ops->current(ops->opaque)||ops->sources(ops->opaque,&s)||ops->manual_present(ops->opaque,&s)){
        *out=(struct bw_nrc_result){.state=BW_NRC_UNKNOWN,.reason="Observed owner evidence remains uncertain."};return -1;}
    removed_cleanup=0;*out=(struct bw_nrc_result){.state=BW_NRC_ABSENT,.reason="Exact Removed owner observed absent."};return 0;
}
int bw_nrl_observe(const struct bw_nrc_environment *e,const char title[10],const char run[33],uint16_t port,int (*guard)(void *,enum bw_nrl_control),void *p,struct bw_nrc_retirement_result *out){
    CHECK(e&&title&&run&&!port&&guard&&!guard(p,BW_NRL_RETIRE));observer_calls++;
    *out=(struct bw_nrc_retirement_result){.registration={.state=BW_NRC_ABSENT,.reason="Observed absent; no request."}};pending_cleanup=0;return 0;
}
static void observe(void *p,const struct bw_reader_event *e){CHECK(p==&owner&&e&&e->phase);if(!strcmp(e->phase,"error")){errors++;snprintf(last_error,sizeof last_error,"%s",e->message?e->message:"");}}
static void reset(void){
    CHECK(!live);memset(&owner,0,sizeof owner);CHECK(!pthread_mutex_init(&owner.lock,NULL));owner.active=1;owner.worker_kind=WORK_PLAY;owner.generation=4;owner.settings_revision=2;owner.play_epoch=9;
    owner.opaque=&owner;owner.ops.read_settings=settings;owner.ops.idle=idle;owner.ops.play=play;owner.playing.selection.connection.present=1;
    owner.settings_status=BW_SETUP_VALID;owner.settings_generation=owner.playing.selection.connection;
    owner.playing.selection.identity.kind=BW_PROVIDER_FOLDER;owner.playing.selection.identity.policy=1;
    owner.playing.selection.prepared.state=BW_PREPARED_VALID;memset(&selection,0,sizeof selection);strcpy(selection.run,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    strcpy(selection.title,"PPSA99999");strcpy(owner.external_nonce,"cccccccccccccccccccccccccccccccc");
    strcpy(owner.playing.selection.run,selection.run);
    static const char game[]="PPSA99999_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    strcpy(selection.game,game);strcpy(owner.playing.selection.identity.binding.game,game);
    strcpy(owner.playing.selection.identity.binding.title,"PPSA99999");
    strcpy(owner.playing.selection.prepared.game,game);strcpy(owner.playing.selection.prepared.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    memset(owner.playing.selection.prepared.record_sha256,0x45,32);
    for(unsigned i=0;i<32;i++)snprintf(selection.prepared_record_hex+2*i,3,"%02x",owner.playing.selection.prepared.record_sha256[i]);

    opened=closed=selected_calls=strict_calls=register_calls=reader_calls=owned_calls=errors=setting_calls=0;
    bundle_calls=begin_calls=commit_calls=retire_calls=destroy_calls=observer_calls=idle_calls=bundle_checkpoint_calls=0;
    fail_bundle=fail_begin=fail_commit=cancelled_reader=pending_cleanup=unqualified_close=new_game=change_reservation=log_busy=startup_changed=0;release_flags=7;
    settings_status=BW_SETUP_VALID;query_mutation=0;
    removed_cleanup=removed_observers=removed_manual_queries=removed_sources_queries=0;removed_observe_unknown=removed_source_unknown=0;memset(&removed_record,0,sizeof removed_record);
    fail_open=fail_register=fail_reader=settings_changed=foreign_registration=0;
    last_error[0]=0;
}
static int execute(void){int r=play(NULL,&owner.playing.selection,play_current,&owner,observe,&owner);CHECK(!live&&!retained_registration&&!pthread_mutex_destroy(&owner.lock));cases++;return r;}
int bw_startup_idle_ps5_cached(const struct bw_startup_idle_request *r,struct bw_idle_log_cache *c,const char operation[33]){
    CHECK(r&&c&&operation);return new_game||log_busy||r->current(r->opaque)?-1:0;
}
int __real_bw_idle_log_cache_close(struct bw_idle_log_cache **,struct bw_idle_log_cache_release *);
int __wrap_bw_idle_log_cache_close(struct bw_idle_log_cache **p,struct bw_idle_log_cache_release *r){
    int closed=__real_bw_idle_log_cache_close(p,r);
    if(!closed&&cache_close_failure){cache_close_failure=0;r->release_unknown=1;return BW_IDLE_CACHE_RELEASE_UNKNOWN;}
    return closed;
}
static int no_close(void *p,const char *run){(void)p;(void)run;CHECK(0);return -1;}
static int no_prepare(void *p,struct bw_share_config *c,const struct bw_native_prepare_selection *s,int (*g)(void *),void *o,bw_source_observer_fn fn,void *fo,struct bw_native_prepared *out){
    (void)p;(void)c;(void)s;(void)g;(void)o;(void)fn;(void)fo;(void)out;CHECK(0);return -1;
}
static void begin_case(void){
    reset();final_admission_checks=0;late_startup_drift=late_settings_drift=admission_close_failure=cache_close_failure=workspace_pending=reader_error_mode=owned_failure=registration_close_failure=begin_release_unknown=commit_release_unknown=0;
    owner.phase=BACKEND_READY;owner.settings_enabled=owner.settings_known=1;owner.ops.close=no_close;owner.ops.prepare=no_prepare;
    owner.jobs=bw_native_jobs_create();CHECK(owner.jobs);owner.library.catalog.count=1;
    owner.library.catalog.items=calloc(1,sizeof *owner.library.catalog.items);owner.library.prepared=calloc(1,sizeof *owner.library.prepared);CHECK(owner.library.catalog.items&&owner.library.prepared);
    struct bw_catalog_item *row=owner.library.catalog.items;strcpy(row->source,"Fixture Game");strcpy(row->title,"Fixture Game");strcpy(row->title_id,"PPSA99999");row->state=BW_CATALOG_FOLDER;
    strcpy(owner.playing.selection.identity.binding.source,row->source);strcpy(selection.source,row->source);owner.library.prepared[0]=owner.playing.selection.prepared;
    strcpy(owner.playing.phase,"preflight");strcpy(owner.playing.message,"Checking the selected PC and console files.");
}
static void joined_worker(void){
    pthread_t thread;CHECK(!pthread_create(&thread,NULL,play_worker,&owner));CHECK(!pthread_join(thread,NULL));
    CHECK(!owner.active&&!live&&!retained_registration);cases++;
}
static void flags(unsigned ready,unsigned sendable,unsigned playable){
    struct bw_native_response r={0};allow_locked_settings=1;CHECK(!state(&owner,&r)&&r.status==200);allow_locked_settings=0;char *json=calloc(1,r.size+1);CHECK(json);memcpy(json,r.bytes,r.size);
    CHECK(strstr(json,ready?"\"startupReady\":true":"\"startupReady\":false"));
    CHECK(strstr(json,sendable?"\"canPrepare\":true":"\"canPrepare\":false"));
    CHECK(strstr(json,playable?"\"canPlay\":true":"\"canPlay\":false"));
    CHECK(strstr(json,"\"onConsole\":true"));free(json);bw_native_response_clear(&r);
}
static void end_case(void){
    CHECK(owner.library.prepared[0].state==BW_PREPARED_VALID);CHECK(!bw_native_jobs_destroy(owner.jobs));
    free(owner.library.catalog.items);free(owner.library.prepared);CHECK(!pthread_mutex_destroy(&owner.lock));
}
static int generic_play(void *p,const struct bw_native_play_selection *s,int (*g)(void *),void *go,void (*fn)(void *,const struct bw_reader_event *),void *fo){
    CHECK(p==&owner&&s&&g&&!g(go));struct bw_reader_event e={.phase="launching",.message="Generic stage",.pid=0};
    if(reader_error_mode==2)fn(fo,&e);e.phase="error";e.message="Generic unclassified failure";fn(fo,&e);return -1;
}
#ifdef TEST_UNCHANGED_BASELINE
#define CONTROL_VERIFIED 0
#else
#define CONTROL_VERIFIED owner.playing.preflight_control_verified
#endif
int main(void){
    begin_case();fail_begin=1;joined_worker();CHECK(begin_calls==1&&!register_calls&&!reader_calls&&owner.playing.preflight_failed);
#ifdef TEST_UNCHANGED_BASELINE
    CHECK(owner.revalidation_count==1);flags(0,1,0);end_case();
    printf("{\"passed\":true,\"baselineControlFailureIncorrectlyInvalidates\":true,\"cases\":%u,\"checks\":%u}\n",cases,checks);return 0;
#else
    CHECK(CONTROL_VERIFIED&&final_admission_checks==1&&!owner.revalidation_count&&strstr(owner.last_failure.message,"ownership_intent_current_sources"));flags(1,0,1);
    /* A later metadata-only observation cannot invent a need for Send. */
    owner.library.prepared[0]=owner.playing.selection.prepared;owner.library.prepared[0].bytes_verified=0;flags(1,0,1);end_case();
    begin_case();fail_register=1;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count&&register_calls==1&&!reader_calls);flags(1,0,1);end_case();
    begin_case();fail_commit=1;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count&&commit_calls==1&&!reader_calls);flags(1,0,1);end_case();
    begin_case();owned_failure=1;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count&&!register_calls&&!reader_calls);flags(1,0,1);end_case();
    begin_case();fail_bundle=2;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count&&!begin_calls&&!reader_calls);flags(1,0,1);end_case();
    begin_case();fail_open=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.revalidation_count==1&&!begin_calls&&!reader_calls);flags(0,1,0);end_case();
    begin_case();fail_begin=1;late_startup_drift=1;joined_worker();CHECK(!CONTROL_VERIFIED&&final_admission_checks==1&&owner.revalidation_count==1);flags(0,1,0);end_case();
    begin_case();fail_begin=1;late_settings_drift=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.playing.changed&&!owner.revalidation_count);end_case();
    /* Unknown descriptor release keeps global CONTROL and blocks another Send. */
    begin_case();fail_begin=1;admission_close_failure=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.revalidation_count==1&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown"));flags(0,0,0);end_case();
    begin_case();reader_error_mode=1;joined_worker();CHECK(!CONTROL_VERIFIED&&reader_calls==1&&owner.revalidation_count==1&&!owner.playing.launch_seen);flags(0,1,0);end_case();
    begin_case();reader_error_mode=2;joined_worker();CHECK(!CONTROL_VERIFIED&&reader_calls==1&&!owner.revalidation_count&&owner.playing.launch_seen);flags(1,0,1);end_case();
    begin_case();owner.ops.play=generic_play;joined_worker();CHECK(!CONTROL_VERIFIED&&!opened&&owner.revalidation_count==1);flags(0,1,0);end_case();
    begin_case();owner.ops.play=generic_play;reader_error_mode=2;joined_worker();CHECK(!CONTROL_VERIFIED&&!opened&&!owner.revalidation_count&&owner.playing.launch_seen);flags(1,0,1);end_case();
    begin_case();fail_begin=1;pending_cleanup=1;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count&&registration_waiting(&owner,"PPSA99999"));flags(0,0,0);end_case();
    begin_case();fail_bundle=2;workspace_pending=1;joined_worker();CHECK(CONTROL_VERIFIED&&!owner.revalidation_count);flags(0,0,0);end_case();
    begin_case();cache_close_failure=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown")&&!reader_calls);end_case();
    begin_case();fail_register=1;registration_close_failure=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.revalidation_count==1&&!reader_calls&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown"));end_case();
    begin_case();fail_begin=1;begin_release_unknown=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown")&&!register_calls&&!reader_calls&&owner.revalidation_count==1);end_case();
    begin_case();commit_release_unknown=1;joined_worker();CHECK(!CONTROL_VERIFIED&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown")&&destroy_calls==1&&!reader_calls&&owner.revalidation_count==1);end_case();
    CHECK(total_bundle_checkpoint_calls>0);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"controlledBundleCheckpoints\":%u,\"actualDefaultPlayAndJoinedWorker\":true,\"actualStateJson\":true,\"sourceAdmissionAndTransportModeled\":true,\"consoleActions\":0}\n",cases,checks,total_bundle_checkpoint_calls);return 0;
#endif
}
