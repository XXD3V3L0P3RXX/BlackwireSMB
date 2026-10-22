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
static struct bw_native_backend owner;
static struct bw_reader_selection selection;
static struct bw_native_launch_admission *live;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"fast routing case%u line%d: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
int bw_native_setup_idle(void *p){(void)p;return 0;}
static int settings(void *p,struct bw_share_config *c,struct bw_setup_generation *g){CHECK(p==&owner);setting_calls++;memset(c,0,sizeof *c);*g=owner.playing.selection.connection;if(settings_changed)g->sha256[0]^=1;return BW_SETUP_VALID;}
int bw_native_launch_admission_open(struct bw_native_launch_admission **p,const char *root,const struct bw_provider_binding *id,const struct bw_native_prepared *expected,const char *run,int (*guard)(void *),void *opaque,const char **error){
    CHECK(p&&!live&&!*p&&root&&id==&owner.playing.selection.identity&&expected==&owner.playing.selection.prepared&&run&&!guard(opaque));opened++;
    if(fail_open){*error="Required startup receipt changed; choose Send to repair it.";return -1;}
    live=*p=(struct bw_native_launch_admission *)(uintptr_t)0x1234;return 0;
}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *out){CHECK(live&&p==live&&out);selected_calls++;*out=selection;return 0;}
int bw_native_launch_admission_current(struct bw_native_launch_admission *p,const char *root,const struct bw_provider_binding *id,const struct bw_reader_selection *s,struct bw_native_prepared *out){
    CHECK(live&&p==live&&root&&id&&s&&!strcmp(s->run,selection.run));if(out)*out=owner.playing.selection.prepared;return settings_changed?-1:0;
}
int bw_native_launch_admission_close(struct bw_native_launch_admission **p){CHECK(p);if(*p){CHECK(live==*p);closed++;live=NULL;*p=NULL;}return 0;}
int bw_native_launch_selection(const char *a,const struct bw_provider_binding *b,const struct bw_native_prepared *c,const char *d,struct bw_reader_selection *e){(void)a;(void)b;(void)c;(void)d;(void)e;strict_calls++;CHECK(0);return -1;}
int bw_native_owned_registration(const struct bw_native_register_request *r,struct bw_native_owned_registration_result *out){CHECK(r&&r->startup==live&&out);owned_calls++;return 0;}
int bw_native_register(const struct bw_native_register_request *r,struct bw_register_result *out){CHECK(r&&r->startup==live&&out&&!r->current(r->guard));register_calls++;out->message="Registered source mismatch";return fail_register?-1:0;}
int bw_native_registration_verify(const char *root,const char *apps,const char *db,const struct bw_reader_selection *s){CHECK(root&&apps&&db&&s);return foreign_registration?-1:0;}
int bw_native_reader_run(const struct bw_native_reader_request *r){
    CHECK(live&&r&&r->startup==live&&!r->current(r->opaque)&&!r->data_current(r->opaque));reader_calls++;
    if(foreign_registration){CHECK(r->before_launch(r->opaque,&r->selection)!=0);return -1;}
    CHECK(!r->before_launch(r->opaque,&r->selection));return fail_reader?-1:0;
}
void bw_native_sensitive_clear(void *p,size_t n){memset(p,0,n);}
static void observe(void *p,const struct bw_reader_event *e){CHECK(p==&owner&&e&&e->phase);if(!strcmp(e->phase,"error"))errors++;}
static void reset(void){
    CHECK(!live);memset(&owner,0,sizeof owner);CHECK(!pthread_mutex_init(&owner.lock,NULL));owner.active=1;owner.worker_kind=WORK_PLAY;owner.generation=4;owner.settings_revision=2;owner.play_epoch=9;
    owner.opaque=&owner;owner.ops.read_settings=settings;owner.playing.selection.connection.present=1;
    owner.playing.selection.identity.kind=BW_PROVIDER_FOLDER;owner.playing.selection.identity.policy=1;
    owner.playing.selection.prepared.state=BW_PREPARED_VALID;memset(&selection,0,sizeof selection);strcpy(selection.run,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    opened=closed=selected_calls=strict_calls=register_calls=reader_calls=owned_calls=errors=setting_calls=0;
    fail_open=fail_register=fail_reader=settings_changed=foreign_registration=0;
}
static int execute(void){int r=play(NULL,&owner.playing.selection,play_current,&owner,observe,&owner);CHECK(!live&&!pthread_mutex_destroy(&owner.lock));cases++;return r;}
int main(void){
    reset();CHECK(!execute()&&opened==1&&closed==1&&selected_calls==1&&!strict_calls&&register_calls==1&&reader_calls==1&&!errors);cases++;
    reset();fail_open=1;CHECK(execute()!=0&&opened==1&&!closed&&!register_calls&&!reader_calls&&errors==1);cases++;
    reset();fail_register=1;CHECK(execute()!=0&&opened==1&&closed==1&&register_calls==1&&!reader_calls&&errors==1);cases++;
    reset();fail_reader=1;CHECK(execute()!=0&&opened==1&&closed==1&&reader_calls==1);cases++;
    reset();settings_changed=1;CHECK(execute()!=0&&!opened&&!register_calls&&!reader_calls);cases++;
    reset();foreign_registration=1;CHECK(execute()!=0&&closed==1&&reader_calls==1);cases++;
    reset();owner.playing.selection.prepared.prepared_policy=2;CHECK(execute()!=0&&!opened&&!register_calls&&!reader_calls);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualDefaultPlayRouting\":true,\"admissionTransportModeled\":true}\n",cases,checks);return 0;
}
