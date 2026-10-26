/* Actual dashboard worker failure body; Sony/process and failed-admission
 * boundaries are recording models. No source, bundle or launch may run. */
#include "worker-release-under-test.h"
#include <stdio.h>
static struct bw_native_backend owner;
static unsigned checks,cases,open_calls,empty_closes,process_queries,session_releases,retired,failures,phases;
static int open_result;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"worker case%u line%u: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
static int process(void *opaque,int32_t pid,struct bw_stop_identity *out){C(opaque==&owner&&pid==500&&out&&open_calls==1);process_queries++;return 0;}
void bw_native_session_ps5_ops(struct bw_session_ops *out){C(out);memset(out,0,sizeof *out);out->opaque=&owner;out->process=process;}
static void home_phase(struct home_worker *h,unsigned phase,const char *message){C(h&&h->backend==&owner&&message&&(phase==1||phase==2||phase==8));phases++;}
static int home_lease(void *opaque){struct home_worker *h=opaque;C(h&&h->backend==&owner&&owner.active);return 0;}
static int home_select(struct home_worker *h,struct bw_share_config *config){
    C(h&&config&&h->backend==&owner&&!open_calls);h->session.game.bound=1;h->session.game.process=h->birth;h->session.game.app_id=23;strcpy(h->session.game.title,"PPSA99999");return 0;
}
static int home_startup_control(struct home_worker *h,const struct bw_reader_selection *selected){C(h&&h->backend==&owner&&!selected&&!open_calls);return 0;}
static int home_source(struct home_worker *h,const struct bw_share_config *c,const struct bw_reader_selection *s){(void)h;(void)c;(void)s;abort();}
static void play_failure_locked(struct bw_native_backend *p,const char *message){C(p==&owner&&message);failures++;}
static void home_consumed_retire_locked(struct bw_native_backend *p,const struct bw_stop_identity *birth){C(p==&owner&&birth->pid==500);retired++;}
int bw_native_launch_admission_open(struct bw_native_launch_admission **out,const char *base,const struct bw_provider_binding *identity,const struct bw_native_prepared *prepared,const char *run,int (*guard)(void *),void *opaque,const char **error){
    C(out&&!*out&&base&&identity&&prepared&&run&&guard==home_lease&&!guard(opaque)&&error);open_calls++;*error="Startup admission failed.";return open_result;
}
int bw_native_launch_admission_close(struct bw_native_launch_admission **out){C(out&&!*out);empty_closes++;return 0;}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *s){(void)p;(void)s;abort();}
int bw_native_launch_admission_map_sha256(struct bw_native_launch_admission *p,uint8_t bytes[32]){(void)p;(void)bytes;abort();}
int bw_native_launch_admission_source_ampr(struct bw_native_launch_admission *p,const struct bw_preload_source_ampr **out){(void)p;(void)out;abort();}
int bw_preload_bundle_verify_source_checked(const struct bw_preload_bundle_request *q,const struct bw_preload_source_ampr *w,struct bw_preload_bundle_result *out){(void)q;(void)w;(void)out;abort();}
int bw_home_session_publish(struct bw_home_session *s,const char *root,const struct bw_reader_selection *selected,const struct bw_share_config *c){(void)s;(void)root;(void)selected;(void)c;abort();}
int bw_home_session_close(struct bw_home_session *s,const char run[33]){(void)s;(void)run;abort();}
int bw_home_session_exited(void *opaque){struct bw_home_session *s=opaque;C(s&&s->game.bound&&process_queries>=2&&!s->publication_attempted);return 0;}
int bw_home_session_release(struct bw_home_session *s,double deadline){C(s&&s->game.bound&&deadline>now()&&!s->private_inputs.initialized&&process_queries>=2);session_releases++;return 0;}
int main(void){
    for(int mode=1;mode<=2;mode++){
        cases++;memset(&owner,0,sizeof owner);C(!pthread_mutex_init(&owner.lock,NULL));owner.active=owner.home.active=1;owner.worker_kind=WORK_PLAY;
        strcpy(owner.playing.selection.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");owner.home.claim.pid=500;owner.home.claim.deadline_us=(uint64_t)((now()+10)*1000000);owner.home.birth.pid=500;
        owner.home.retained=calloc(1,sizeof(struct home_worker));C(owner.home.retained);open_result=-mode;open_calls=empty_closes=process_queries=session_releases=retired=failures=phases=0;
        C(!home_worker(&owner));C(!owner.active&&!owner.home.active&&open_calls==1&&empty_closes==1&&process_queries==2&&session_releases==1&&failures==1&&phases==3);
        C(owner.control_release_unknown==(mode==2));C(!strcmp(owner.playing.phase,mode==2?"unknown":"error"));
        if(mode==2){C(owner.home.retained&&!retired);free(owner.home.retained);owner.home.retained=NULL;}else C(!owner.home.retained&&retired==1);
        C(!pthread_mutex_destroy(&owner.lock));
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualDashboardWorkerBody\":true,\"ordinaryRefusalUnchanged\":true,\"failedOpenReleaseUnknownRetained\":true,\"noSourceBundleOrLaunch\":true,\"consoleActions\":0}\n",cases,checks);return 0;
}

