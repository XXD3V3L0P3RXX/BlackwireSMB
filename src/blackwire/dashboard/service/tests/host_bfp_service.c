/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real service/model/catalog/FS/SQLite code with explicit modeled platform and
 * backend seams. No console, Sony launch, process query or share contact. */
#define _XOPEN_SOURCE 700
#include "dashboard_service.h"
#include "native_document.h"
#include "native_json.h"
#include "native_request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void fixture_package_fingerprint(const struct bd_frontend_package *,uint8_t[32]);

static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"check failed line%d: %s\n",__LINE__,#x);exit(90);}}while(0)
struct fixture {
    const char *smp,*apps,*database,*icon;
    uint64_t now;
    unsigned front_present,idle,bfp_busy,bfp_calls,bfp_mode,guard_calls,reserved,reserves,releases,dispatches,local_calls,nonce;
    int guard_result;
    int local_result,local_state,backend_result,release_fail,source_changed,front_unknown;
    char owner[33],job[33],action[8];
};
static const char token[]="11111111111111111111111111111111";
static const char job[]="22222222222222222222222222222222";
static unsigned char *load(const char *path,size_t *n) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>=0);rewind(f);
    unsigned char *p=malloc((size_t)size+1);CHECK(p);*n=fread(p,1,(size_t)size,f);CHECK(*n==(size_t)size);CHECK(!fclose(f));p[*n]=0;return p;
}
static int json_reply(struct bw_native_response *out,int status,const char *body) {
    size_t n=strlen(body);char *p=malloc(n+1);CHECK(p);memcpy(p,body,n+1);*out=(struct bw_native_response){status,"application/json",p,n};return 0;
}
void bw_native_response_clear(struct bw_native_response *r){if(r){free(r->bytes);memset(r,0,sizeof *r);}}
int bw_native_backend_handle_authenticated(struct bw_native_backend *p,const char *m,const char *u,const void *b,size_t n,const char *t,struct bw_native_response *r){
    (void)p;(void)m;(void)u;(void)b;(void)n;(void)t;(void)r;abort();
}
int bw_native_setup_nonce(char out[33]){(void)out;abort();}
int rc_game_identity_admit(const struct rc_game_selection *s,struct rc_game_identity *out){(void)s;(void)out;return -1;}
int rc_game_identity_current(const struct rc_game_identity *s,struct rc_game_identity *out){(void)s;(void)out;return -1;}
int rc_game_identity_admit_with_ops(const struct rc_game_identity_ops *o,const struct rc_game_selection *s,struct rc_game_identity *out){(void)o;(void)s;(void)out;return -1;}
int rc_game_identity_current_with_ops(const struct rc_game_identity_ops *o,const struct rc_game_identity *s,struct rc_game_identity *out){(void)o;(void)s;(void)out;return -1;}
static int native_read(void *opaque,const char *method,const char *url,const void *body,size_t n,const char *auth,struct bw_native_response *out) {
    struct fixture *f=opaque;(void)body;(void)n;(void)auth;CHECK(!strcmp(method,"GET"));
    if(!strncmp(url,"/cover?",7)){
        CHECK(!strcmp(url,"/cover?name=%50%43%20%44%65%6D%6F&v=7"));out->bytes=load(f->icon,&out->size);out->type="image/png";out->status=200;return 0;
    }
    CHECK(!strcmp(url,"/api/state"));
    char raw[4096];int written=snprintf(raw,sizeof raw,
        "{\"setup\":{\"token\":\"%s\",\"revision\":2},\"library\":{\"phase\":\"ready\",\"generation\":%u,\"stale\":false},"
        "\"stream\":{\"phase\":\"%s\",\"runId\":\"%s\",\"message\":\"Host native worker status\"},"
        "\"games\":[{\"title\":\"PC title\",\"titleId\":\"PPSA99912\",\"source\":\"PC Demo\","
        "\"sourceBinding\":\"PPSA99912_1111111111111111111111111111111111111111111111111111111111111111\","
        "\"playReason\":\"\",\"preparationReason\":\"\",\"startupReady\":true,\"canStart\":true,\"canPrepare\":true}],"
        "\"jobs\":[{\"id\":\"%s\",\"state\":\"%s\",\"message\":\"Host preparation status\"}]}",
        token,f->source_changed?8u:7u,!f->dispatches?"unverified":f->backend_result==2?"stopped":"preflight",f->dispatches?job:"",
        job,f->backend_result==2?"done":"running");
    CHECK(written>0&&(size_t)written<sizeof raw);return json_reply(out,200,raw);
}
static int local_http(void *opaque,enum bd_http_route route,const struct bd_http_options *options,struct bd_http_reply *out) {
    struct fixture *f=opaque;CHECK(options->timeout_ms>0);memset(out,0,sizeof *out);out->status=200;
    if(route==BD_HTTP_SMP_GAMES)out->bytes=load(f->smp,&out->size);
    else {CHECK(route==BD_HTTP_BFP_JOB_STATUS);f->bfp_calls++;
        if(f->bfp_mode==3){out->status=0;out->error="fixture_timeout";return -1;}
        if(f->bfp_mode==4)out->status=503;
        const char *raw=f->bfp_mode==2?"not JSON":f->bfp_mode==5?"{\"ok\":false,\"busy\":false}":
            f->bfp_mode==6?"{\"ok\":true,\"ok\":true,\"busy\":false}":
            f->bfp_mode==7?"{\"ok\":true,\"busy\":\"false\"}":
            f->bfp_busy?"{\"ok\":true,\"busy\":true}":"{\"ok\":true,\"busy\":false}";
        out->size=strlen(raw);out->bytes=malloc(out->size+1);CHECK(out->bytes);memcpy(out->bytes,raw,out->size+1);}
    return 0;
}
static int capable(void *opaque,const struct bd_item *item){(void)opaque;return item->kind==BD_LOCAL_FOLDER;}
static int activity_guard(void *opaque){struct fixture *f=opaque;f->guard_calls++;return f->guard_result;}
static void change_boot(struct fixture *f){
    size_t n=0,count=0;unsigned char *data=load(f->smp,&n);struct bd_smp_record row;
    CHECK(!bd_smp_records_parse(data,n,&row,1,&count)&&count==1);free(data);
    char path[1024];CHECK(snprintf(path,sizeof path,"%s/eboot.bin",row.source)>0);
    FILE *file=fopen(path,"ab");CHECK(file);CHECK(fputc('x',file)!=EOF);CHECK(!fclose(file));
}
static int full_idle(void *opaque){struct fixture *f=opaque;return !f->idle||f->front_present? -1:0;}
static int clock_ms(void *opaque,uint64_t *out){*out=((struct fixture *)opaque)->now;return 0;}
static int nonce(void *opaque,char out[33]){struct fixture *f=opaque;memset(out,'a',32);out[31]=(char)('1'+f->nonce++);out[32]=0;return 0;}
static int reserve(void *opaque,const char id[33],const char auth[33]){
    struct fixture *f=opaque;CHECK(!strcmp(auth,token));f->reserves++;if(f->reserved)return 1;f->reserved=1;memcpy(f->owner,id,33);return 0;
}
static int release(void *opaque,const char id[33]){struct fixture *f=opaque;CHECK(f->reserved&&!strcmp(f->owner,id));f->releases++;if(f->release_fail)return -1;f->reserved=0;return 0;}
static int reserved_backend(void *opaque,const char id[33],const char *method,const char *url,const void *body,size_t n,const char *auth,struct bw_native_response *out) {
    struct fixture *f=opaque;CHECK(f->reserved&&!strcmp(f->owner,id)&&!strcmp(auth,token)&&!strcmp(method,"POST"));
    struct bw_native_send_request request;CHECK(!bw_native_send_request_parse(body,n,&request));
    CHECK(!strcmp(request.source,"PC Demo")&&request.library_generation==7&&request.settings_revision==2);
    CHECK(!strcmp(url,"/api/start")||!strcmp(url,"/api/send"));snprintf(f->action,sizeof f->action,"%s",!strcmp(url,"/api/send")?"send":"play");f->dispatches++;
    if(f->backend_result<0)return -1;
    if(!f->backend_result)return json_reply(out,409,"{\"error\":\"Host refusal\"}");
    return json_reply(out,202,!strcmp(f->action,"send")?"{\"ok\":true,\"jobId\":\"22222222222222222222222222222222\"}":"{\"ok\":true,\"runId\":\"22222222222222222222222222222222\"}");
}
static int launch_local(void *opaque,const struct bd_item *item,const char id[33]){
    struct fixture *f=opaque;CHECK(f->reserved&&!strcmp(f->owner,id));CHECK(item->kind==BD_LOCAL_FOLDER&&!strcmp(item->title_id,"PPSA99911"));
    CHECK(!strcmp(item->source,item->registered_source));f->local_calls++;return f->local_result;
}
static int local_state(void *opaque,const char id[33]){struct fixture *f=opaque;CHECK(!strcmp(f->owner,id));return f->local_state;}
static int frontend_admit(void *opaque,struct bd_frontend_owner *owner,int32_t pid,uint32_t app) {
    struct fixture *f=opaque;if(pid!=900||app!=901||!f->front_present)return -1;
    if(owner->admitted)return owner->identity.pid==pid&&owner->identity.app_id==app?1:-1;
    owner->admitted=1;owner->original=(struct rc_game_identity){.process={pid,1234,567,"eboot.bin"},.firmware=BW_STOP_PROFILE_FW,.app_id=app,.admitted_wall_us=INT64_C(1235000000)};
    memcpy(owner->original.title,BD_FRONTEND_TITLE,10);
    owner->identity=(struct bd_frontend_identity){.pid=pid,.app_id=app,.birth_sec=1234,.birth_usec=567};
    memcpy(owner->identity.title_id,BD_FRONTEND_TITLE,10);fixture_package_fingerprint(&owner->expected,owner->identity.package_fingerprint);return 1;
}
static int frontend_current(void *opaque,const struct bd_frontend_owner *owner){CHECK(owner->admitted);struct fixture *f=opaque;return f->front_unknown?-1:(int)f->front_present;}
struct read_catalog {uint64_t generation;char pc[BD_ID_BYTES],local[BD_ID_BYTES],phase[64];unsigned active,exit_requested;int32_t frontend_pid;};
static int row_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct bd_item *row=opaque;if(kn==2&&!memcmp(key,"id",2))return bw_document_string(raw,n,row->id,sizeof row->id);
    if(kn==4&&!memcmp(key,"kind",4)){CHECK(n==1);row->kind=(enum bd_source_kind)(*(const char *)raw-'0');}return 0;
}
static int row_item(void *opaque,const void *raw,size_t n){struct read_catalog *out=opaque;struct bd_item row={0};CHECK(!bw_document_fields(raw,n,row_field,&row));
    if(row.kind==BD_PC_STREAM)memcpy(out->pc,row.id,sizeof out->pc);if(row.kind==BD_LOCAL_FOLDER)memcpy(out->local,row.id,sizeof out->local);return 0;}
static int operation_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){struct read_catalog *out=opaque;
    if(kn==5&&!memcmp(key,"phase",5))return bw_document_string(raw,n,out->phase,sizeof out->phase);
    if(kn==6&&!memcmp(key,"active",6))out->active=n==4&&!memcmp(raw,"true",4);
    else if(kn==13&&!memcmp(key,"exitRequested",13))out->exit_requested=n==4&&!memcmp(raw,"true",4);
    else if(kn==11&&!memcmp(key,"frontendPid",11)){char number[32];CHECK(n<sizeof number);memcpy(number,raw,n);number[n]=0;out->frontend_pid=(int32_t)strtol(number,NULL,10);}return 0;}
static int catalog_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){struct read_catalog *out=opaque;
    if(kn==17&&!memcmp(key,"catalogGeneration",17)){char number[32];CHECK(n<sizeof number);memcpy(number,raw,n);number[n]=0;out->generation=strtoull(number,NULL,10);}
    else if(kn==5&&!memcmp(key,"items",5))return bw_document_array(raw,n,row_item,opaque);
    else if(kn==9&&!memcmp(key,"operation",9))return bw_document_fields(raw,n,operation_field,opaque);return 0;}
static struct read_catalog catalog(struct bd_dashboard_service *s) {
    struct bw_native_response out={0};CHECK(!bd_service_handle(s,"GET","/api/dashboard/library",NULL,0,NULL,&out));CHECK(out.status==200);
    struct read_catalog value={0};CHECK(!bw_document_fields(out.bytes,out.size,catalog_field,&value));bw_native_response_clear(&out);
    static const char *phases[]={"idle","waiting-for-dashboard-exit","dispatching","accepted","refused","unknown","cancelled","running","stopped"};unsigned valid=0;
    for(size_t i=0;i<sizeof phases/sizeof phases[0];i++)valid|=!strcmp(value.phase,phases[i]);CHECK(valid);
    if(value.exit_requested)CHECK(value.active&&value.frontend_pid==900&&!strcmp(value.phase,"waiting-for-dashboard-exit"));else CHECK(value.frontend_pid==0);
    return value;
}
static void post(struct bd_dashboard_service *s,const char *url,const char *body,const char *auth,int status) {
    struct bw_native_response out={0};CHECK(!bd_service_handle(s,"POST",url,body,strlen(body),auth,&out));CHECK(out.status==status);bw_native_response_clear(&out);
}
static void action(struct bd_dashboard_service *s,const struct read_catalog *c,int local,const char *verb,const char *auth,int status,unsigned gen_delta) {
    char raw[512];int n=snprintf(raw,sizeof raw,"{\"id\":\"%s\",\"kind\":%u,\"action\":\"%s\",\"catalogGeneration\":%llu}",local?c->local:c->pc,local?2u:1u,verb,(unsigned long long)(c->generation+gen_delta));CHECK(n>0&&(size_t)n<sizeof raw);post(s,"/api/dashboard/action",raw,auth,status);
}
int main(int argc,char **argv){
    if(argc!=6)return 2;const char *mode=argv[1];struct fixture f={.smp=argv[2],.apps=argv[3],.database=argv[4],.icon=argv[5],.now=1000,.idle=1,.local_result=1,.local_state=1,.backend_result=1};
    struct bd_service_config config={.backend=(struct bw_native_backend *)&f,.apps_root=f.apps,.database=f.database,.observe_bfp=1,
        .ops={.opaque=&f,.idle=full_idle,.reserve=reserve,.release=release,.backend_reserved=reserved_backend,.local_launch=launch_local,.local_state=local_state,.local_capable=capable,
        .backend_read=native_read,.http=local_http,.frontend_admit=frontend_admit,.frontend_current=frontend_current,.now_ms=clock_ms,.nonce=nonce}};
    config.frontend_package.eboot.bytes=16;config.frontend_package.param.bytes=1;config.frontend_package.mount_link.bytes=1;
    config.frontend_package.eboot.sha256[0]=config.frontend_package.param.sha256[0]=config.frontend_package.mount_link.sha256[0]=1;
    if(!strncmp(mode,"default-",8)){config.observe_bfp=0;f.bfp_mode=3;}
    if(!strcmp(mode,"default-guard-refusal"))config.ops.copy_idle=activity_guard;
    struct bd_dashboard_service *s=bd_service_create(&config);CHECK(s);
    struct read_catalog c=catalog(s);CHECK(c.pc[0]&&c.local[0]&&c.generation);
    struct read_catalog stable=catalog(s);CHECK(stable.generation==c.generation);
    int retained=0;
    if(!strcmp(mode,"default-local")){
        action(s,&c,1,"play",token,202,0);CHECK(f.local_calls==1&&f.reserved&&!f.bfp_calls);
        f.local_state=0;CHECK(!bd_service_tick(s)&&!f.reserved&&!catalog(s).active);
    }else if(!strcmp(mode,"default-pc")||!strcmp(mode,"default-send")){
        action(s,&c,0,!strcmp(mode,"default-send")?"send":"play",token,202,0);
        CHECK(f.dispatches==1&&!f.reserved&&!f.bfp_calls);f.backend_result=2;CHECK(!catalog(s).active);
    }else if(!strcmp(mode,"default-native-handoff")||!strcmp(mode,"default-source-change-handoff")){
        f.front_present=1;post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);
        action(s,&c,1,"play",token,202,0);CHECK(f.reserved&&!f.local_calls);
        CHECK(!bd_service_tick(s)&&!f.local_calls);
        if(!strcmp(mode,"default-source-change-handoff"))change_boot(&f);
        f.front_present=0;CHECK(!bd_service_tick(s));
        if(!strcmp(mode,"default-source-change-handoff"))CHECK(!f.local_calls&&!f.reserved);
        else {CHECK(f.local_calls==1);f.local_state=0;CHECK(!bd_service_tick(s));}
        CHECK(!catalog(s).active&&!f.bfp_calls);
    }else if(!strcmp(mode,"default-busy-game")){
        f.idle=0;action(s,&c,1,"play",token,409,0);CHECK(!f.local_calls&&!f.dispatches&&!f.bfp_calls);
    }else if(!strcmp(mode,"default-reservation-busy")){
        f.reserved=1;action(s,&c,1,"play",token,409,0);CHECK(!f.local_calls&&!f.dispatches&&f.reserves==1&&!f.bfp_calls);f.reserved=0;
    }else if(!strcmp(mode,"default-source-change")){
        change_boot(&f);action(s,&c,1,"play",token,409,0);CHECK(!f.local_calls&&!f.bfp_calls);
    }else if(!strcmp(mode,"default-guard-refusal")){
        f.guard_result=-1;action(s,&c,1,"play",token,409,0);CHECK(!f.local_calls&&f.guard_calls&&!f.bfp_calls);
    }else if(!strncmp(mode,"optional-",9)){
        if(!strcmp(mode,"optional-busy"))f.bfp_busy=1;
        else if(!strcmp(mode,"optional-malformed"))f.bfp_mode=2;
        else if(!strcmp(mode,"optional-timeout"))f.bfp_mode=3;
        else if(!strcmp(mode,"optional-status"))f.bfp_mode=4;
        else if(!strcmp(mode,"optional-not-ok"))f.bfp_mode=5;
        else if(!strcmp(mode,"optional-duplicate"))f.bfp_mode=6;
        else if(!strcmp(mode,"optional-type"))f.bfp_mode=7;
        else CHECK(0);
        action(s,&c,1,"play",token,409,0);CHECK(!f.local_calls&&!f.dispatches&&f.bfp_calls);
    }else if(!strcmp(mode,"pc-browser")||!strcmp(mode,"send-browser")){
        action(s,&c,0,!strcmp(mode,"send-browser")?"send":"play",token,202,0);CHECK(f.dispatches==1&&!f.reserved&&f.releases==1);
        CHECK(catalog(s).active);f.backend_result=2;CHECK(!catalog(s).active);
    }else if(!strcmp(mode,"native-wait")||!strcmp(mode,"browser-queues-native")){
        f.front_present=1;post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);
        post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);
        action(s,&c,0,"play",token,202,0);CHECK(f.reserved&&!f.dispatches);CHECK(!bd_service_tick(s)&&!f.dispatches);
        struct read_catalog queued=catalog(s);CHECK(queued.exit_requested&&queued.frontend_pid==900&&queued.active);
        f.front_present=0;CHECK(!bd_service_tick(s)&&f.dispatches==1);f.backend_result=2;CHECK(!catalog(s).active);
    }else if(!strcmp(mode,"local-browser")||!strcmp(mode,"local-unknown-recovery")){
        if(!strcmp(mode,"local-unknown-recovery"))f.local_result=-1;
        action(s,&c,1,"play",token,f.local_result<0?503:202,0);CHECK(f.local_calls==1&&f.reserved);
        CHECK(catalog(s).active);CHECK(f.local_calls==1);f.local_state=0;CHECK(!bd_service_tick(s));CHECK(!catalog(s).active&&!f.reserved);
    }else if(!strcmp(mode,"native-timeout")||!strcmp(mode,"native-rewind")){
        f.front_present=1;post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);action(s,&c,0,"play",token,202,0);
        f.now=!strcmp(mode,"native-timeout")?16000:999;CHECK(!bd_service_tick(s));CHECK(!catalog(s).active&&!f.dispatches&&!f.reserved);
    }else if(!strcmp(mode,"external-game-before-handoff")||!strcmp(mode,"source-change-before-handoff")||!strcmp(mode,"frontend-unknown-before-handoff")){
        f.front_present=1;post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);action(s,&c,0,"play",token,202,0);f.front_present=0;
        if(!strcmp(mode,"external-game-before-handoff"))f.idle=0;
        if(!strcmp(mode,"source-change-before-handoff"))f.source_changed=1;
        if(!strcmp(mode,"frontend-unknown-before-handoff"))f.front_unknown=1;
        CHECK(!bd_service_tick(s));CHECK(!catalog(s).active&&!f.dispatches&&!f.reserved);
    }else if(!strcmp(mode,"bad-token"))action(s,&c,0,"play","33333333333333333333333333333333",403,0);
    else if(!strcmp(mode,"stale-generation"))action(s,&c,0,"play",token,409,1);
    else if(!strcmp(mode,"changed-native-source")){f.source_changed=1;action(s,&c,0,"play",token,409,0);}
    else if(!strcmp(mode,"busy-game")){f.idle=0;action(s,&c,0,"play",token,409,0);}
    else if(!strcmp(mode,"busy-transfer")){f.bfp_busy=1;action(s,&c,1,"play",token,409,0);}
    else if(!strcmp(mode,"frontend-without-hello")){f.front_present=1;action(s,&c,0,"play",token,409,0);}
    else if(!strcmp(mode,"hello-wrong-identity"))post(s,"/api/dashboard/frontend","{\"pid\":902,\"appId\":901}",token,409);
    else if(!strcmp(mode,"hello-unknown-field"))post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901,\"title\":\"PPSA99171\"}",token,400);
    else if(!strcmp(mode,"action-duplicate-field"))post(s,"/api/dashboard/action","{\"id\":\"a\",\"id\":\"b\",\"kind\":1,\"action\":\"play\",\"catalogGeneration\":1}",token,400);
    else if(!strcmp(mode,"send-local-refused"))action(s,&c,1,"send",token,409,0);
    else if(!strcmp(mode,"definite-native-refusal")){f.backend_result=0;action(s,&c,0,"play",token,409,0);CHECK(f.dispatches==1&&!f.reserved&&!catalog(s).active);}
    else if(!strcmp(mode,"unknown-native-no-replay")||!strcmp(mode,"reservation-release-failure")){
        if(!strcmp(mode,"unknown-native-no-replay"))f.backend_result=-1;else f.release_fail=1;
        action(s,&c,0,"play",token,503,0);CHECK(f.dispatches==1&&f.reserved);action(s,&c,0,"play",token,409,0);CHECK(f.dispatches==1&&bd_service_destroy(s)==1);retained=1;
    }else if(!strcmp(mode,"cover-local")||!strcmp(mode,"cover-pc")){
        char url[160];snprintf(url,sizeof url,"/api/dashboard/cover?id=%s",!strcmp(mode,"cover-pc")?c.pc:c.local);
        struct bw_native_response out={0};CHECK(!bd_service_handle(s,"GET",url,NULL,0,NULL,&out)&&out.status==200&&!strcmp(out.type,"image/png"));
        size_t expected_n=0;unsigned char *expected=load(f.icon,&expected_n);CHECK(out.size==expected_n&&!memcmp(out.bytes,expected,expected_n));free(expected);bw_native_response_clear(&out);
    }else if(!strcmp(mode,"cover-path-injection")){
        struct bw_native_response out={0};CHECK(!bd_service_handle(s,"GET","/api/dashboard/cover?id=../../outside",NULL,0,NULL,&out)&&out.status==400);bw_native_response_clear(&out);
    }else CHECK(0);
    if(!retained)CHECK(!bd_service_destroy(s));
    printf("{\"passed\":true,\"case\":\"%s\",\"checks\":%u,\"dispatchCalls\":%u,\"localLaunchCalls\":%u,\"retainedOwnerAtProcessEnd\":%s}\n",mode,checks,f.dispatches,f.local_calls,retained?"true":"false");
    if(retained){fflush(stdout);_Exit(0);}return 0;
}
