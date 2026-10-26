/* Exercise the real asynchronous registration job with a deterministic clock. */
#include "register_job.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define C(x) do { checks++; if(!(x)){fprintf(stderr,"case %u line %u: %s\n",cases,__LINE__,#x);exit(1);} } while(0)
struct fixture { double time,ready,cancel; unsigned present,adds,scans,lists,caps;
    int conflict,unknown,nan,fail_list,expire_list,cancel_list,fail_scan,fail_add; };
static double clock_now(void *v){struct fixture *f=v;return f->nan?NAN:f->time;}
static int current(void *v){struct fixture *f=v;return f->cancel>=0&&f->time>=f->cancel;}
static enum bw_registration_state state(void *v){
    struct fixture *f=v;if(f->conflict)return BW_REG_CONFLICT;if(f->unknown)return BW_REG_UNKNOWN;
    if(!f->present)return BW_REG_ABSENT;return f->time>=f->ready?BW_REG_MATCH:BW_REG_PENDING;
}
static int post(void *v,enum bw_sm_operation op,const char *root,struct bw_sm_reply *out){
    struct fixture *f=v;C(!strcmp(root,"/data/blackwire/games/Fixture"));memset(out,0,sizeof *out);
    if(op==BW_SM_VERSION){out->capabilities=f->caps;return 0;}
    if(op==BW_SM_MANUAL_LIST){f->lists++;if(f->expire_list)f->time+=1000;
        if(f->cancel_list)f->cancel=f->time;out->present=f->present;return f->fail_list?-2:0;}
    if(op==BW_SM_MANUAL_ADD){C(++f->adds==1);if(!f->fail_add){f->present=1;out->present=1;}return f->fail_add?-2:0;}
    if(op==BW_SM_SCAN){C(++f->scans==1);return f->fail_scan?-2:0;}
    C(0);return -1;
}
static void pause_job(void *v,unsigned ms){struct fixture *f=v;C(ms==250);f->time+=(double)ms/1000;C(f->time<1000);}
static void progress(void *v,const char *message){C(v&&message&&*message);}
static struct fixture fresh(void){return (struct fixture){.time=100,.ready=101,.cancel=-1,.caps=7};}
static int run(struct fixture *f,struct bw_register_result *r){
    cases++;struct bw_register_ops ops={f,current,state,post,clock_now,pause_job,progress};
    int rc=bw_register_job("/data/blackwire/games/Fixture",&ops,r);
    C(f->adds<=1&&f->scans<=1&&r->reattach_attempted==0);return rc;
}
int main(void){
    struct fixture f=fresh();struct bw_register_result r;C(!run(&f,&r)&&r.registered&&f.adds==1&&!f.scans&&!r.cause);C(r.observations&&r.last_state==BW_REG_MATCH&&r.last_manual);
    f=fresh();f.present=1;C(!run(&f,&r)&&!f.adds&&f.scans==1);
    f=fresh();f.ready=195;C(!run(&f,&r)&&f.time>=197&&f.adds==1); /* previously expired after 60s */
    f=fresh();f.ready=250;C(!run(&f,&r)&&f.time>=252&&f.adds==1);
    f=fresh();f.ready=1000;C(run(&f,&r)&&!r.registered&&f.adds==1&&!strcmp(r.cause,"registration_deadline_expired"));C(strstr(r.message,"timeout")&&!strstr(r.message,"manual source"));
    f=fresh();f.ready=279;C(run(&f,&r)&&!strcmp(r.cause,"registration_deadline_expired")); /* no 2s retained window */
    f=fresh();f.caps=15;C(!run(&f,&r)&&r.capabilities==15);
    f=fresh();f.caps=3;C(run(&f,&r)&&!f.adds&&!f.scans&&!strcmp(r.cause,"registration_api_incompatible"));
    f=fresh();f.fail_list=1;C(run(&f,&r)&&!f.adds&&r.last_post_result==-2&&!strcmp(r.cause,"registration_manual_observation_failed"));
    f=fresh();f.expire_list=1;C(run(&f,&r)&&!f.adds&&!strcmp(r.cause,"registration_deadline_expired"));
    f=fresh();f.cancel_list=1;C(run(&f,&r)&&!f.adds&&!strcmp(r.cause,"registration_current_changed"));
    f=fresh();f.nan=1;C(run(&f,&r)&&!f.adds&&!strcmp(r.cause,"registration_clock_invalid"));
    f=fresh();f.conflict=1;C(run(&f,&r)&&!f.adds&&!f.scans&&!strcmp(r.cause,"registration_conflict"));
    f=fresh();f.unknown=1;C(run(&f,&r)&&!f.adds&&!f.scans&&!strcmp(r.cause,"registration_state_unknown"));
    f=fresh();f.cancel=170;f.ready=200;C(run(&f,&r)&&f.adds==1&&!strcmp(r.cause,"registration_current_changed"));
    f=fresh();f.present=1;f.fail_scan=1;f.ready=195;C(!run(&f,&r)&&f.scans==1&&!f.adds); /* read-only resolution, no replay */
    f=fresh();f.fail_add=1;C(run(&f,&r)&&f.adds==1&&!f.scans&&!r.registered);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualJobCompiled\":true,\"consoleContacted\":false}\n",cases,checks);return 0;
}
