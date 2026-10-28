/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual resident constructor/ticker body plus actual backend refresh/worker.
 * Factories and observer are modeled; ticker scheduling is controlled. */
#define main inherited_removal_main
#define reserve fixture_reserve
#include "test_game_removal_backend.c"
#undef reserve
#undef main
#include <setjmp.h>
#include <errno.h>
#include "dashboard_service.h"
#include "local_launch.h"
#include "local_removal_runtime.h"
#include "registration_observer.h"
static int captured_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
static int captured_sleep(const struct timespec *,struct timespec *);
static int recorded_refresh(struct bw_native_backend *);
#define pthread_create captured_create
#define nanosleep captured_sleep
#define bw_native_backend_refresh recorded_refresh
#define BW_WEB_ONLY 1
#include "resident.c"
#undef pthread_create
#undef nanosleep
#undef bw_native_backend_refresh

struct bd_local_launcher {int live;};
struct bd_local_removal_runtime {int live;};
struct bd_registration_observer {struct bw_native_backend *backend;};
struct bd_dashboard_service {struct bd_service_config config;unsigned settings,preparation;};
static struct removal_fixture fixture;
static struct bw_catalog_item known_item;
static atomic_uint discovers;
static unsigned refreshes,create_calls,service_ticks,observer_ticks,reservation_success,reservation_refused,live_facades,sleeps,events[32],event_count;
static int create_fails,refresh_rc;
static struct bd_dashboard_service *retained_service;
static void *(*captured_entry)(void *);static void *captured_arg;
static jmp_buf ticker_exit;
static void event(unsigned x){CHECK(event_count<32);events[event_count++]=x;}
static int guard_settings(void *v,uint32_t op){(void)op;return v?0:-1;}
static int guard_preparation(void *v,const char a[33],const char b[33],enum bw_prepare_guard_point p){(void)a;(void)b;(void)p;return v?0:-1;}
static int startup_discover(void *v,struct bw_share_config *config,struct bw_native_library *out){struct removal_fixture *f=v;CHECK(config&&out&&!strcmp(config->share,f->config.share));atomic_fetch_add(&discovers,1);atomic_store(&f->entered,1);while(atomic_load(&f->block))pause_worker();memset(out,0,sizeof *out);out->catalog.items=calloc(1,sizeof *out->catalog.items);out->prepared=calloc(1,sizeof *out->prepared);CHECK(out->catalog.items&&out->prepared);out->catalog.count=out->catalog.capacity=1;out->catalog.items[0]=known_item;return 0;}
static void reset(void){fixture_init(&fixture);known_item=fixture.p->library.catalog.items[0];bw_native_library_destroy(&fixture.p->library);fixture.p->phase=BACKEND_INITIAL;fixture.p->generation=0;fixture.p->settings_revision=0;fixture.p->settings_known=0;fixture.p->settings_status=BW_SETUP_UNSAFE;fixture.p->ops.discover=startup_discover;fixture.p->ops.inspect=NULL;atomic_store(&discovers,0);refreshes=create_calls=service_ticks=observer_ticks=reservation_success=reservation_refused=live_facades=sleeps=event_count=0;create_fails=0;refresh_rc=-99;captured_entry=NULL;captured_arg=NULL;retained_service=NULL;}
static int captured_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *v){CHECK(t&&a&&fn&&v);create_calls++;event(4);captured_arg=v;if(create_fails==2)CHECK(!bw_native_backend_external_reserve(fixture.p,"dddddddddddddddddddddddddddddddd",fixture.p->setup_token));if(create_fails)return EAGAIN;*t=(pthread_t)0;captured_entry=fn;return 0;}
static int captured_sleep(const struct timespec *t,struct timespec *unused){(void)unused;CHECK(t&&t->tv_nsec==250000000);if(++sleeps==2)longjmp(ticker_exit,1);return 0;}
static int recorded_refresh(struct bw_native_backend *p){refreshes++;event(5);CHECK(p->settings_idle&&p->preparation_idle&&p->settings_idle_owner==p->preparation_idle_owner);refresh_rc=bw_native_backend_refresh(p);return refresh_rc;}
struct bd_local_launcher *bd_local_launcher_create(const struct bd_local_launch_config *c){CHECK(c);live_facades++;return calloc(1,sizeof(struct bd_local_launcher));}
int bd_local_launcher_destroy(struct bd_local_launcher *p){CHECK(p&&live_facades);live_facades--;free(p);return 0;}
struct bd_local_removal_runtime *bd_local_removal_runtime_create(const struct bd_local_removal_runtime_config *c){CHECK(c&&c->backend);live_facades++;return calloc(1,sizeof(struct bd_local_removal_runtime));}
int bd_local_removal_runtime_destroy(struct bd_local_removal_runtime *p){CHECK(p&&live_facades);live_facades--;free(p);return 0;}
struct bd_registration_observer *bd_registration_observer_create(struct bw_native_backend *b,const struct bd_registration_observer_ops *o){CHECK(b&&!o);live_facades++;struct bd_registration_observer *p=calloc(1,sizeof *p);CHECK(p);p->backend=b;return p;}
int bd_registration_observer_destroy(struct bd_registration_observer *p){CHECK(p&&live_facades);live_facades--;free(p);return 0;}
struct bd_dashboard_service *bd_service_create(const struct bd_service_config *c){CHECK(c&&c->backend&&c->native_frontend_disabled==1);live_facades++;event(1);struct bd_dashboard_service *p=calloc(1,sizeof *p);CHECK(p);p->config=*c;return p;}
int bd_service_attach_settings_guard(struct bd_dashboard_service *p){event(2);int rc=bw_native_backend_set_settings_idle(p->config.backend,guard_settings,p);if(!rc)p->settings=1;return rc;}
int bd_service_attach_preparation_guard(struct bd_dashboard_service *p){event(3);int rc=bw_native_backend_set_preparation_idle(p->config.backend,guard_preparation,p);if(!rc)p->preparation=1;return rc;}
/* Match the real service contract: a refused guard clear retains the object. */
int bd_service_destroy(struct bd_dashboard_service *p){CHECK(p&&live_facades);if(p->preparation){if(bw_native_backend_clear_preparation_idle(p->config.backend,guard_preparation,p)){retained_service=p;return 1;}p->preparation=0;}if(p->settings){if(bw_native_backend_clear_settings_idle(p->config.backend,guard_settings,p)){retained_service=p;return 1;}p->settings=0;}if(retained_service==p)retained_service=NULL;live_facades--;free(p);return 0;}
int bd_service_tick(struct bd_dashboard_service *p){CHECK(p&&p->settings&&p->preparation);service_ticks++;event(6);return 0;}
int bd_registration_observer_tick(struct bd_registration_observer *p){CHECK(p);observer_ticks++;event(7);int rc=bw_native_backend_external_reserve(p->backend,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",fixture.p->setup_token);if(!rc){reservation_success++;CHECK(!bw_native_backend_external_release(p->backend,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));}else{CHECK(rc==1);reservation_refused++;}return 0;}
int bd_service_handle(struct bd_dashboard_service *a,const char*b,const char*c,const void*d,size_t e,const char*f,struct bw_native_response*g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;abort();}
int bd_local_launch(void*a,const struct bd_item*b,const char c[33]){(void)a;(void)b;(void)c;abort();}
int bd_local_state(void*a,const char b[33]){(void)a;(void)b;abort();}
int bd_local_launch_capable(struct bd_local_launcher*a,const struct bd_item*b){(void)a;(void)b;abort();}
int bd_local_removal_runtime_capable(void*a,const struct bd_item*b){(void)a;(void)b;abort();}
int bd_local_removal_runtime_start(void*a,const struct bd_item*b,const char c[33],const char d[10]){(void)a;(void)b;(void)c;(void)d;abort();}
int bd_local_removal_runtime_state(void*a,const char b[33],struct bw_game_uninstall_result*c){(void)a;(void)b;(void)c;abort();}
static void run_ticker(void){CHECK(captured_entry&&captured_arg);if(!setjmp(ticker_exit))captured_entry(captured_arg);CHECK(sleeps==2&&service_ticks==2&&observer_ticks==2);}
static void destroy_resident(struct bd_resident *r){CHECK(r);atomic_store(&fixture.block,0);wait_worker(&fixture);CHECK(!bd_service_destroy(r->service));CHECK(!bd_registration_observer_destroy(r->observer));CHECK(!bd_local_removal_runtime_destroy(r->removal));CHECK(!bd_local_launcher_destroy(r->local));free(r);CHECK(!live_facades);}
static void assert_initial(void){struct bw_native_response o=state_response(&fixture);CHECK(strstr(o.bytes,"\"configured\":true")&&strstr(o.bytes,"\"phase\":\"initial\"")&&!fixture.p->library.catalog.count);bw_native_response_clear(&o);}
static void assert_ready(void){wait_worker(&fixture);struct bw_native_response o=state_response(&fixture);CHECK(strstr(o.bytes,"\"phase\":\"ready\"")&&fixture.p->library.catalog.count==1&&fixture.p->generation==1);bw_native_response_clear(&o);}
static void retained_failure_case(void){
    reset();create_fails=2;struct bd_resident *r=bd_resident_create_with_startup_refresh(fixture.p,NULL);
    CHECK(!r&&create_calls==1&&!refreshes&&retained_service&&captured_arg);
    CHECK(fixture.p->settings_idle_owner==retained_service&&fixture.p->preparation_idle_owner==retained_service);
    /* Execute the real retained resident callback after factory failure. */
    CHECK(!retained_service->config.ops.release(retained_service->config.ops.opaque,"dddddddddddddddddddddddddddddddd"));
    CHECK(live_facades==4&&!fixture.p->external_nonce[0]);
    destroy_resident(captured_arg);CHECK(!retained_service);fixture_close(&fixture);
}
int main(void){
    reset();struct bd_resident *r=bd_resident_create(fixture.p);CHECK(r);run_ticker();CHECK(!refreshes&&!atomic_load(&discovers)&&reservation_success==2);assert_initial();destroy_resident(r);fixture_close(&fixture);
    reset();r=bd_resident_create_with_feedback(fixture.p,NULL);CHECK(r);run_ticker();CHECK(!refreshes&&!atomic_load(&discovers));assert_initial();destroy_resident(r);fixture_close(&fixture);
    reset();atomic_store(&fixture.block,1);r=bd_resident_create_with_startup_refresh(fixture.p,NULL);CHECK(r&&create_calls==1&&!refreshes);run_ticker();CHECK(refreshes==1&&refresh_rc==0&&reservation_refused==2&&!reservation_success);CHECK(events[0]==1&&events[1]==2&&events[2]==3&&events[3]==4&&events[4]==5&&events[5]==6&&events[6]==7);CHECK(bw_native_backend_set_settings_idle(fixture.p,guard_settings,r->service)==1);CHECK(bw_native_backend_set_preparation_idle(fixture.p,guard_preparation,r->service)==1);atomic_store(&fixture.block,0);assert_ready();destroy_resident(r);fixture_close(&fixture);
    reset();r=bd_resident_create_with_startup_refresh(fixture.p,NULL);CHECK(r);fixture_reserve(&fixture,"cccccccccccccccccccccccccccccccc");run_ticker();CHECK(refreshes==1&&refresh_rc==1&&!atomic_load(&discovers)&&reservation_refused==2&&!strcmp(fixture.p->external_nonce,"cccccccccccccccccccccccccccccccc"));assert_initial();CHECK(!bw_native_backend_external_release(fixture.p,"cccccccccccccccccccccccccccccccc"));destroy_resident(r);fixture_close(&fixture);
    reset();atomic_store(&fixture.block,1);CHECK(!bw_native_backend_refresh(fixture.p));r=bd_resident_create_with_startup_refresh(fixture.p,NULL);CHECK(!r&&!create_calls&&!refreshes&&!live_facades&&!fixture.p->settings_idle&&!fixture.p->preparation_idle);atomic_store(&fixture.block,0);assert_ready();fixture_close(&fixture);
    reset();create_fails=1;r=bd_resident_create_with_startup_refresh(fixture.p,NULL);CHECK(!r&&create_calls==1&&!refreshes&&!atomic_load(&discovers)&&!live_facades&&!fixture.p->settings_idle&&!fixture.p->preparation_idle);assert_initial();fixture_close(&fixture);
    reset();int foreign=0;CHECK(!bw_native_backend_set_settings_idle(fixture.p,guard_settings,&foreign));r=bd_resident_create_with_startup_refresh(fixture.p,NULL);CHECK(!r&&!create_calls&&!refreshes&&!live_facades&&fixture.p->settings_idle_owner==&foreign&&!fixture.p->preparation_idle);CHECK(!bw_native_backend_clear_settings_idle(fixture.p,guard_settings,&foreign));fixture_close(&fixture);
    retained_failure_case();
    printf("{\"passed\":true,\"cases\":8,\"checks\":%u,\"actualBackendRefreshAndScanWorker\":true,\"actualCandidateConstructorAndTickerBody\":true,\"guardFacadeUsesRealBackendSetters\":true,\"factoriesAndObserverModeled\":true,\"tickerSchedulingControlled\":true,\"consoleActions\":0}\n",atomic_load(&checked));return 0;
}
