/* Actual service and catalog; platform/backend effects are explicitly modeled. */
#define main retained_feedback_fixture_main
#include "test_launch_feedback_service.c"
#undef main
#include "native_removal_request.h"
#include <sys/stat.h>

struct removal_fixture {struct fixture base;unsigned startup_only,removed,restore,done,unknown,error,bad_id,bad_counts,pc_cap,local_cap,local_polls;unsigned observe_calls,observe_complete,observe_foreign,observe_error;int local_terminal;};
static int removal_read(void *opaque,const char *method,const char *url,const void *body,size_t size,const char *auth,struct bw_native_response *out){
    struct removal_fixture *r=opaque;struct fixture *f=&r->base;
    if(strcmp(url,"/api/state"))return native_read(f,method,url,body,size,auth,out);
    if(f->status_error)return json_reply(out,503,"{\"error\":\"Fresh removal status unavailable\"}");
    CHECK(!strcmp(method,"GET"));char raw[8192];
    int n=snprintf(raw,sizeof raw,"{\"setup\":{\"token\":\"%s\",\"revision\":2},\"library\":{\"phase\":\"ready\",\"generation\":7,\"stale\":false},"
        "\"stream\":{\"phase\":\"stopped\",\"runId\":\"\",\"gamePid\":0,\"message\":\"No game running\"},\"jobs\":[],"
        "\"games\":[{\"title\":\"PC title\",\"titleId\":\"PPSA99912\",\"source\":\"PC Demo\",\"sourceBinding\":\"PPSA99912_%064d\",\"playReason\":\"\",\"preparationReason\":\"\","
        "\"startupReady\":%s,\"canStart\":%s,\"canPrepare\":%s,\"canUninstall\":%s,\"canRemoveStartup\":%s,\"removed\":%s,\"canRestore\":%s}],"
        "\"removal\":{\"operationId\":\"%s\",\"source\":\"PC Demo\",\"titleId\":\"PPSA99912\",\"state\":\"%s\",\"phase\":%u,\"files\":%u,\"filesTotal\":3,\"message\":\"Exact operation observation\",\"preparationOnly\":%s,\"removed\":%s}}",
        token,1,r->removed||f->pc_unready?"false":"true",r->removed||f->pc_unready?"false":"true",!r->removed&&f->pc_unready?"true":"false",
        r->pc_cap&&!r->startup_only&&!r->removed?"true":"false",r->pc_cap&&r->startup_only&&!r->done?"true":"false",r->removed?"true":"false",r->removed?"true":"false",
        r->bad_id?job:f->owner,r->unknown?"unknown":r->error?"error":r->done?"done":"running",r->unknown?7u:r->error?6u:r->done?5u:2u,r->bad_counts?4u:r->done?3u:1u,r->startup_only?"true":"false",r->done&&!r->restore&&!r->startup_only?"true":"false");
    CHECK(n>0&&(size_t)n<sizeof raw);return json_reply(out,200,raw);
}
static int removal_backend(void *opaque,const char id[33],const char *method,const char *url,const void *body,size_t size,const char *auth,struct bw_native_response *out){
    struct removal_fixture *r=opaque;struct fixture *f=&r->base;CHECK(f->reserved&&!strcmp(f->owner,id)&&!strcmp(auth,token)&&!strcmp(method,"POST"));
    CHECK(!strcmp(url,"/api/uninstall")||!strcmp(url,"/api/restore")||!strcmp(url,"/api/remove-startup"));CHECK(r->startup_only==!strcmp(url,"/api/remove-startup"));r->restore=!strcmp(url,"/api/restore");
    struct bw_native_removal_request q;CHECK(!bw_native_removal_request_parse(body,size,r->restore,&q));
    CHECK(!strcmp(q.selected.source,"PC Demo")&&q.selected.library_generation==7&&q.selected.settings_revision==2);
    CHECK(r->restore?!q.confirmed_title[0]:!strcmp(q.confirmed_title,"PPSA99912"));f->dispatches++;
    if(f->backend_result<0)return -1;if(!f->backend_result)return json_reply(out,409,"{\"error\":\"Exact source refused\"}");
    char reply[160];snprintf(reply,sizeof reply,"{\"ok\":true,\"operationId\":\"%s\"}",id);return json_reply(out,202,reply);
}
static int local_remove_capable(void *opaque,const struct bd_item *item){struct removal_fixture *r=opaque;return r->local_cap&&item->kind==BD_LOCAL_FOLDER;}
static int local_remove(void *opaque,const struct bd_item *item,const char id[33],const char title[10]){
    struct removal_fixture *r=opaque;struct fixture *f=&r->base;CHECK(f->reserved&&!strcmp(id,f->owner)&&!strcmp(title,item->title_id)&&!strcmp(title,"PPSA99911"));f->local_calls++;return f->local_result;
}
static int local_removal_state(void *opaque,const char id[33],struct bw_game_uninstall_result *out){
    struct removal_fixture *r=opaque;CHECK(!strcmp(id,r->base.owner));r->local_polls++;
    *out=(struct bw_game_uninstall_result){.phase=r->unknown?BW_GAME_REMOVE_UNKNOWN:r->done?BW_GAME_REMOVE_DONE:BW_GAME_REMOVE_FILES,
        .mutation_started=1,.source_absent=r->done,.registration_confirmed=r->done,.library_removed=r->done,.files_removed=r->done?3u:1u,.files_total=3,.reason="Exact local removal observation"};
    return r->unknown?-1:r->done?1:0;
}
static int removal_observe(void *opaque,const char id[33],struct bw_native_cleanup_observation *out){
    struct removal_fixture *r=opaque;struct fixture *f=&r->base;
    CHECK(f->reserved&&!strcmp(id,f->owner)&&f->dispatches==1&&r->unknown&&f->idle);r->observe_calls++;
    /* Re-entering these real APIs proves the observation callback is outside
     * the service mutex and cannot destroy its retained owner. */
    CHECK(bd_service_destroy(f->service)==1&&catalog(f->service).active);
    if(r->observe_complete){r->unknown=0;r->done=1;r->removed=1;}
    if(r->observe_error){r->unknown=0;r->error=1;}
    if(r->observe_foreign)r->bad_id=1;
    /* Even a positive arbitrary observation is never terminal authority. */
    memcpy(out->title,"PPSA90000",10);memcpy(out->run,job,33);out->absent=1;return 0;
}
static void removal_action_post(struct bd_dashboard_service *s,const struct read_catalog *c,unsigned local,const char *verb,const char *confirm,int expected){
    char raw[512];int n=snprintf(raw,sizeof raw,"{\"id\":\"%s\",\"kind\":%u,\"action\":\"%s\",\"catalogGeneration\":%llu%s%s%s}",local?c->local:c->pc,local?2u:1u,verb,(unsigned long long)c->generation,
        confirm?",\"confirmedTitleId\":\"":"",confirm?confirm:"",confirm?"\"":"");CHECK(n>0&&(size_t)n<sizeof raw);post(s,"/api/dashboard/action",raw,token,expected);
}
static int observed_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    unsigned *out=opaque;if(kn==8&&!memcmp(key,"observed",8)){CHECK((size==4&&!memcmp(raw,"true",4))||(size==5&&!memcmp(raw,"false",5)));*out=size==4;}return 0;
}
static int progress_operation(void *opaque,const char *key,size_t kn,const void *raw,size_t size){return kn==7&&!memcmp(key,"removal",7)?bw_document_fields(raw,size,observed_field,opaque):0;}
static int progress_root(void *opaque,const char *key,size_t kn,const void *raw,size_t size){return kn==9&&!memcmp(key,"operation",9)?bw_document_fields(raw,size,progress_operation,opaque):0;}
static unsigned removal_observed(struct bd_dashboard_service *s){struct bw_native_response out={0};CHECK(!bd_service_handle(s,"GET","/api/dashboard/library",NULL,0,NULL,&out)&&out.status==200);unsigned value=99;CHECK(!bw_document_fields(out.bytes,out.size,progress_root,&value)&&value<=1);bw_native_response_clear(&out);return value;}
int main(int argc,char **argv){
    if(argc!=6)return 2;const char *mode=argv[1];struct removal_fixture r={.base={.smp=argv[2],.apps=argv[3],.database=argv[4],.icon=argv[5],.now=1000,.idle=1,.local_result=1,.local_state=1,.backend_result=1},.pc_cap=1,.local_cap=1};
    struct fixture *f=&r.base;unsigned local=strstr(mode,"local")!=NULL,native=strstr(mode,"native")!=NULL,restore=strstr(mode,"restore")!=NULL;
    r.startup_only=strstr(mode,"startup")!=NULL;r.removed=restore;r.restore=restore;f->front_present=native;if(strstr(mode,"send-only"))f->pc_unready=1;
    struct bd_service_config cfg={.backend=(struct bw_native_backend *)f,.apps_root=f->apps,.database=f->database,.observe_bfp=1,
        .ops={.opaque=&r,.idle=full_idle,.reserve=reserve,.release=release,.backend_reserved=removal_backend,.local_launch=launch_local,.local_state=local_state,.local_capable=capable,
          .local_uninstall_capable=local_remove_capable,.local_uninstall=local_remove,.local_uninstall_state=local_removal_state,.removal_observe=removal_observe,.backend_read=removal_read,.http=local_http,.frontend_admit=frontend_admit,.frontend_current=frontend_current,.now_ms=clock_ms,.nonce=nonce,.feedback=notify_feedback}};
    cfg.frontend_package.eboot.bytes=16;cfg.frontend_package.param.bytes=cfg.frontend_package.mount_link.bytes=1;cfg.frontend_package.eboot.sha256[0]=cfg.frontend_package.param.sha256[0]=cfg.frontend_package.mount_link.sha256[0]=1;
    struct bd_dashboard_service *s=bd_service_create(&cfg);CHECK(s);f->service=s;struct read_catalog c=catalog(s);CHECK(c.pc[0]&&c.local[0]);
    const char *verb=restore?"restore":r.startup_only?"remove-startup":"uninstall",*title=restore?NULL:local?"PPSA99911":"PPSA99912";unsigned retained=0;
    if(!strcmp(mode,"startup-confirmation")){
        removal_action_post(s,&c,0,"remove-startup",NULL,400);removal_action_post(s,&c,0,"remove-startup","PPSA90000",409);removal_action_post(s,&c,1,"remove-startup","PPSA99911",409);removal_action_post(s,&c,0,"uninstall","PPSA99912",409);CHECK(!f->reserves&&!f->dispatches);
    }else if(!strcmp(mode,"confirmation")){
        removal_action_post(s,&c,0,"uninstall",NULL,400);removal_action_post(s,&c,0,"uninstall","PPSA90000",409);removal_action_post(s,&c,0,"play","PPSA99912",400);CHECK(!f->reserves&&!f->dispatches);
    }else if(!strcmp(mode,"busy")){f->idle=0;removal_action_post(s,&c,0,verb,title,409);CHECK(!f->reserves&&!f->dispatches);}
    else if(!strcmp(mode,"source-absent")){
        size_t size=0,count=0;unsigned char *wire=load(f->smp,&size);struct bd_smp_record item;CHECK(!bd_smp_records_parse(wire,size,&item,1,&count)&&count==1);free(wire);
        char moved[1200];CHECK(snprintf(moved,sizeof moved,"%s-renamed",item.source)>0);CHECK(!rename(item.source,moved));CHECK(!catalog(s).local[0]);CHECK(!rename(moved,item.source));CHECK(catalog(s).local[0]);
    }else{
        if(native)post(s,"/api/dashboard/frontend","{\"pid\":900,\"appId\":901}",token,200);
        if(strstr(mode,"lost-reply"))f->backend_result=-1;
        if(!strcmp(mode,"refused"))f->backend_result=0;
        removal_action_post(s,&c,local,verb,title,f->backend_result<0?503:f->backend_result==0?409:202);
        if(native){CHECK(!f->dispatches&&!f->local_calls&&f->reserved&&catalog(s).exit_requested);CHECK(!bd_service_tick(s)&&!f->dispatches&&!f->local_calls);f->front_present=0;CHECK(!bd_service_tick(s));}
        CHECK(f->dispatches+f->local_calls==1);
        if(!strcmp(mode,"refused")){CHECK(!f->reserved&&!catalog(s).active);}
        else if(strstr(mode,"reconcile")){
            r.unknown=1;CHECK(catalog(s).active&&removal_observed(s));
            r.observe_complete=strstr(mode,"done")!=NULL||strstr(mode,"foreign")!=NULL;r.observe_foreign=strstr(mode,"foreign")!=NULL;
            r.observe_error=strstr(mode,"error")!=NULL;
            unsigned busy=strstr(mode,"busy")!=NULL;if(busy)f->idle=0;
            CHECK(!bd_service_tick(s));CHECK(r.observe_calls==!busy&&f->dispatches==1);
            if(r.observe_complete&&!r.observe_foreign){CHECK(!f->reserved&&!catalog(s).active);}
            else{
                CHECK(f->reserved&&catalog(s).active);
                if(strstr(mode,"throttle")){CHECK(!bd_service_tick(s)&&r.observe_calls==1);f->now+=4999;CHECK(!bd_service_tick(s)&&r.observe_calls==1);f->now++;CHECK(!bd_service_tick(s)&&r.observe_calls==2);}
                f->idle=1;r.bad_id=0;r.unknown=0;r.error=0;r.done=1;r.removed=1;
                CHECK(!bd_service_tick(s)&&!f->reserved&&!catalog(s).active&&f->dispatches==1);
            }
        }
        else if(strstr(mode,"lost-reply")){
            r.bad_id=1;CHECK(f->reserved&&catalog(s).active);removal_action_post(s,&c,0,verb,title,409);CHECK(f->dispatches==1);
            if(!strcmp(mode,"lost-reply-held"))retained=1;
            else {r.bad_id=0;CHECK(!bd_service_tick(s)&&f->reserved&&catalog(s).active);CHECK(!strcmp(catalog(s).phase,"accepted"));
                r.done=1;r.removed=1;CHECK(!bd_service_tick(s)&&!f->reserved&&!catalog(s).active&&f->dispatches==1);}
        }
        else{
            CHECK(f->reserved&&catalog(s).active);removal_action_post(s,&c,local,verb,title,409);CHECK(f->dispatches+f->local_calls==1);
            if(strstr(mode,"unknown")){r.unknown=1;CHECK(!bd_service_tick(s)&&f->reserved&&catalog(s).active);r.unknown=0;}
            if(!strcmp(mode,"wrong-status")){r.bad_id=1;CHECK(!bd_service_tick(s)&&f->reserved&&catalog(s).active);r.bad_id=0;r.bad_counts=1;CHECK(!bd_service_tick(s)&&f->reserved);r.bad_counts=0;}
            if(!strcmp(mode,"status-unavailable")){CHECK(removal_observed(s));f->status_error=1;CHECK(!removal_observed(s)&&f->reserved);f->status_error=0;CHECK(removal_observed(s)&&f->reserved);}
            r.done=1;r.removed=!restore&&!r.startup_only;CHECK(!bd_service_tick(s));CHECK(!f->reserved&&!catalog(s).active);CHECK(f->dispatches+f->local_calls==1&&f->notices>=2);
        }
    }
    if(!retained)CHECK(!bd_service_destroy(s));else {CHECK(bd_service_destroy(s)==1);fflush(stdout);}
    printf("{\"passed\":true,\"case\":\"%s\",\"checks\":%u,\"backendCalls\":%u,\"localCalls\":%u,\"retained\":%s}\n",mode,checks,f->dispatches,f->local_calls,retained?"true":"false");
    if(retained){fflush(stdout);_Exit(0);}return 0;
}
