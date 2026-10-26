/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual removal worker invokes the actual normal adapter/core. Only the
 * external settings/idle observation is a fixed host fixture. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#include "preload_bundle.h"
#include "native_backend.c"
static struct bw_native_backend release_owner;
static struct bw_share_config release_config;
static struct bw_setup_generation release_generation;
static const struct bw_game_uninstall_request *release_request;
static unsigned release_initialized,release_calls,release_force_idle,release_idle_calls,release_checks,release_pending;
static int release_status;
static unsigned setup_checks_done,setup_discovers,setup_saves;
static struct bw_native_backend *setup_checkpoint;
#define RCHECK(x) do{release_checks++;if(!(x)){fprintf(stderr,"release worker line %d: %s\n",__LINE__,#x);abort();}}while(0)
unsigned bw_test_uninstall_release_checks(void){return release_checks;}
static int release_settings(void *unused,struct bw_share_config *config,struct bw_setup_generation *generation){(void)unused;*config=release_config;*generation=release_generation;return BW_SETUP_VALID;}
void bw_test_uninstall_release_final_close(void){release_force_idle=1;}
void bw_test_uninstall_expect_pending(void){release_pending=1;}
static int release_idle(void *unused){(void)unused;return release_force_idle&&++release_idle_calls>1?-1:0;}
static int setup_nonce(void *unused,char nonce[33]){(void)unused;memset(nonce,'b',32);nonce[32]=0;return 0;}
static int setup_discover(void *opaque,struct bw_share_config *config,struct bw_native_library *out){
    struct bw_native_backend *p=opaque;RCHECK(config&&out);setup_discovers++;memset(out,0,sizeof *out);
    if(p==setup_checkpoint){RCHECK(!pthread_mutex_lock(&p->lock));p->control_release_unknown=1;RCHECK(!pthread_mutex_unlock(&p->lock));}return 0;
}
static int setup_save(void *unused,const struct bw_share_config *config,const struct bw_setup_generation *expected,const char *nonce,int (*guard)(void *),void *owner,struct bw_setup_saved *saved){
    (void)unused;RCHECK(guard&&!guard(owner)&&nonce_valid(nonce)&&bw_native_setup_generation_equal(expected,&release_generation));
    setup_saves++;release_config=*config;release_generation.sha256[0]++;*saved=(struct bw_setup_saved){.published=1,.verified=1};RCHECK(!guard(owner));return 0;
}
static void setup_fixture(struct bw_native_backend *p){
    if(!p->jobs){p->jobs=bw_native_jobs_create();RCHECK(p->jobs);}
    p->opaque=p;p->settings_enabled=p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->settings_generation=release_generation;
    p->ops.read_settings=release_settings;p->ops.save_settings=setup_save;p->ops.discover=setup_discover;p->ops.idle=release_idle;p->ops.nonce=setup_nonce;
    strcpy(p->server,release_config.server);strcpy(p->share,release_config.share);strcpy(p->share2,release_config.share2);
}
static void setup_editable(struct bw_native_backend *p,unsigned expected){
    struct bw_native_response response={0};RCHECK(!bw_native_backend_handle_authenticated(p,"GET","/api/state",NULL,0,NULL,&response)&&response.status==200);
    RCHECK(strstr(response.bytes,expected?"\"editable\":true":"\"editable\":false"));bw_native_response_clear(&response);
}
static int setup_post(struct bw_native_backend *p,const char *share){
    char body[512];int n=snprintf(body,sizeof body,"{\"server\":\"192.0.2.1\",\"share\":\"%s\",\"revision\":%u,\"keepCredentials\":true}",share,p->settings_revision);RCHECK(n>0&&(size_t)n<sizeof body);
    struct bw_native_response response={0};RCHECK(!bw_native_backend_handle_authenticated(p,"POST","/api/setup",body,(size_t)n,p->setup_token,&response));int status=response.status;bw_native_response_clear(&response);return status;
}
static int setup_late_guard(void *opaque,uint32_t operation){struct bw_native_backend *p=opaque;RCHECK(operation==p->setup_operation&&!pthread_mutex_lock(&p->lock));p->control_release_unknown=1;RCHECK(!pthread_mutex_unlock(&p->lock));return 0;}
static void setup_late_checks(void){
    /* A service callback returning success must not hide uncertainty it just
     * discovered. Use a separate owner; never clear an existing sticky flag. */
    struct bw_native_backend *p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL));
    p->active=1;p->worker_kind=WORK_SETUP;p->setup_operation=1;p->settings_idle=setup_late_guard;p->settings_idle_owner=p;
    RCHECK(settings_idle(p)&&p->control_release_unknown);RCHECK(!pthread_mutex_destroy(&p->lock));free(p);
    p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL)&&!pthread_mutex_init(&p->dispatch_lock,NULL));setup_fixture(p);
    p->phase=BACKEND_READY;p->settings_revision=2;strcpy(p->setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");setup_checkpoint=p;
    struct bw_share_config before=release_config;unsigned saves=setup_saves,discovers=setup_discovers;
    RCHECK(setup_post(p,"FixtureShare-Checkpoint")==202&&p->joinable);RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;
    RCHECK(setup_discovers==discovers+1&&setup_saves==saves&&config_equal(&before,&release_config)&&p->control_release_unknown&&!p->active);
    setup_editable(p,0);RCHECK(setup_post(p,"FixtureShare-Checkpoint")==409&&setup_saves==saves);setup_checkpoint=NULL;
    RCHECK(!bw_native_jobs_destroy(p->jobs));RCHECK(!pthread_mutex_destroy(&p->dispatch_lock)&&!pthread_mutex_destroy(&p->lock));free(p);
}
static void setup_after_removal(struct bw_native_backend *p,unsigned unknown){
    if(setup_checks_done)return;setup_checks_done=1;setup_fixture(p);setup_editable(p,!unknown);
    unsigned operation=p->setup_operation,saves=setup_saves,discovers=setup_discovers;
    int status=setup_post(p,"FixtureShare-Checked");
    if(unknown){RCHECK(status==409&&!p->active&&p->setup_operation==operation&&setup_saves==saves&&setup_discovers==discovers&&(p->control_release_unknown||p->removing.unknown));setup_editable(p,0);}
    else{RCHECK(status==202&&p->joinable);RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;
        RCHECK(setup_discovers==discovers+1&&setup_saves==saves+1&&!p->active&&!p->control_release_unknown&&!strcmp(release_config.share,"FixtureShare-Checked"));setup_editable(p,1);setup_late_checks();}
    RCHECK(!bw_native_jobs_destroy(p->jobs));p->jobs=NULL;
}
static int release_normal(void *unused,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *opaque,void (*observe)(void *,const struct bw_game_uninstall_progress *),void *observer,struct bw_game_uninstall_result *out){
    (void)unused;(void)observer;RCHECK(!memcmp(&selected->identity,&release_request->identity,sizeof selected->identity));release_calls++;
    struct bw_game_uninstall_request q=*release_request;q.ops=(struct bw_game_uninstall_ops){.opaque=opaque,.current=guard,.observe=observe};
    release_status=bw_native_game_uninstall_normal(&q,0,out);return release_status;
}
static void diagnostic_reason_checks(struct bw_native_backend *p){
    const char *original=p->removing.result.reason;char long_text[1024],split_utf8[196];
    memset(long_text,'A',sizeof long_text-1);long_text[sizeof long_text-1]=0;
    memset(split_utf8,'B',191);memcpy(split_utf8+191,"\xe2\x82\xac",4);
    const char *values[]={"quoted\"slash\\line\n",long_text,split_utf8};
    for(unsigned i=0;i<3;i++){
        p->removing.result.reason=values[i];struct bw_json json={.maximum=2048};removal_state_json(&json,p);
        RCHECK(!json.bad&&json.used<1200&&strstr(json.bytes,"\"diagnosticReason\":")&&p->removing.result.reason==values[i]);
        if(!i)RCHECK(strstr(json.bytes,"quoted\\\"slash\\\\line\\u000a")&&!strchr(json.bytes,'\n'));
        if(i==2)RCHECK(bw_json_utf8(json.bytes)&&strstr(json.bytes,"\xe2")==NULL);
        free(json.bytes);
    }p->removing.result.reason=original;
}
int bw_test_uninstall_reserved_worker(const struct bw_game_uninstall_request *q,struct bw_game_uninstall_result *out,unsigned expect_release){
    if(!release_initialized){
        release_initialized=1;RCHECK(!pthread_mutex_init(&release_owner.lock,NULL)&&!pthread_mutex_init(&release_owner.dispatch_lock,NULL));
        strcpy(release_config.server,"192.0.2.1");strcpy(release_config.share,"FixtureShare");release_generation.present=1;release_generation.file.st_mode=S_IFREG|0600;release_generation.file.st_nlink=1;memset(release_generation.sha256,0x31,32);
        release_owner.ops.read_settings=release_settings;release_owner.ops.idle=release_idle;release_owner.ops.uninstall=release_normal;
        release_owner.phase=BACKEND_READY;release_owner.settings_status=BW_SETUP_VALID;release_owner.settings_known=1;release_owner.settings_generation=release_generation;release_owner.settings_revision=2;release_owner.generation=5;
        strcpy(release_owner.setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    unsigned before=release_calls;
    if(release_owner.control_release_unknown||release_owner.removing.unknown){*out=release_owner.removing.result;RCHECK((expect_release||release_pending)&&out->release_unknown==expect_release&&out->phase==BW_GAME_REMOVE_UNKNOWN&&before==release_calls);return -1;}
    release_request=q;struct bw_native_backend *p=&release_owner;memset(&p->removing,0,sizeof p->removing);
    p->removing.selected.identity=q->identity;p->removing.selected.prepared=q->prepared;p->removing.selected.connection=release_generation;
    memcpy(p->removing.selected.operation,q->operation,33);memcpy(p->removing.selected.confirmed_title,q->confirmed_title,10);memcpy(p->external_nonce,q->operation,33);
    p->removing.state=REMOVAL_RUNNING;p->removing.generation=p->generation;p->removing.epoch=p->play_epoch;p->removing.revision=p->settings_revision;p->worker_kind=WORK_UNINSTALL;p->active=1;
    (void)removal_worker(p);*out=p->removing.result;
    if(release_calls!=before+1||p->active||!release_status)fprintf(stderr,"release chain before=%u after=%u active=%d status=%d phase=%u release=%u reason=%s\n",before,release_calls,p->active,release_status,(unsigned)out->phase,out->release_unknown,out->reason?out->reason:"none");
    RCHECK(release_calls==before+1&&!p->active&&release_status);
    RCHECK(out->mutation_started==release_pending&&!out->files_removed&&!out->source_absent&&!out->library_removed);
    RCHECK(out->release_unknown==expect_release&&p->control_release_unknown==(int)expect_release);
    RCHECK(p->removing.state==(expect_release||release_pending?REMOVAL_UNKNOWN:REMOVAL_ERROR));
    struct bw_json json={.maximum=2048};removal_state_json(&json,p);RCHECK(!json.bad&&strstr(json.bytes,expect_release?"\"releaseUnknown\":true":"\"releaseUnknown\":false"));free(json.bytes);
    diagnostic_reason_checks(p);
    /* Even a new queue reservation cannot restore control authority after an
     * uncertain release. An ordinary refusal retains that fresh authority. */
    RCHECK(!bw_native_backend_external_release(p,q->operation));
    const char *next="cccccccccccccccccccccccccccccccc";
    RCHECK(!bw_native_backend_external_reserve(p,next,p->setup_token));
    int current=bw_native_backend_external_current(p,next);RCHECK((current!=0)==(expect_release!=0));
    RCHECK(!bw_native_backend_external_release(p,next));
    setup_after_removal(p,expect_release||release_pending);
    return release_status;
}
