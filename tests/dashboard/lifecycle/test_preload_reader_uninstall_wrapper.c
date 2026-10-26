/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual default Play orchestration with narrow ownership spies. Actual
 * receipt/filesystem verification is tested by test_fast_ready_play.c. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#include "native_backend.c"
#include <assert.h>
#include "idle_log_cache.h"
#include "game_stop_identity.h"
#include <unistd.h>
static unsigned checks,cases,opened,closed,selected_calls,strict_calls,register_calls,reader_calls,owned_calls,errors,setting_calls;
static int fail_open,fail_register,fail_reader,settings_changed,foreign_registration;
static unsigned bundle_calls,begin_calls,commit_calls,retire_calls,destroy_calls,observer_calls,idle_calls;
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
static unsigned cache_calls,cache_lines,cache_queries,cache_repeats,cache_testing,cache_fault,cache_pending,cache_closed_calls;
static double cache_shift;
static char cache_log_path[256];
static unsigned cache_owned[8192],cache_live;

#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"fast routing case%u line%d: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
#include "source_ampr_admission_model.h"
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
int bw_native_setup_idle(void *p){(void)p;return 0;}
static int settings(void *p,struct bw_share_config *c,struct bw_setup_generation *g){CHECK(p==&owner&&!pthread_mutex_trylock(&owner.lock));CHECK(!pthread_mutex_unlock(&owner.lock));setting_calls++;memset(c,0,sizeof *c);*g=owner.playing.selection.connection;if(settings_changed)g->sha256[0]^=1;if(query_mutation)owner.setup_operation++;return settings_status;}
static int idle(void *p){CHECK(p==&owner&&!pthread_mutex_trylock(&owner.lock));CHECK(!pthread_mutex_unlock(&owner.lock));idle_calls++;if(change_reservation)owner.external_nonce[0]='f';return new_game?-1:0;}
int bw_startup_idle_ps5(void *opaque){struct bw_startup_idle_request *q=opaque;CHECK(q&&q->current&&q->game_log);return new_game||log_busy||q->current(q->opaque)?-1:0;}
int __real_open(const char *,int,...);int __real_dup(int);int __real_close(int);int __real_fclose(FILE *);char *__real_fgets(char *,int,FILE *);int __real_clock_gettime(clockid_t,struct timespec *);
static void cache_own(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&!cache_owned[fd]);cache_owned[fd]=1;cache_live++;}
static void cache_drop_fd(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&cache_owned[fd]&&cache_live);cache_owned[fd]=0;cache_live--;}
int __wrap_open(const char *name,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}int fd=__real_open(name,flags,mode);if(cache_testing&&fd>=0)cache_own(fd);return fd;}
int __wrap_dup(int fd){int n=__real_dup(fd);if(cache_testing&&n>=0)cache_own(n);return n;}
int __wrap_close(int fd){if(cache_testing)cache_drop_fd(fd);int rc=__real_close(fd);if(cache_testing){cache_closed_calls++;if(cache_pending){cache_pending=0;errno=EIO;return -1;}}return rc;}
int __wrap_fclose(FILE *f){int fd=fileno(f);if(cache_testing)cache_drop_fd(fd);int rc=__real_fclose(f);if(cache_testing&&cache_fault==3){cache_fault=0;errno=EIO;return EOF;}return rc;}
char *__wrap_fgets(char *s,int n,FILE *f){char *v=__real_fgets(s,n,f);if(cache_testing&&v)cache_lines++;return v;}
int __wrap_clock_gettime(clockid_t id,struct timespec *t){int rc=__real_clock_gettime(id,t);if(!rc&&id==CLOCK_MONOTONIC){long long ns=(long long)(cache_shift*1e9);t->tv_sec+=ns/1000000000;t->tv_nsec+=ns%1000000000;if(t->tv_nsec>=1000000000){t->tv_nsec-=1000000000;t->tv_sec++;}}return rc;}
static void cache_put32(unsigned char *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(8*i));}
static void cache_put64(unsigned char *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(8*i));}
static uint32_t cache_firmware(void *p){(void)p;return BW_STOP_PROFILE_FW;}
static struct bw_src_process_result cache_query(void *p,void *buffer,size_t capacity){
    (void)p;cache_queries++;struct bw_src_process_result out={.used=3*BW_STOP_PROFILE_RECORD};
    if(buffer){CHECK(capacity>=out.used);memset(buffer,0,out.used);for(unsigned i=0;i<3;i++){
        unsigned char *b=(unsigned char *)buffer+i*BW_STOP_PROFILE_RECORD;cache_put32(b,BW_STOP_PROFILE_RECORD);cache_put32(b+72,i==2?5:i);cache_put64(b+336,i==2?123:0);strcpy((char *)b+447,i==2?"payload.elf":i==1?"init":"kernel");}}
    return out;
}
int bw_startup_idle_ps5_cached(const struct bw_startup_idle_request *q,struct bw_idle_log_cache *c,const char operation[33]){
    CHECK(q&&q->current&&q->game_log&&c&&operation);double at=now();CHECK(q->deadline>at&&q->deadline<=at+2.01);
    cache_calls++;if(cache_fault==4)cache_shift=21;
    const struct bw_src_idle_ops ops={cache_firmware,cache_query,NULL};return new_game||log_busy?-1:bw_src_idle_evaluate_cached(q,&ops,c,operation);
}

int bw_native_launch_admission_open(struct bw_native_launch_admission **p,const char *root,const struct bw_provider_binding *id,const struct bw_native_prepared *expected,const char *run,int (*guard)(void *),void *opaque,const char **error){
    CHECK(p&&!live&&!*p&&root&&run&&!guard(opaque));
    if(owner.worker_kind==WORK_UNINSTALL){
        CHECK(id&&expected&&!memcmp(id,&owner.removing.selected.identity,sizeof *id)&&
            !memcmp(expected,&owner.removing.selected.prepared,sizeof *expected)&&!strcmp(run,owner.removing.selected.operation));
    }else CHECK(id==&owner.playing.selection.identity&&expected==&owner.playing.selection.prepared);
    opened++;
    if(fail_open){*error="Required startup receipt changed; choose Send to repair it.";return -1;}
    live=*p=(struct bw_native_launch_admission *)(uintptr_t)0x1234;ampr_model_open(live);return 0;
}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *out){CHECK(live&&p==live&&out);selected_calls++;*out=selection;return 0;}
int bw_native_launch_admission_current(struct bw_native_launch_admission *p,const char *root,const struct bw_provider_binding *id,const struct bw_reader_selection *s,struct bw_native_prepared *out){
    CHECK(live&&p==live&&root&&id&&s&&!strcmp(s->run,selection.run));if(out)*out=owner.playing.selection.prepared;return settings_changed||startup_changed?-1:0;
}
int bw_native_launch_admission_close(struct bw_native_launch_admission **p){CHECK(p);if(*p){CHECK(live==*p);closed++;live=NULL;*p=NULL;}return 0;}
int bw_native_launch_selection(const char *a,const struct bw_provider_binding *b,const struct bw_native_prepared *c,const char *d,struct bw_reader_selection *e){(void)a;(void)b;(void)c;(void)d;(void)e;strict_calls++;CHECK(0);return -1;}
int bw_native_owned_registration(const struct bw_native_register_request *r,struct bw_native_owned_registration_result *out){CHECK(r&&r->startup==live&&out);owned_calls++;return 0;}
int bw_native_register(const struct bw_native_register_request *r,struct bw_register_result *out){CHECK(r&&r->startup==live&&out&&!r->current(r->guard)&&begin_calls==1);register_calls++;*out=(struct bw_register_result){.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=!fail_register,.message="Registered source mismatch"};return fail_register?-1:0;}
int bw_native_registration_verify(const char *root,const char *apps,const char *db,const struct bw_reader_selection *s){CHECK(root&&apps&&db&&s);return foreign_registration?-1:0;}
int bw_native_reader_run(const struct bw_native_reader_request *r){
    CHECK(live&&r&&r->startup==live&&!r->current(r->opaque)&&!r->data_current(r->opaque));CHECK(!((struct play_observer *)r->opaque)->context.control_log&&!cache_live);reader_calls++;
    if(foreign_registration){CHECK(r->before_launch(r->opaque,&r->selection)!=0);return -1;}
    CHECK(!r->before_launch(r->opaque,&r->selection));CHECK(commit_calls==1&&r->released);
    if(cancelled_reader)owner.playing.close_requested=1;
    memcpy(r->released->run,selection.run,33);r->released->runtime_released=!!(release_flags&1);r->released->io_released=!!(release_flags&2);r->released->private_released=!!(release_flags&4);
    if(unqualified_close)owner.playing.changed=1;return fail_reader?-1:0;
}
int bw_preload_bundle_upgrade_checked(const struct bw_preload_bundle_upgrade_request *upgrade,struct bw_preload_bundle_result *r){
    CHECK(upgrade&&upgrade->data_root&&upgrade->game&&upgrade->prepared_run&&upgrade->launch_run&&upgrade->prepared_record_sha256);const struct bw_preload_bundle_request *q=&upgrade->bundle;
    CHECK(q&&r&&live&&!register_calls&&!begin_calls);if(q->current(q->opaque))return -2;bundle_calls++;
    for(unsigned i=0;i<cache_repeats;i++)if(q->current(q->opaque))return -2;
    if(cache_fault==1)cache_pending=1;
    if(fail_bundle){r->stage="bundle_fixture_exact_bytes_refused";r->reason="bundle_fixture_named_identity_differs";}return fail_bundle==2?-1:fail_bundle?-2:0;
}
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request *upgrade,const struct bw_preload_bundle_control *control,struct bw_preload_bundle_result *r){
    CHECK(control&&control->checkpoint);if(control->checkpoint(control->opaque))return -2;
    int result=bw_preload_bundle_upgrade_checked(upgrade,r);if(!result&&control->checkpoint(control->opaque))return -2;return result;
}
int bw_nrl_begin(const struct bw_nrl_request *q,struct bw_nrl_owner **out,struct bw_nrc_result *r){
    CHECK(q&&q->selection&&live&&bundle_calls==1&&!register_calls&&!q->current(q->opaque,BW_NRL_REGISTER));CHECK(!((struct play_observer *)q->opaque)->context.control_log&&!cache_live);begin_calls++;
    if(fail_begin){*r=(struct bw_nrc_result){.state=BW_NRC_PENDING,.reason="Exact title cleanup is pending."};return -1;}
    copied_request=*q;borrowed_request=&copied_request;retained_registration=*out=(struct bw_nrl_owner *)(uintptr_t)0x5678;return 0;
}
int bw_nrl_registered(struct bw_nrl_owner *p,int status,const struct bw_register_result *r,struct bw_nrc_result *out){CHECK(p==retained_registration&&register_calls==1&&r&&out);commit_calls++;if(fail_commit)out->reason="ownership_commit_fixture_refused";return fail_commit||status?-1:0;}
int bw_nrl_retire(struct bw_nrl_owner *p,const struct bw_native_reader_release *r,struct bw_nrc_retirement_result *out){
    CHECK(p==retained_registration&&r&&!live&&closed==1&&reader_calls==1&&owner.playing.retiring);retire_calls++;
    if(borrowed_request->current(borrowed_request->opaque,BW_NRL_RETIRE))return -1;
    CHECK(r->runtime_released&&r->io_released&&r->private_released&&!strcmp(r->run,selection.run));
    if(cache_fault==2)cache_pending=1;
    *out=(struct bw_nrc_retirement_result){.registration={.state=pending_cleanup?BW_NRC_PENDING:BW_NRC_ABSENT}};return 0;
}
int bw_nrl_destroy(struct bw_nrl_owner **p){CHECK(p);if(*p){CHECK(*p==retained_registration);destroy_calls++;*p=NULL;retained_registration=NULL;borrowed_request=NULL;}return 0;}
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
    opened=closed=selected_calls=strict_calls=register_calls=reader_calls=owned_calls=errors=setting_calls=0;
    bundle_calls=begin_calls=commit_calls=retire_calls=destroy_calls=observer_calls=idle_calls=0;
    fail_bundle=fail_begin=fail_commit=cancelled_reader=pending_cleanup=unqualified_close=new_game=change_reservation=log_busy=startup_changed=0;release_flags=7;
    settings_status=BW_SETUP_VALID;query_mutation=0;
    removed_cleanup=removed_observers=removed_manual_queries=removed_sources_queries=0;removed_observe_unknown=removed_source_unknown=0;memset(&removed_record,0,sizeof removed_record);
    fail_open=fail_register=fail_reader=settings_changed=foreign_registration=0;
    last_error[0]=0;
    CHECK(!cache_live);memset(cache_owned,0,sizeof cache_owned);cache_calls=cache_lines=cache_queries=cache_repeats=cache_testing=cache_fault=cache_pending=cache_closed_calls=0;cache_shift=0;backend_game_log=cache_log_path;
}
static int execute(void){cache_testing=1;int r=play(NULL,&owner.playing.selection,play_current,&owner,observe,&owner);cache_testing=0;CHECK(!cache_live&&!live&&!retained_registration&&!pthread_mutex_destroy(&owner.lock));cases++;return r;}
extern const uint8_t fixture_previous[];extern const size_t fixture_previous_size;
static unsigned selector_mode,select_calls,normal_calls;
int bw_preload_bundle_select_for_removal(const struct bw_preload_bundle_upgrade_request *q,struct bw_preload_reader_pin *out,struct bw_preload_bundle_result *d){CHECK(q&&out&&d&&q->bundle.root&&q->data_root&&!strcmp(q->game,owner.removing.selected.identity.binding.game)&&!strcmp(q->prepared_run,owner.removing.selected.prepared.run)&&!strcmp(q->launch_run,owner.removing.selected.operation)&&!memcmp(q->prepared_record_sha256,owner.removing.selected.prepared.record_sha256,32)&&!q->bundle.current(q->bundle.opaque));select_calls++;memset(out,0,sizeof *out);if(selector_mode==5){d->release_unknown=1;return 0;}if(selector_mode>=3)return selector_mode==4?-2:-1;
 if(!selector_mode){out->size=fixture_previous_size;cp_sha_bytes(fixture_previous,fixture_previous_size,out->sha256);}else{out->size=bw_preload_embedded_image.size;memcpy(out->sha256,bw_preload_embedded_image.sha256,32);}return 0;}
int bw_native_game_uninstall_normal(const struct bw_game_uninstall_request *q,uint16_t port,struct bw_game_uninstall_result *out){CHECK(q&&!port&&out&&select_calls==1&&!q->ops.current(q->ops.opaque));normal_calls++;uint8_t hash[32];if(!selector_mode){cp_sha_bytes(fixture_previous,fixture_previous_size,hash);CHECK(q->reader_size==fixture_previous_size&&!memcmp(q->reader_sha256,hash,32));}else CHECK(q->reader_size==bw_preload_embedded_image.size&&!memcmp(q->reader_sha256,bw_preload_embedded_image.sha256,32));
 *out=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_DONE,.registration_confirmed=1,.source_absent=1,.prepared_record_absent=1,.library_removed=1,.files_removed=3,.files_total=3};return 0;}
static void removal_fixture(void){reset();owner.worker_kind=WORK_UNINSTALL;owner.removing.selected.connection=owner.playing.selection.connection;owner.removing.selected.prepared.state=BW_PREPARED_VALID;strcpy(owner.removing.selected.prepared.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");memset(owner.removing.selected.prepared.record_sha256,1,32);strcpy(owner.removing.selected.identity.binding.title,"PPSA99999");snprintf(owner.removing.selected.identity.binding.game,sizeof owner.removing.selected.identity.binding.game,"PPSA99999_%064d",1);strcpy(owner.removing.selected.confirmed_title,"PPSA99999");strcpy(owner.removing.selected.operation,owner.external_nonce);owner.removing.generation=owner.generation;owner.removing.epoch=owner.play_epoch;owner.removing.revision=owner.settings_revision;owner.removing.state=REMOVAL_RUNNING;owner.ops.uninstall=default_uninstall;select_calls=normal_calls=0;}
int main(void){for(selector_mode=0;selector_mode<6;selector_mode++){removal_fixture();struct bw_game_uninstall_result out={0};int result=default_uninstall(NULL,&owner.removing.selected,removal_current,&owner,NULL,NULL,&out);CHECK(select_calls==1);if(selector_mode<3)CHECK(!result&&normal_calls==1&&out.phase==BW_GAME_REMOVE_DONE&&!out.release_unknown);else CHECK(result&&normal_calls==0&&!out.mutation_started&&out.release_unknown==(selector_mode>=4)&&out.phase==(selector_mode>=4?BW_GAME_REMOVE_UNKNOWN:BW_GAME_REMOVE_ERROR));CHECK(!pthread_mutex_destroy(&owner.lock));cases++;}
 for(selector_mode=3;selector_mode<6;selector_mode++){removal_fixture();pthread_t worker;int created=pthread_create(&worker,NULL,removal_worker,&owner);int joined=created?-1:pthread_join(worker,NULL);CHECK(!created&&!joined&&!owner.active&&!owner.removing.result.mutation_started&&!normal_calls&&!strcmp(owner.external_nonce,"cccccccccccccccccccccccccccccccc"));CHECK(owner.removing.state==(selector_mode>=4?REMOVAL_UNKNOWN:REMOVAL_ERROR)&&owner.removing.unknown==(selector_mode>=4)&&owner.control_release_unknown==(selector_mode>=4)&&owner.removing.result.release_unknown==(selector_mode>=4));CHECK(!pthread_mutex_destroy(&owner.lock));cases++;}
 printf("{\"passed\":true,\"scenarios\":9,\"checks\":%u,\"actualDefaultWrapperAndJoinedWorker\":true,\"releaseUnknownWithoutMutationRetained\":true}\n",checks);return 0;}
