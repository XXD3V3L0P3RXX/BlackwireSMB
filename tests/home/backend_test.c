/* Actual extracted queue/current/selection functions. The Sony transport,
 * worker creation and local receipt/registration boundaries are modeled;
 * no source, filesystem, network, Sony launch or real thread is used here. */
#define _DEFAULT_SOURCE 1
#include "backend-under-test.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned cases,checks,queries,queued,nonce_count,settings_calls,metadata_calls,registration_calls;
static int spawn_fail,registered_mask,app_unknown,dead_history,full_idle_refuse,legacy_idle_refuse;static unsigned full_idles,legacy_idles;
static struct bw_native_backend owner;
static struct bw_stop_identity birth={.pid=500,.start_sec=1700000000,.start_usec=123456,.role="eboot.bin"};
static struct bw_catalog_item catalog[2];static struct bw_native_prepared prepared[2];
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"case %u line %u: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
static int play(void *a,const struct bw_native_play_selection *b,int(*c)(void*),void*d,void(*e)(void*,const struct bw_reader_event*),void*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -1;}
static int process(void *v,int32_t pid,struct bw_stop_identity *out){(void)v;queries++;if(pid!=birth.pid)return dead_history?0:-1;*out=birth;return 1;}
static int64_t wall(void *v){(void)v;return birth.start_sec*1000000+birth.start_usec+1000000;}
static int running(void*v,uint32_t*out){(void)v;*out=23;return 0;}
static int title(void*v,uint32_t app,char out[16]){(void)v;C(app==23);strcpy(out,"PPSA04734");return 0;}
static int close_game(void*v,uint32_t app){(void)v;(void)app;C(0);return -1;}
void bw_native_session_ps5_ops(struct bw_session_ops *out){*out=(struct bw_session_ops){NULL,running,title,process,wall,close_game};}
int bw_home_identity_app(int32_t pid,uint32_t *app,char out[10]){C(pid==birth.pid);if(app_unknown)return -1;*app=23;strcpy(out,"PPSA04734");return 0;}
int bw_home_identity_current(const struct bw_native_session *game){C(game->bound&&game->process.pid==birth.pid);return 0;}
int bw_home_identity_uid(pid_t pid,uid_t *out){C(pid==birth.pid);*out=1234;return 0;}
int bw_startup_idle_ps5(void *v){struct bw_startup_idle_request*r=v;C(r&&r->current&&!r->current(r->opaque));full_idles++;return full_idle_refuse?-1:0;}
static int fake_idle(void *v){(void)v;legacy_idles++;return legacy_idle_refuse?-1:0;}
static int fake_nonce(void *v,char out[33]){(void)v;nonce_count++;memset(out,"cdef123456789ab"[nonce_count%14],32);out[32]=0;return 0;}
static int fake_settings(void *v,struct bw_share_config *c,struct bw_setup_generation *g){(void)v;settings_calls++;memset(c,0,sizeof *c);strcpy(c->server,"192.0.2.20");strcpy(c->share,"OwnedBackups");strcpy(c->user,"project-fixture");strcpy(c->password,"fixture-value");*g=owner.settings_generation;return BW_SETUP_VALID;}
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
int bw_native_launch_selection(const char *base,const struct bw_provider_binding *id,const struct bw_native_prepared *expected,const char *run,struct bw_reader_selection *out){
    C(!strcmp(base,backend_data_root)&&expected->state==BW_PREPARED_VALID);metadata_calls++;memset(out,0,sizeof *out);strcpy(out->source,id->binding.source);strcpy(out->game,id->binding.game);strcpy(out->title,id->binding.title);strcpy(out->run,run);return 0;
}
int bw_native_registration_verify(const char *base,const char *apps,const char *db,const struct bw_reader_selection *selected){
    C(!strcmp(base,backend_data_root)&&!strcmp(apps,"/user/app")&&!strcmp(db,"/system_data/priv/mms/app.db"));registration_calls++;
    return (!strcmp(selected->source,"First")?(registered_mask&1):(registered_mask&2))?0:-1;
}
int bw_source_catalog_available(const struct bw_catalog_item *item){return item->state==BW_CATALOG_FOLDER||item->state==BW_CATALOG_CONTAINER_READY;}
enum bw_provider_kind bw_source_catalog_kind(const struct bw_catalog_item *item){return item->state==BW_CATALOG_CONTAINER_READY?BW_PROVIDER_CONTAINER:BW_PROVIDER_FOLDER;}
static void *home_worker(void *v){(void)v;C(0);return NULL;}
static int worker_start(pthread_t *thread,void *(*entry)(void *),void *opaque){C(entry==home_worker&&opaque==&owner);(void)thread;queued++;return spawn_fail?-1:0;}
static int text(struct bw_native_response *out,int status,const char *raw){out->status=status;out->type="application/json";out->size=strlen(raw);out->bytes=malloc(out->size+1);C(out->bytes);memcpy(out->bytes,raw,out->size+1);return 0;}
static void reset(void){
    cases++;memset(&owner,0,sizeof owner);C(!pthread_mutex_init(&owner.lock,NULL));C(!pthread_mutex_init(&owner.dispatch_lock,NULL));
    owner.ops.play=play;owner.ops.nonce=fake_nonce;owner.ops.read_settings=fake_settings;owner.ops.idle=fake_idle;owner.phase=BACKEND_READY;
    owner.settings_enabled=1;owner.settings_status=BW_SETUP_VALID;owner.settings_revision=3;owner.generation=5;owner.play_epoch=7;
    owner.settings_generation.present=1;memset(owner.settings_generation.sha256,0x12,32);
    queries=queued=nonce_count=settings_calls=metadata_calls=registration_calls=0;spawn_fail=app_unknown=dead_history=full_idle_refuse=legacy_idle_refuse=0;full_idles=legacy_idles=0;registered_mask=2;
}
static void done(void){free(owner.home.retained);owner.home.retained=NULL;owner.joinable=0;C(!pthread_mutex_destroy(&owner.lock));C(!pthread_mutex_destroy(&owner.dispatch_lock));}
static struct bw_home_claim claim(void){struct bw_home_claim c={.pid=birth.pid,.deadline_us=(uint64_t)((now()+10)*1000000)};memset(c.nonce,'a',32);memset(c.map_sha256,0x12,32);return c;}
static int request(struct bw_home_claim *c,int peer,int header,int origin,char run[33]){
    char body[512];size_t used=0;C(!bw_home_claim_format(c,body,sizeof body,&used));struct bw_native_response out={0};C(!bw_native_backend_home_claim(&owner,body,used,peer,header,origin,&out));int status=out.status;
    if(status==202)C(!bw_home_ack_parse(out.bytes,out.size,c->nonce,run));free(out.bytes);return status;
}
static void queue_tests(void){
    char run[33];struct bw_home_claim c;
    reset();c=claim();C(request(&c,0,1,0,run)==403&&!queries&&!queued);C(request(&c,1,0,0,run)==403&&!queries&&!queued);C(request(&c,1,1,1,run)==403&&!queries&&!queued);done();
    reset();c=claim();c.deadline_us=(uint64_t)((now()-1)*1000000);C(request(&c,1,1,0,run)==400&&!queries&&!queued);c.deadline_us=(uint64_t)((now()+21)*1000000);C(request(&c,1,1,0,run)==400&&!queries&&!queued);done();
    reset();c=claim();C(request(&c,1,1,0,run)==202&&queued==1&&queries==2&&!settings_calls&&!metadata_calls&&!registration_calls);C(owner.active&&owner.home.active&&owner.playing.home_adopted&&owner.home_consumed_count==1);C(!strcmp(run,owner.playing.selection.run));
    C(request(&c,1,1,0,run)==409&&queued==1);owner.active=owner.home.active=owner.joinable=0;memset(c.nonce,'b',32);C(request(&c,1,1,0,run)==409&&queued==1);done();
    reset();c=claim();spawn_fail=1;C(request(&c,1,1,0,run)==503&&queued==1&&!owner.active&&!owner.home.active&&owner.home_consumed_count==1);spawn_fail=0;C(request(&c,1,1,0,run)==409&&queued==1);done();
    reset();c=claim();owner.control_release_unknown=1;C(request(&c,1,1,0,run)==409&&!queued);owner.control_release_unknown=0;strcpy(owner.external_nonce,"dddddddddddddddddddddddddddddddd");C(request(&c,1,1,0,run)==409&&!queued);done();
    reset();c=claim();owner.home_consumed_count=256;C(request(&c,1,1,0,run)==409&&!queued);done();
    reset();c=claim();owner.home_consumed_count=256;dead_history=1;for(unsigned i=0;i<256;i++){owner.home_consumed[i]=birth;owner.home_consumed[i].pid=1000+(int32_t)i;}C(request(&c,1,1,0,run)==202&&queued==1&&owner.home_consumed_count==1);done();
}
static struct home_worker selection_fixture(void){
    reset();owner.active=1;owner.worker_kind=WORK_PLAY;owner.home.active=1;owner.playing.home_adopted=1;owner.home.claim=claim();owner.home.birth=birth;
    owner.home.generation=owner.generation;owner.home.epoch=owner.play_epoch;owner.home.revision=owner.settings_revision;
    strcpy(owner.playing.selection.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");owner.playing.selection.connection=owner.settings_generation;
    memset(catalog,0,sizeof catalog);memset(prepared,0,sizeof prepared);strcpy(catalog[0].source,"First");strcpy(catalog[1].source,"Second");
    for(unsigned i=0;i<2;i++){strcpy(catalog[i].title_id,"PPSA04734");catalog[i].state=BW_CATALOG_FOLDER;prepared[i].state=BW_PREPARED_VALID;}
    owner.library.catalog.items=catalog;owner.library.catalog.count=2;owner.library.prepared=prepared;
    struct home_worker h={.backend=&owner,.selected=owner.playing.selection,.claim=owner.home.claim,.birth=birth,.generation=owner.generation,.epoch=owner.play_epoch,.revision=owner.settings_revision,.deadline=now()+20};bw_native_session_ps5_ops(&h.process);return h;
}
static void cleanup_tests(void){
    struct home_worker h=selection_fixture();owner.home.retained=&h;h.deadline=0;
    C(!home_idle_after_exit(&h)&&full_idles==1&&legacy_idles==1);full_idle_refuse=1;C(home_idle_after_exit(&h)&&full_idles==2);full_idle_refuse=0;legacy_idle_refuse=1;C(home_idle_after_exit(&h)&&full_idles==2);owner.home.retained=NULL;done();
    reset();owner.home_consumed_count=3;for(unsigned i=0;i<3;i++){owner.home_consumed[i]=birth;owner.home_consumed[i].start_usec+=i;}
    struct bw_stop_identity terminal=owner.home_consumed[1];home_consumed_retire_locked(&owner,&terminal);C(owner.home_consumed_count==2);for(unsigned i=0;i<2;i++)C(bw_stop_identity_match(&owner.home_consumed[i],&terminal)!=1);home_consumed_retire_locked(&owner,&terminal);C(owner.home_consumed_count==2);done();
}
static void selection_tests(void){
    struct bw_share_config config;
    struct home_worker h=selection_fixture();C(!home_select(&h,&config)&&h.resolved&&owner.playing.item==1&&!strcmp(h.selected.identity.binding.source,"Second"));C(metadata_calls==2&&registration_calls==2&&settings_calls==1);bw_share_clear(&config);done();
    h=selection_fixture();registered_mask=1;C(!home_select(&h,&config)&&owner.playing.item==0&&!strcmp(h.selected.identity.binding.source,"First"));bw_share_clear(&config);done();
    h=selection_fixture();registered_mask=3;C(home_select(&h,&config)&&!h.resolved);bw_share_clear(&config);done();
    h=selection_fixture();registered_mask=0;C(home_select(&h,&config)&&!h.resolved);bw_share_clear(&config);done();
    h=selection_fixture();owner.settings_revision++;C(home_select(&h,&config)&&!metadata_calls&&!registration_calls);bw_share_clear(&config);done();
    h=selection_fixture();catalog[1].state=BW_CATALOG_CONTAINER_READY;
    C(!home_select(&h,&config)&&h.resolved&&owner.playing.item==1&&h.selected.identity.kind==BW_PROVIDER_CONTAINER);bw_share_clear(&config);done();
}
int main(void){queue_tests();selection_tests();cleanup_tests();printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualQueueLeaseAndSelection\":true,\"SonyWorkerReceiptAndRegistrationBoundariesModeled\":true}\n",cases,checks);return 0;}


