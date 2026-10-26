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
static unsigned release_initialized,release_calls,release_force_idle,release_idle_calls,release_checks;
static int release_status;
#define RCHECK(x) do{release_checks++;if(!(x)){fprintf(stderr,"release worker line %d: %s\n",__LINE__,#x);abort();}}while(0)
unsigned bw_test_uninstall_release_checks(void){return release_checks;}
static int release_settings(void *unused,struct bw_share_config *config,struct bw_setup_generation *generation){(void)unused;*config=release_config;*generation=release_generation;return BW_SETUP_VALID;}
void bw_test_uninstall_release_final_close(void){release_force_idle=1;}
static int release_idle(void *unused){(void)unused;return release_force_idle&&++release_idle_calls>1?-1:0;}
static int release_normal(void *unused,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *opaque,void (*observe)(void *,const struct bw_game_uninstall_progress *),void *observer,struct bw_game_uninstall_result *out){
    (void)unused;(void)observer;RCHECK(!memcmp(&selected->identity,&release_request->identity,sizeof selected->identity));release_calls++;
    struct bw_game_uninstall_request q=*release_request;q.ops=(struct bw_game_uninstall_ops){.opaque=opaque,.current=guard,.observe=observe};
    release_status=bw_native_game_uninstall_normal(&q,0,out);return release_status;
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
    if(release_owner.control_release_unknown){*out=release_owner.removing.result;RCHECK(expect_release&&out->release_unknown&&out->phase==BW_GAME_REMOVE_UNKNOWN&&before==release_calls);return -1;}
    release_request=q;struct bw_native_backend *p=&release_owner;memset(&p->removing,0,sizeof p->removing);
    p->removing.selected.identity=q->identity;p->removing.selected.prepared=q->prepared;p->removing.selected.connection=release_generation;
    memcpy(p->removing.selected.operation,q->operation,33);memcpy(p->removing.selected.confirmed_title,q->confirmed_title,10);memcpy(p->external_nonce,q->operation,33);
    p->removing.state=REMOVAL_RUNNING;p->removing.generation=p->generation;p->removing.epoch=p->play_epoch;p->removing.revision=p->settings_revision;p->worker_kind=WORK_UNINSTALL;p->active=1;
    (void)removal_worker(p);*out=p->removing.result;
    if(release_calls!=before+1||p->active||!release_status)fprintf(stderr,"release chain before=%u after=%u active=%d status=%d phase=%u release=%u reason=%s\n",before,release_calls,p->active,release_status,(unsigned)out->phase,out->release_unknown,out->reason?out->reason:"none");
    RCHECK(release_calls==before+1&&!p->active&&release_status);
    RCHECK(!out->mutation_started&&!out->files_removed&&!out->source_absent&&!out->library_removed);
    RCHECK(out->release_unknown==expect_release&&p->control_release_unknown==(int)expect_release);
    RCHECK(p->removing.state==(expect_release?REMOVAL_UNKNOWN:REMOVAL_ERROR));
    struct bw_json json={.maximum=2048};removal_state_json(&json,p);RCHECK(!json.bad&&strstr(json.bytes,expect_release?"\"releaseUnknown\":true":"\"releaseUnknown\":false"));free(json.bytes);
    /* Even a new queue reservation cannot restore control authority after an
     * uncertain release. An ordinary refusal retains that fresh authority. */
    RCHECK(!bw_native_backend_external_release(p,q->operation));
    const char *next="cccccccccccccccccccccccccccccccc";
    RCHECK(!bw_native_backend_external_reserve(p,next,p->setup_token));
    int current=bw_native_backend_external_current(p,next);RCHECK((current!=0)==(expect_release!=0));
    RCHECK(!bw_native_backend_external_release(p,next));
    return release_status;
}
