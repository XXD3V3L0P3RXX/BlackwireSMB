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
static char candidate_binding_game[75];
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
    CHECK(p&&!live&&!*p&&root&&id==&owner.playing.selection.identity&&expected==&owner.playing.selection.prepared&&run&&!guard(opaque));opened++;
    if(fail_open){*error="Required startup receipt changed; choose Send to repair it.";return fail_open==2?-2:-1;}
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
int bw_preload_bundle_upgrade_workspace(const char *data,const char *game){
    CHECK(data&&!strcmp(data,backend_data_root)&&game&&!strcmp(game,owner.playing.selection.prepared.game));return 0;
}
int bw_preload_bundle_upgrade_checked(const struct bw_preload_bundle_upgrade_request *upgrade,struct bw_preload_bundle_result *r){
    CHECK(upgrade&&upgrade->data_root&&!strcmp(upgrade->data_root,backend_data_root)&&upgrade->game&&
        !strcmp(upgrade->game,selection.game)&&!strcmp(upgrade->game,owner.playing.selection.prepared.game)&&
        upgrade->prepared_run&&!strcmp(upgrade->prepared_run,owner.playing.selection.prepared.run)&&
        upgrade->launch_run&&!strcmp(upgrade->launch_run,selection.run)&&!strcmp(upgrade->launch_run,owner.playing.selection.run)&&
        upgrade->prepared_record_sha256==owner.playing.selection.prepared.record_sha256);
    const struct bw_preload_bundle_request *q=&upgrade->bundle;
    char expected_root[1152];CHECK(snprintf(expected_root,sizeof expected_root,"%s/games/%s",backend_data_root,selection.game)>0&&
        q->root&&!strcmp(q->root,expected_root));

    CHECK(q&&r&&live&&!register_calls&&!begin_calls);if(q->current(q->opaque))return -2;bundle_calls++;
    for(unsigned i=0;i<cache_repeats;i++)if(q->current(q->opaque))return -2;
    if(cache_fault==1)cache_pending=1;
    if(fail_bundle){r->stage="bundle_fixture_exact_bytes_refused";r->reason="bundle_fixture_named_identity_differs";}return fail_bundle==2?-1:fail_bundle?-2:0;
}
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request *upgrade,const struct bw_preload_bundle_control *control,struct bw_preload_bundle_result *r){
    CHECK(upgrade&&control&&control->checkpoint&&r);
    struct bw_preload_bundle_upgrade_request scoped=*upgrade;
    scoped.bundle.current=control->checkpoint;scoped.bundle.opaque=control->opaque;
    int result=bw_preload_bundle_upgrade_checked(&scoped,r);r->release_unknown=result==-2;return result;
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
int bw_nrc_candidates(const struct bw_nrc_environment *e,struct bw_nrc_candidate *out,size_t cap,size_t *n){CHECK(e&&out&&cap&&n);*n=0;if(pending_cleanup||removed_cleanup){*n=1;strcpy(out[0].subject.title,"PPSA99999");strcpy(out[0].subject.game,*candidate_binding_game?candidate_binding_game:removed_cleanup?removed_record.identity.binding.game:"fixture");strcpy(out[0].subject.run,selection.run);out[0].pending=out[0].retiring=!!pending_cleanup;}return 0;}
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
void bw_native_sensitive_clear(void *p,size_t n){memset(p,0,n);}
static void observe(void *p,const struct bw_reader_event *e){CHECK(p==&owner&&e&&e->phase);if(!strcmp(e->phase,"error")){errors++;snprintf(last_error,sizeof last_error,"%s",e->message?e->message:"");}}
static void reset(void){
    CHECK(!live);memset(&owner,0,sizeof owner);CHECK(!pthread_mutex_init(&owner.lock,NULL));owner.active=1;owner.worker_kind=WORK_PLAY;owner.generation=4;owner.settings_revision=2;owner.play_epoch=9;
    owner.opaque=&owner;owner.ops.read_settings=settings;owner.ops.idle=idle;owner.ops.play=play;owner.playing.selection.connection.present=1;
    owner.settings_status=BW_SETUP_VALID;owner.settings_generation=owner.playing.selection.connection;
    owner.playing.selection.identity.kind=BW_PROVIDER_FOLDER;owner.playing.selection.identity.policy=1;
    owner.playing.selection.prepared.state=BW_PREPARED_VALID;memset(&selection,0,sizeof selection);strcpy(selection.run,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    strcpy(selection.title,"PPSA99999");strcpy(owner.external_nonce,"cccccccccccccccccccccccccccccccc");
    strcpy(owner.playing.selection.run,selection.run);
    strcpy(owner.playing.dispatch,owner.external_nonce);
    static const char game[]="PPSA99999_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    strcpy(selection.game,game);strcpy(owner.playing.selection.identity.binding.game,game);
    strcpy(owner.playing.selection.identity.binding.title,"PPSA99999");
    strcpy(owner.playing.selection.prepared.game,game);strcpy(owner.playing.selection.prepared.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    memset(owner.playing.selection.prepared.record_sha256,0x45,32);
    for(unsigned i=0;i<32;i++)snprintf(selection.prepared_record_hex+2*i,3,"%02x",owner.playing.selection.prepared.record_sha256[i]);

    opened=closed=selected_calls=strict_calls=register_calls=reader_calls=owned_calls=errors=setting_calls=0;
    bundle_calls=begin_calls=commit_calls=retire_calls=destroy_calls=observer_calls=idle_calls=0;
    fail_bundle=fail_begin=fail_commit=cancelled_reader=pending_cleanup=unqualified_close=new_game=change_reservation=log_busy=startup_changed=0;release_flags=7;
    settings_status=BW_SETUP_VALID;query_mutation=0;
    removed_cleanup=removed_observers=removed_manual_queries=removed_sources_queries=0;removed_observe_unknown=removed_source_unknown=0;memset(&removed_record,0,sizeof removed_record);candidate_binding_game[0]=0;
    fail_open=fail_register=fail_reader=settings_changed=foreign_registration=0;
    last_error[0]=0;
    CHECK(!cache_live);memset(cache_owned,0,sizeof cache_owned);cache_calls=cache_lines=cache_queries=cache_repeats=cache_testing=cache_fault=cache_pending=cache_closed_calls=0;cache_shift=0;backend_game_log=cache_log_path;
}
static int execute(void){cache_testing=1;int r=play(NULL,&owner.playing.selection,play_current,&owner,observe,&owner);cache_testing=0;CHECK(!cache_live&&!live&&!retained_registration&&!pthread_mutex_destroy(&owner.lock));cases++;return r;}
static int retained_routing_main(void){
    reset();CHECK(!execute()&&opened==1&&closed==1&&selected_calls==1&&!strict_calls&&register_calls==1&&reader_calls==1&&!errors);cases++;
    reset();fail_open=1;CHECK(execute()!=0&&opened==1&&!closed&&!register_calls&&!reader_calls&&errors==1);cases++;
    reset();fail_register=1;CHECK(execute()!=0&&opened==1&&closed==1&&register_calls==1&&!reader_calls&&errors==1&&strstr(last_error,"Registered source mismatch")&&strstr(last_error,"add=1 confirmed=1 scan=0 registered=0"));cases++;
    reset();fail_reader=1;CHECK(execute()!=0&&opened==1&&closed==1&&reader_calls==1);cases++;
    reset();settings_changed=1;CHECK(execute()!=0&&!opened&&!register_calls&&!reader_calls);cases++;
    reset();foreign_registration=1;CHECK(execute()!=0&&closed==1&&reader_calls==1);cases++;
    reset();owner.playing.selection.prepared.prepared_policy=2;CHECK(execute()!=0&&!opened&&!register_calls&&!reader_calls);cases++;
    reset();fail_bundle=1;CHECK(execute()!=0&&bundle_calls==1&&!begin_calls&&!register_calls&&!reader_calls&&!retire_calls&&strstr(last_error,"bundle_fixture_exact_bytes_refused")&&strstr(last_error,"bundle_fixture_named_identity_differs"));cases++;
    reset();new_game=1;CHECK(execute()!=0&&!bundle_calls&&!begin_calls&&!register_calls&&!reader_calls&&!strcmp(last_error,"registration_full_idle_refused"));cases++;
    reset();log_busy=1;CHECK(execute()!=0&&!bundle_calls&&!begin_calls&&!register_calls&&!reader_calls&&!strcmp(last_error,"registration_game_log_idle_refused"));cases++;
    reset();startup_changed=1;CHECK(execute()!=0&&!bundle_calls&&!begin_calls&&!register_calls&&!reader_calls&&!strcmp(last_error,"registration_startup_current_refused"));cases++;
    reset();fail_begin=1;CHECK(execute()!=0&&begin_calls==1&&!register_calls&&!reader_calls&&!retire_calls);cases++;
    reset();fail_commit=1;CHECK(execute()!=0&&commit_calls==1&&!reader_calls&&!retire_calls&&destroy_calls==1&&strstr(last_error,"ownership_commit_fixture_refused")&&strstr(last_error,"add=1 confirmed=1 scan=0 registered=1"));cases++;
    for(unsigned flags=0;flags<7;flags++){reset();release_flags=(int)flags;CHECK(!execute()&&!retire_calls&&destroy_calls==1);cases++;}
    reset();cancelled_reader=1;CHECK(!execute()&&retire_calls==1&&owner.playing.close_requested);cases++;
    reset();pending_cleanup=1;CHECK(!execute()&&retire_calls==4&&owner.playing.cleanup_attention&&registration_waiting(&owner,"PPSA99999")&&!registration_waiting(&owner,"PPSA10002"));cases++;
    reset();unqualified_close=1;CHECK(!execute()&&retire_calls==1&&owner.playing.cleanup_attention);cases++;
    reset();owner.active=0;pending_cleanup=1;CHECK(!pthread_mutex_init(&owner.dispatch_lock,NULL));
    struct bw_native_cleanup_observation observed;CHECK(!bw_native_backend_registration_observe(&owner,owner.external_nonce,&observed)&&observed.absent&&!observed.pending&&observer_calls==1&&!owner.registration_pending_count);CHECK(!pthread_mutex_destroy(&owner.dispatch_lock));CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
    reset();owner.active=0;pending_cleanup=1;CHECK(!pthread_mutex_init(&owner.dispatch_lock,NULL));new_game=1;CHECK(bw_native_backend_registration_observe(&owner,owner.external_nonce,&observed)<0&&!observer_calls);CHECK(!pthread_mutex_destroy(&owner.dispatch_lock));CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
    reset();owner.active=0;pending_cleanup=1;CHECK(!pthread_mutex_init(&owner.dispatch_lock,NULL));change_reservation=1;CHECK(bw_native_backend_registration_observe(&owner,"cccccccccccccccccccccccccccccccc",&observed)<0&&!observer_calls);CHECK(!pthread_mutex_destroy(&owner.dispatch_lock));CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
    for(unsigned mode=0;mode<10;mode++){
        reset();owner.active=0;CHECK(!pthread_mutex_init(&owner.dispatch_lock,NULL));memset(owner.external_nonce,0,33);strcpy(owner.setup_token,"dddddddddddddddddddddddddddddddd");
        if(mode==1)settings_status=owner.settings_status=BW_SETUP_MISSING;
        if(mode==2)settings_status=owner.settings_status=BW_SETUP_INVALID;
        CHECK(!bw_native_backend_external_reserve(&owner,"cccccccccccccccccccccccccccccccc",owner.setup_token));
        if(mode==3)owner.active=1;else if(mode==4)owner.control_release_unknown=1;else if(mode==5)settings_changed=1;
        else if(mode==6)owner.settings_revision++;else if(mode==7)owner.play_epoch++;else if(mode==8)owner.setup_operation++;else if(mode==9)query_mutation=1;
        unsigned before_reader=reader_calls,before_register=register_calls,before_retire=retire_calls;
        int checked=bw_native_backend_external_current(&owner,"cccccccccccccccccccccccccccccccc");CHECK(mode<3?!checked:checked<0);
        CHECK(before_reader==reader_calls&&before_register==register_calls&&before_retire==retire_calls);
        CHECK(bw_native_backend_external_current(&owner,"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")<0);
        owner.active=0;CHECK(!bw_native_backend_external_release(&owner,"cccccccccccccccccccccccccccccccc"));CHECK(bw_native_backend_external_current(&owner,"cccccccccccccccccccccccccccccccc")<0);
        CHECK(!pthread_mutex_destroy(&owner.dispatch_lock)&&!pthread_mutex_destroy(&owner.lock));cases++;
    }
    for(unsigned mode=0;mode<5;mode++){
        reset();owner.active=0;CHECK(!pthread_mutex_init(&owner.dispatch_lock,NULL));removed_cleanup=1;
        removed_record.identity.kind=BW_PROVIDER_FOLDER;removed_record.identity.policy=BW_PROVIDER_POLICY_V1;
        strcpy(removed_record.identity.binding.title,"PPSA99999");strcpy(removed_record.identity.binding.game,"fixture");strcpy(removed_record.identity.binding.source,"Fixture");
        strcpy(removed_record.operation,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");memset(removed_record.prepared_sha256,0x45,32);
        owner.removing.state=REMOVAL_UNKNOWN;owner.removing.selected.identity=removed_record.identity;strcpy(owner.removing.selected.operation,removed_record.operation);memcpy(owner.removing.selected.prepared.record_sha256,removed_record.prepared_sha256,32);
        if(mode==1)removed_observe_unknown=1;else if(mode==2)removed_source_unknown=1;else if(mode==3)owner.removing.selected.operation[0]='b';else if(mode==4)new_game=1;
        CHECK(!registration_pending_refresh(&owner)&&registration_waiting(&owner,"PPSA99999"));
        struct bw_native_prepared prepared={.state=BW_PREPARED_ABSENT};struct bw_game_uninstall_capability cap;
        CHECK(!default_removal_probe(NULL,&removed_record.identity,&prepared,&cap)&&cap.removed&&!cap.can_restore);
        char nonce_before[33];memcpy(nonce_before,owner.external_nonce,33);unsigned generation=owner.generation;
        int result=bw_native_backend_registration_observe(&owner,owner.external_nonce,&observed);
        CHECK(!strcmp(owner.external_nonce,nonce_before)&&!register_calls&&!retire_calls&&!reader_calls);
        if(mode==0||mode==3){CHECK(!result&&observed.absent&&!observed.pending&&!registration_waiting(&owner,"PPSA99999")&&owner.generation==generation+1&&removed_observers==1&&removed_manual_queries==2&&removed_sources_queries==1);CHECK(owner.removing.state==(mode?REMOVAL_UNKNOWN:REMOVAL_DONE));}
        else CHECK(result<0&&!observed.absent&&owner.removing.state==REMOVAL_UNKNOWN&&owner.generation==generation);
        CHECK(!pthread_mutex_destroy(&owner.dispatch_lock)&&!pthread_mutex_destroy(&owner.lock));cases++;
    }
    /* A removed folder copy does not own a newly prepared compressed copy
     * of the same title. A real pending event must still block that title. */
    for(unsigned pending=0;pending<2;pending++){
        reset();owner.active=0;removed_cleanup=1;pending_cleanup=(int)pending;
        removed_record.identity.kind=BW_PROVIDER_FOLDER;removed_record.identity.policy=BW_PROVIDER_POLICY_V1;
        strcpy(removed_record.identity.binding.title,"PPSA99999");
        strcpy(removed_record.identity.binding.game,"removed-folder-binding");
        strcpy(removed_record.identity.binding.source,"Extracted fixture");
        strcpy(removed_record.operation,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        strcpy(candidate_binding_game,"prepared-container-binding");
        CHECK(!registration_pending_refresh(&owner));
        CHECK(!!registration_waiting(&owner,"PPSA99999")==!!pending);
        struct registration_observation_guard guard={0};
        struct bw_native_cleanup_observation untouched={0};
        CHECK(registration_observe_removed(&owner,&guard,&registration_environment,&untouched)==1);
        CHECK(!untouched.pending&&!removed_observers&&!removed_manual_queries&&!removed_sources_queries);
        CHECK(!register_calls&&!retire_calls&&!reader_calls);
        CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualDefaultPlayRouting\":true,\"admissionTransportModeled\":true}\n",cases,checks);return 0;
}

static void *execute_joined(void *p){int *result=p;*result=play(NULL,&owner.playing.selection,play_current,&owner,observe,&owner);return NULL;}
static int explicit_uninstall(void *opaque,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *context,void (*progress)(void *,const struct bw_game_uninstall_progress *),void *observer,struct bw_game_uninstall_result *out){
    CHECK(opaque==&owner&&selected&&guard&&context==&owner&&progress&&observer==&owner);
    for(unsigned i=0;i<3;i++)CHECK(!guard(context));
    *out=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_DONE,.registration_confirmed=1,.source_absent=1,.prepared_record_absent=1,.library_removed=1};return 0;
}
static void explicit_owner(void){
    reset();owner.worker_kind=WORK_UNINSTALL;owner.removing.selected.connection=owner.playing.selection.connection;
    strcpy(owner.removing.selected.operation,owner.external_nonce);owner.removing.generation=owner.generation;owner.removing.epoch=owner.play_epoch;owner.removing.revision=owner.settings_revision;owner.removing.state=REMOVAL_RUNNING;
}
int main(void){
    strcpy(cache_log_path,"/tmp/blackwire-cache-callers-XXXXXX");int fd=mkstemp(cache_log_path);CHECK(fd>=0);
    FILE *f=fdopen(fd,"w");CHECK(f);for(unsigned i=0;i<20;i++)CHECK(fputs("ordinary complete line\n",f)>=0);CHECK(!fclose(f));
    CHECK(!retained_routing_main());unsigned retained=cases,focused=0;
    reset();fail_open=1;CHECK(execute()!=0&&opened==1&&!closed&&!selected_calls&&!bundle_calls&&!register_calls&&!reader_calls&&!owner.control_release_unknown);focused++;
    reset();fail_open=2;CHECK(execute()!=0&&opened==1&&!closed&&!selected_calls&&!bundle_calls&&!register_calls&&!reader_calls&&owner.control_release_unknown);focused++;
    reset();cache_repeats=32;CHECK(!execute());
    /* The source-generation check adds one full control checkpoint before
     * witness minting. It must reuse the cached log and release it normally. */
    CHECK(cache_calls==36&&cache_queries==144&&cache_lines==40);CHECK(!owner.control_release_unknown);focused++;
    reset();cache_repeats=32;fail_bundle=1;CHECK(execute()!=0&&!reader_calls&&!register_calls&&cache_lines==20&&owner.control_release_unknown);
    reset();cache_repeats=32;fail_bundle=2;CHECK(execute()!=0&&!reader_calls&&!register_calls&&cache_lines==20&&!owner.control_release_unknown);focused++;
    reset();cache_fault=1;CHECK(execute()!=0&&owner.control_release_unknown&&!register_calls&&!reader_calls&&!begin_calls);focused++;
    reset();cache_fault=2;CHECK(execute()!=0&&owner.control_release_unknown&&reader_calls==1&&retire_calls==1&&owner.playing.cleanup_attention);focused++;
    reset();cache_fault=3;CHECK(execute()!=0&&owner.control_release_unknown&&!bundle_calls&&!register_calls&&!reader_calls);focused++;
    reset();cache_fault=4;CHECK(execute()!=0&&!bundle_calls&&!register_calls&&!reader_calls&&!owner.control_release_unknown);cache_shift=0;focused++;
    reset();cancelled_reader=1;CHECK(!execute()&&retire_calls==1&&!owner.control_release_unknown&&cache_lines==40);focused++;
    reset();release_flags=0;CHECK(!execute()&&!retire_calls&&owner.control_release_unknown&&cache_lines==20);focused++;
    reset();cache_repeats=12;cache_testing=1;pthread_t worker;int outcome=-9;int create=pthread_create(&worker,NULL,execute_joined,&outcome);
    int joined=create?-1:pthread_join(worker,NULL);cache_testing=0;CHECK(!create&&!joined&&!outcome&&!live&&!retained_registration&&!cache_live&&cache_lines==40);CHECK(!pthread_mutex_destroy(&owner.lock));focused++;
    reset();cache_fault=1;cache_testing=1;create=pthread_create(&worker,NULL,play_worker,&owner);joined=create?-1:pthread_join(worker,NULL);cache_testing=0;
    CHECK(!create&&!joined&&!owner.active&&owner.control_release_unknown&&!strcmp(owner.playing.phase,"unknown")&&!live&&!retained_registration&&!cache_live&&!reader_calls&&!register_calls);
    CHECK(!pthread_mutex_destroy(&owner.lock));focused++;
    for(unsigned mode=0;mode<4;mode++){
        explicit_owner();if(mode==1)settings_changed=1;else if(mode==2)owner.removing.unknown=1;else if(mode==3)owner.external_nonce[0]='f';
        CHECK((removal_current(&owner)==0)==(mode==0));CHECK(!cache_calls&&!cache_lines);CHECK(!pthread_mutex_destroy(&owner.lock));focused++;
    }
    explicit_owner();owner.ops.uninstall=explicit_uninstall;create=pthread_create(&worker,NULL,removal_worker,&owner);joined=create?-1:pthread_join(worker,NULL);
    CHECK(!create&&!joined&&!owner.active&&owner.removing.state==REMOVAL_DONE&&!cache_calls&&!cache_lines&&setting_calls>=4&&idle_calls>=4);CHECK(!pthread_mutex_destroy(&owner.lock));focused++;
    CHECK(!unlink(cache_log_path));printf("{\"passed\":true,\"retainedRoutingCases\":%u,\"focusedCallerCases\":%u,\"checks\":%u,\"actualDefaultPlayAndJoinedWorker\":true,\"allCachesClosedBeforeHandoff\":true,\"releaseUnknownRetained\":true,\"consoleActions\":0}\n",retained,focused,checks);return 0;
}
