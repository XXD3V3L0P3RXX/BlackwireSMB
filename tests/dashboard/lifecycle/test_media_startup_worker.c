/* Actual main.c startup scheduling/worker; platform and backend effects modeled. */
#include <time.h>
#include <pthread.h>
static int fixture_clock(clockid_t,struct timespec *);
static int fixture_sleep(const struct timespec *,struct timespec *);
static int fixture_thread(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
#define clock_gettime fixture_clock
#define nanosleep fixture_sleep
#define pthread_create fixture_thread
#define BW_LIBRARY_HOST_TEST 1
#define BW_NATIVE_BACKEND 1
#define BW_WEB_ONLY 1
#define BW_MEDIA_PUBLIC_TRIAL 1
#define main unused_resident_main
#include "../../../src/blackwire/library/main.c"
#undef main
#undef pthread_create
#undef nanosleep
#undef clock_gettime

static unsigned cases,checks,threads,tokens,reservations,releases,core_calls,current_calls;
static unsigned token_busy,idle_busy;
static int thread_failure,token_failure,release_failure,clock_failure,clock_after_current,wait_interrupted,clock_errno;
static unsigned held;
static double observed_time;
static void *(*scheduled)(void *);static void *scheduled_arg;
static void check(int good,int line){checks++;if(!good){fprintf(stderr,"Media worker case %u line %d\n",cases,line);abort();}}
#define CHECK(x) check(!!(x),__LINE__)
static void reset(void){
    CHECK(!media_trial.running);
    media_trial.reserved=media_trial.release_unknown=media_trial.automatic=media_trial.automatic_requested=0;
    memset(media_trial.nonce,0,sizeof media_trial.nonce);memset(&media_trial.record,0,sizeof media_trial.record);
    threads=tokens=reservations=releases=core_calls=current_calls=token_busy=idle_busy=held=0;
    thread_failure=token_failure=release_failure=clock_failure=clock_after_current=wait_interrupted=clock_errno=0;
    scheduled=NULL;scheduled_arg=NULL;observed_time=10;native_backend=(struct bw_native_backend *)(uintptr_t)1;cases++;
}
static int fixture_clock(clockid_t id,struct timespec *out){
    CHECK(id==CLOCK_MONOTONIC);if(clock_failure)return -1;
    out->tv_sec=(time_t)observed_time;out->tv_nsec=(long)((observed_time-out->tv_sec)*1e9);if(clock_errno)errno=EIO;return 0;
}
static int fixture_sleep(const struct timespec *delay,struct timespec *remaining){
    (void)remaining;CHECK(delay->tv_sec==0&&delay->tv_nsec==250000000L);
    if(wait_interrupted){wait_interrupted--;errno=EINTR;return -1;}observed_time+=.25;return 0;
}
static int fixture_thread(pthread_t *thread,const pthread_attr_t *attr,void *(*work)(void *),void *arg){
    (void)thread;CHECK(attr&&work==media_public_worker&&arg==&media_trial);threads++;
    if(thread_failure)return EAGAIN;scheduled=work;scheduled_arg=arg;return 0;
}
static void execute(void){CHECK(scheduled&&media_trial.running);void *(*work)(void *)=scheduled;scheduled=NULL;CHECK(work(scheduled_arg)==NULL);}
int bw_native_backend_startup_token(struct bw_native_backend *p,char out[33]){
    CHECK(p==native_backend);tokens++;memset(out,0,33);if(token_failure)return -1;if(token_busy){token_busy--;return 1;}memset(out,'a',32);return 0;
}
int bw_native_setup_nonce(char out[33]){memset(out,'b',32);out[32]=0;return 0;}
int bw_native_backend_external_reserve(struct bw_native_backend *p,const char nonce[33],const char token[33]){
    CHECK(p==native_backend&&!held&&nonce[0]=='b'&&token[0]=='a');reservations++;held=1;return 0;
}
int bw_native_backend_external_current(struct bw_native_backend *p,const char nonce[33]){
    CHECK(p==native_backend&&held&&nonce[0]=='b');current_calls++;
    if(clock_after_current&&current_calls==3)observed_time=clock_after_current<0?9:101;
    return 0;
}
int bw_native_backend_external_release_idle(struct bw_native_backend *p,const char nonce[33]){
    CHECK(p==native_backend&&held&&nonce[0]=='b');releases++;if(release_failure)return 1;held=0;return 0;
}
int bw_native_backend_external_release(struct bw_native_backend *p,const char nonce[33]){return bw_native_backend_external_release_idle(p,nonce);}
int bw_native_setup_idle(void *opaque){(void)opaque;if(idle_busy){idle_busy--;return -1;}return 0;}
int bw_startup_idle_ps5(void *opaque){struct bw_startup_idle_request *r=opaque;CHECK(r&&r->current);return r->current(r->opaque);}
int bw_native_backend_registration_pending(struct bw_native_backend *p,const char title[10]){(void)p;(void)title;return 0;}
void bw_native_sensitive_clear(void *bytes,size_t size){memset(bytes,0,size);}
int bw_media_public_run(const struct bw_media_public_request *q,struct bw_media_public_record *r){
    CHECK(q&&q->opaque==&media_trial&&held);core_calls++;
    CHECK(!q->current(q->opaque));r->phase=BW_MEDIA_PUBLIC_VERIFIED;r->registered=1;r->reason="own_row_observed";return 0;
}
const char *bw_media_public_phase_name(enum bw_media_public_phase p){return p==BW_MEDIA_PUBLIC_VERIFIED?"verified":p==BW_MEDIA_PUBLIC_REFUSED?"refused":"idle";}
const char *bw_media_public_mode_name(enum bw_media_public_mode mode){(void)mode;return "none";}
int kernel_dynlib_handle(int pid,const char *name,uint32_t *handle){(void)pid;(void)name;(void)handle;return -1;}
void *kernel_dynlib_dlsym(int pid,uint32_t h,const char *name){(void)pid;(void)h;(void)name;return NULL;}
void *kernel_dynlib_resolve(int pid,uint32_t h,const char *name){(void)pid;(void)h;(void)name;return NULL;}
int sceKernelLoadStartModule(const char *p,size_t a,const void *v,uint32_t f,void *o,int *r){(void)p;(void)a;(void)v;(void)f;(void)o;(void)r;CHECK(0);return -1;}
int sceKernelSendNotificationRequest(int d,notify_request_t *r,size_t s,int f){CHECK(!d&&r&&s==sizeof *r&&!f);return 0;}

int main(void){
    reset();media_automatic_schedule();CHECK(threads==1&&media_trial.automatic_requested&&!held);
    CHECK(!strcmp(media_setup_status(),"automatic_setup_waiting"));media_automatic_schedule();CHECK(threads==1);execute();
    CHECK(core_calls==1&&releases==1&&!held&&!media_trial.running);CHECK(!strcmp(media_setup_status(),"verified"));media_automatic_schedule();CHECK(threads==1);
    reset();token_busy=3;media_automatic_schedule();execute();CHECK(tokens==4&&reservations==1&&core_calls==1&&releases==1&&!held);
    reset();idle_busy=2;media_automatic_schedule();execute();CHECK(reservations==3&&releases==3&&core_calls==1&&!held);
    reset();token_failure=1;media_automatic_schedule();execute();CHECK(tokens==1&&!reservations&&!core_calls&&!releases&&!held);CHECK(media_trial.record.phase==BW_MEDIA_PUBLIC_REFUSED);
    reset();token_busy=999;media_automatic_schedule();execute();CHECK(!core_calls&&!reservations&&!held&&!media_trial.running);CHECK(!strcmp(media_trial.record.reason,"startup_idle_wait_expired"));
    reset();idle_busy=1;release_failure=1;media_automatic_schedule();execute();CHECK(!core_calls&&reservations==1&&releases==1&&held&&media_trial.release_unknown);CHECK(!strcmp(media_setup_status(),"ownership_held"));
    reset();clock_after_current=1;media_automatic_schedule();execute();CHECK(!core_calls&&reservations==1&&releases==1&&!held);CHECK(!strcmp(media_trial.record.reason,"startup_idle_wait_expired"));
    reset();clock_after_current=-1;media_automatic_schedule();execute();CHECK(!core_calls&&reservations==1&&releases==1&&!held);CHECK(!strcmp(media_trial.record.reason,"startup_clock_changed"));
    reset();thread_failure=1;media_automatic_schedule();CHECK(threads==1&&!scheduled&&!media_trial.running&&!reservations);CHECK(!strcmp(media_trial.record.reason,"startup_worker_start_failed"));media_automatic_schedule();CHECK(threads==1);
    reset();clock_failure=1;media_automatic_schedule();CHECK(!threads&&!reservations&&!media_trial.running);CHECK(!strcmp(media_trial.record.reason,"startup_clock_unavailable"));clock_failure=0;media_automatic_schedule();CHECK(!threads);
    reset();token_busy=1;wait_interrupted=1;clock_errno=1;media_automatic_schedule();execute();CHECK(core_calls==1&&tokens==2&&releases==1&&!held&&!media_trial.running);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualMainWorker\":true,\"platformAndBackendModeled\":true,\"publicCoreModeled\":true,\"consoleActions\":0}\n",cases,checks);
    return 0;
}
