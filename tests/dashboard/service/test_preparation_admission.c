/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual service + setup/copy_fs lifecycle parser. Platform catalog/auth,
 * backend reservation/202, and exact ordinary-log stdio are explicit host seams.
 * No console, network, real game, credentials, or user save is used. */
#define _GNU_SOURCE 1
#define main retained_feedback_fixture_main
#define bw_native_setup_nonce unused_fixture_setup_nonce
#define bw_native_backend_set_preparation_idle unused_fixture_set_preparation_idle
#define bw_native_backend_clear_preparation_idle unused_fixture_clear_preparation_idle
#include "../lifecycle/test_launch_feedback_service.c"
#undef bw_native_backend_clear_preparation_idle
#undef bw_native_backend_set_preparation_idle
#undef bw_native_setup_nonce
#undef main
#include "native_setup.h"
#include <errno.h>

struct expected {
    const char *name,*slug;enum bw_setup_idle_reason reason;
    unsigned pass,accepted,legacy,close_failed;int error;
};
static const struct expected expectations[]={
    {"readable-empty","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"matching-stopped","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"stale-errno-empty","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"missing","log-open",BW_SETUP_IDLE_LOG_OPEN,1,0,0,0,ENOENT},
    {"open-noerrno","log-open",BW_SETUP_IDLE_LOG_OPEN,1,0,0,0,0},
    {"open-denied","log-open",BW_SETUP_IDLE_LOG_OPEN,1,0,0,0,EACCES},
    {"unmatched-started","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"malformed-pid","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"capacity","log-capacity",BW_SETUP_IDLE_LOG_CAPACITY,1,0,0,0,0},
    {"overlong-line","log-incomplete",BW_SETUP_IDLE_LOG_INCOMPLETE,1,0,0,0,0},
    {"short-eof","log-incomplete",BW_SETUP_IDLE_LOG_INCOMPLETE,1,0,0,0,0},
    {"read-error","log-read",BW_SETUP_IDLE_LOG_READ,1,0,0,0,EIO},
    {"read-noerrno","log-read",BW_SETUP_IDLE_LOG_READ,1,0,0,0,0},
    {"close-error","log-close",BW_SETUP_IDLE_LOG_CLOSE,1,0,0,1,EBUSY},
    {"close-noerrno","log-close",BW_SETUP_IDLE_LOG_CLOSE,1,0,0,1,0},
    {"read-close-error","log-read",BW_SETUP_IDLE_LOG_READ,1,0,0,1,EIO},
    {"second-unmatched","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,2,0,0,0,0},
    {"second-read-error","log-read",BW_SETUP_IDLE_LOG_READ,2,0,0,0,EIO},
    {"second-close-error","log-close",BW_SETUP_IDLE_LOG_CLOSE,2,0,0,1,EBUSY},
    {"unmatched-close-error","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,1,0},
    {"short-close-error","log-incomplete",BW_SETUP_IDLE_LOG_INCOMPLETE,1,0,0,1,0},
    {"legacy-unmatched","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,1,0,0},
    {"legacy-empty","stopped",BW_SETUP_IDLE_STOPPED,0,1,1,0,0},
    {"handoff-awaiting","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-active","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-final-new","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"handoff-stale-final-old","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-direct-active","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-direct-final-new","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"handoff-pending-same-pid-active","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-pending-same-pid-final","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"handoff-direct-same-pid-invalid","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"handoff-zero-app-active","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"handoff-zero-known-final","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0},
    {"handoff-known-app-mismatch","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"handoff-orphan","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"handoff-wrong-old-pid","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"handoff-malformed","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"handoff-trailing","log-record",BW_SETUP_IDLE_LOG_RECORD,1,0,0,0,0},
    {"cusa-start","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"lapy-start","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"fake-start","unmatched-start",BW_SETUP_IDLE_UNMATCHED_START,1,0,0,0,0},
    {"stock-families-final","stopped",BW_SETUP_IDLE_STOPPED,0,1,0,0,0}
};
static const struct expected *expectation;
static const char *case_name,*redirected_log;
static unsigned actual_idle_calls,observed_idle_calls,legacy_idle_calls,stock_log_opens,stock_log_closes,guard_attach_calls,guard_clear_calls;
static int idle_results[16];
static struct bw_setup_idle_observation idle_observations[16];
static FILE *active_log;
static int (*attached_guard)(void *,const char[33],const char[33],enum bw_prepare_guard_point);
static void *attached_owner;
static int named(const char *name){return !strcmp(case_name,name);}
struct fault_cookie {const char *bytes;size_t at;int read_error,read_noerrno,close_error,close_noerrno;};
static ssize_t fault_read(void *opaque,char *out,size_t size){
    struct fault_cookie *p=opaque;
    if(p->read_error||p->read_noerrno){errno=p->read_error;return -1;}
    size_t remaining=strlen(p->bytes)-p->at;if(size>remaining)size=remaining;memcpy(out,p->bytes+p->at,size);p->at+=size;return (ssize_t)size;
}
static int fault_close(void *opaque){
    struct fault_cookie *p=opaque;int error=p->close_error,failed=error||p->close_noerrno;free(p);
    if(failed){errno=error;return -1;}return 0;
}
FILE *__real_fopen(const char *,const char *);
int __real_fclose(FILE *);
FILE *__wrap_fopen(const char *path,const char *mode){
    if(strcmp(path,"/data/shadowmount/debug.log"))return __real_fopen(path,mode);
    CHECK(!strcmp(mode,"r")&&!active_log);stock_log_opens++;
    if(named("open-noerrno")){errno=0;return NULL;}
    if(named("open-denied")){errno=EACCES;return NULL;}
    if(named("second-unmatched")&&stock_log_opens==2){
        FILE *w=__real_fopen(redirected_log,"a");CHECK(w&&fputs("[GAME] started: PPSA99001 pid=24\n",w)>=0&&!__real_fclose(w));
    }
    unsigned fault=(!strncmp(case_name,"second-",7)?stock_log_opens==2:1);
    if(fault&&(strstr(case_name,"read-error")||named("read-noerrno")||strstr(case_name,"close-error")||named("close-noerrno"))){
        struct fault_cookie *p=calloc(1,sizeof *p);CHECK(p);p->bytes="";
        if(named("unmatched-close-error"))p->bytes="[GAME] started: PPSA99001 pid=24\n";
        if(named("short-close-error"))p->bytes="[GAME] started: PPSA99001 pid=24";
        p->read_error=strstr(case_name,"read-error")||named("read-close-error")?EIO:0;p->read_noerrno=named("read-noerrno");
        p->close_error=strstr(case_name,"close-error")?(named("read-close-error")?ENOSPC:EBUSY):0;p->close_noerrno=named("close-noerrno");
        active_log=fopencookie(p,"r",(cookie_io_functions_t){.read=fault_read,.close=fault_close});CHECK(active_log);return active_log;
    }
    active_log=__real_fopen(redirected_log,mode);return active_log;
}
int __wrap_fclose(FILE *fp){unsigned tracked=fp==active_log;if(tracked){active_log=NULL;stock_log_closes++;}return __real_fclose(fp);}
static int actual_stock_idle(void *opaque){
    (void)opaque;CHECK(actual_idle_calls<16);errno=EDOM;int rc=bw_native_setup_idle(NULL);
    idle_results[actual_idle_calls++]=rc;legacy_idle_calls++;return rc;
}
static int actual_observed_idle(void *opaque,struct bw_setup_idle_observation *out){
    (void)opaque;CHECK(out&&actual_idle_calls<16);memset(out,0xff,sizeof *out);errno=EDOM;
    int rc=bw_native_setup_idle_observe(NULL,out);idle_observations[actual_idle_calls]=*out;
    idle_results[actual_idle_calls++]=rc;observed_idle_calls++;return rc;
}
int bw_native_backend_set_preparation_idle(struct bw_native_backend *p,
    int (*fn)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *owner){
    CHECK(p&&fn&&owner&&!attached_guard&&!attached_owner);guard_attach_calls++;attached_guard=fn;attached_owner=owner;return 0;
}
int bw_native_backend_clear_preparation_idle(struct bw_native_backend *p,
    int (*fn)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *owner){
    CHECK(p&&fn==attached_guard&&owner==attached_owner);guard_clear_calls++;attached_guard=NULL;attached_owner=NULL;return 0;
}
static int response_error(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    if(kn==5&&!memcmp(key,"error",5))return bw_document_string(raw,n,opaque,768);return 0;
}
int main(int argc,char **argv){
    CHECK(argc==7);case_name=argv[1];redirected_log=argv[6];
    for(unsigned i=0;i<sizeof expectations/sizeof *expectations;i++)if(named(expectations[i].name))expectation=expectations+i;
    CHECK(expectation);
    struct fixture f={.smp=argv[2],.apps=argv[3],.database=argv[4],.icon=argv[5],
        .now=1000,.idle=1,.local_result=1,.local_state=1,.backend_result=1,.pc_unready=1};
    struct bd_service_config cfg={.backend=(struct bw_native_backend *)&f,.apps_root=f.apps,.database=f.database,.native_frontend_disabled=1,.observe_bfp=0,
        .ops={.opaque=&f,.idle=actual_stock_idle,.reserve=reserve,.release=release,.backend_reserved=reserved_backend,.local_launch=launch_local,
            .local_state=local_state,.local_capable=capable,.backend_read=native_read,.http=local_http,.now_ms=clock_ms,.nonce=nonce}};
    if(!expectation->legacy)cfg.ops.idle_observe=actual_observed_idle;
    struct bd_dashboard_service *s=bd_service_create(&cfg);CHECK(s);f.service=s;
    CHECK(!bd_service_attach_preparation_guard(s)&&guard_attach_calls==1);
    struct read_catalog c=catalog(s);CHECK(c.pc[0]&&c.generation&&c.pc_send&&!c.active);CHECK(!actual_idle_calls&&!stock_log_opens);
    char raw[512];int n=snprintf(raw,sizeof raw,"{\"id\":\"%s\",\"kind\":1,\"action\":\"send\",\"catalogGeneration\":%llu}",c.pc,(unsigned long long)c.generation);CHECK(n>0&&(size_t)n<sizeof raw);
    struct bw_native_response out={0};CHECK(!bd_service_handle(s,"POST","/api/dashboard/action",raw,(size_t)n,token,&out));
    CHECK(out.status==(expectation->accepted?202:409));char error[768]={0};CHECK(!bw_document_fields(out.bytes,out.size,response_error,error));
    unsigned idle_at_response=actual_idle_calls,opens_at_response=stock_log_opens,observed_at_response=observed_idle_calls,legacy_at_response=legacy_idle_calls;
    CHECK(idle_at_response==opens_at_response&&idle_at_response==observed_at_response+legacy_at_response&&!active_log);
    unsigned open_failed=named("missing")||named("open-noerrno")||named("open-denied");
    CHECK(stock_log_closes==opens_at_response-open_failed);
    if(!expectation->accepted){
        CHECK(!f.reserves&&!f.reserved&&!f.releases&&!f.dispatches&&!*f.action);
        CHECK(idle_at_response==expectation->pass&&idle_results[idle_at_response-1]!=0);if(expectation->pass==2)CHECK(!idle_results[0]);
        CHECK(!strstr(error,"PPSA")&&!strstr(error,"/data")&&!strstr(error,redirected_log)&&!strstr(error,"PC Demo")&&!strstr(error,"is running"));
        if(expectation->legacy){CHECK(!observed_at_response&&legacy_at_response==1);CHECK(!strcmp(error,"The exact frontend or preparation idle authority could not be retained."));}
        else{
            CHECK(observed_at_response==expectation->pass&&!legacy_at_response);
            struct bw_setup_idle_observation *seen=idle_observations+idle_at_response-1;
            CHECK(seen->reason==expectation->reason&&seen->error_valid==(unsigned)(expectation->error!=0)&&seen->error==expectation->error&&seen->close_failed==expectation->close_failed);
            char code[192];if(expectation->error)snprintf(code,sizeof code,"[preparation/idle-%s/%s, errno=%d]",expectation->pass==1?"first":"second",expectation->slug,expectation->error);
            else snprintf(code,sizeof code,"[preparation/idle-%s/%s]",expectation->pass==1?"first":"second",expectation->slug);
            CHECK(strstr(error,code)&&strstr(error,"No preparation was accepted."));
            CHECK((strstr(error,"errno=")!=NULL)==(expectation->error!=0));
            CHECK((strstr(error,"A log close failure was also reported.")!=NULL)==(expectation->close_failed&&expectation->reason!=BW_SETUP_IDLE_LOG_CLOSE));
            if(expectation->reason==BW_SETUP_IDLE_UNMATCHED_START)CHECK(strstr(error,"log has an unmatched start record")&&strstr(error,"any running game normally"));
        }
        struct read_catalog after=catalog(s);CHECK(!after.active&&actual_idle_calls==idle_at_response&&stock_log_opens==opens_at_response);
    }else{
        CHECK(f.reserves==1&&f.dispatches==1&&!strcmp(f.action,"send")&&idle_at_response==6);
        CHECK(observed_at_response==(expectation->legacy?0u:2u)&&legacy_at_response==(expectation->legacy?6u:4u));
        for(unsigned i=0;i<idle_at_response;i++)CHECK(!idle_results[i]);
        if(!expectation->legacy)for(unsigned i=0;i<2;i++)CHECK(idle_observations[i].reason==BW_SETUP_IDLE_STOPPED&&!idle_observations[i].error&&!idle_observations[i].error_valid&&!idle_observations[i].close_failed);
        f.backend_result=2;CHECK(!catalog(s).active);
    }
    unsigned reserves=f.reserves,dispatches=f.dispatches;
    bw_native_response_clear(&out);CHECK(!bd_service_destroy(s)&&guard_clear_calls==1&&!attached_guard&&!attached_owner&&!active_log);
    printf("{\"passed\":true,\"case\":\"%s\",\"httpStatus\":%d,\"acceptedByModeledBackend\":%s,\"idleCallsAtResponse\":%u,\"observedCallsAtResponse\":%u,\"legacyCallsAtResponse\":%u,\"stockLogOpensAtResponse\":%u,\"stockLogCloses\":%u,\"firstFailingIdlePass\":%u,\"reserveCallsAtResponse\":%u,\"backendDispatchCallsAtResponse\":%u,\"actualStockParser\":true,\"actualServicePreparationBegin\":true,\"sameCallObservation\":%s,\"checks\":%u,\"consoleActions\":0,\"networkActions\":0}\n",
        case_name,expectation->accepted?202:409,expectation->accepted?"true":"false",idle_at_response,observed_at_response,legacy_at_response,opens_at_response,stock_log_closes,
        expectation->pass,reserves,dispatches,expectation->legacy?"false":"true",checks);return 0;
}

