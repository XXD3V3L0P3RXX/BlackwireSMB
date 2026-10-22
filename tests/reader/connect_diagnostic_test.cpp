/* Exact extracted source connect + worker entry. Calls are modeled, not a
 * native allocator, socket, server, credential or game-heap witness. */
#include "reader/preload/source.h"
#include "streamreader/cache.h"
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned checks,cases,guards,caches,contexts,options,shares,ntreads,clears,disconnects,logs,borrowed_caches,source_closes,scratch_allocations,performance_snapshots;
static unsigned guard_failure,source_observes;
static int cache_result,cache_errno,share_result,ntstatus;
static bool context_failure,scratch_failure,maximum_performance;
static size_t max_marker;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"connect-diagnostic line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Config{char server[2],share[2],user[2],password[2];};
struct Cache{unsigned placeholder;};
struct Smb{unsigned placeholder;};
struct bp_source{
    int failed,ready;bp_current_fn current;void *opaque;Config config;Cache cache;Smb *smb;
    bp_connect_diagnostic connect_diagnostic;
    void *scratch;
};
static int guard(void*){guards++;errno=EBUSY;return guards==guard_failure?-17:0;}
static int bw_cache_init(Cache*){caches++;errno=cache_errno;return cache_result;}
static int bw_cache_init_with_storage(Cache*,void*,size_t);
static Smb *smb2_init_context(){contexts++;errno=ENOBUFS;static Smb instance;return context_failure?nullptr:&instance;}
static void smb2_set_timeout(Smb*,int n){CHECK(n==10);options++;}
static void smb2_set_user(Smb*,const char*){options++;}
static void smb2_set_password(Smb*,const char*){options++;}
constexpr unsigned SMB2_NEGOTIATE_SIGNING_ENABLED=1;
constexpr unsigned SMB2_NEGOTIATE_SIGNING_REQUIRED=2;
static void smb2_set_sign(Smb*,int n){CHECK(n==1);options++;}
static void smb2_set_security_mode(Smb*,unsigned n){CHECK(n==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED));options++;}
static int smb2_connect_share(Smb*,const char*,const char*,const char*){shares++;errno=ECONNREFUSED;return share_result;}
static int smb2_get_nterror(Smb*){ntreads++;CHECK(errno==ECONNREFUSED);return ntstatus;}
static void bw_share_clear(Config *p){clears++;std::memset(p,0,sizeof *p);errno=ERANGE;}
static void *scratch_malloc(size_t bytes){scratch_allocations++;CHECK(bytes==BP_COALESCE_BYTES);errno=ENOMEM;return scratch_failure?nullptr:std::malloc(bytes);}
int bp_source_disconnect(bp_source *p){disconnects++;std::free(p->scratch);p->scratch=nullptr;p->smb=nullptr;p->ready=0;p->failed=1;errno=EIO;return 0;}
#define malloc scratch_malloc
#include "connect-source-under-test.h"
#undef malloc
static std::atomic<unsigned> status;
static std::atomic<bool> metadata_ready,finished,canceled;
static std::atomic<unsigned> metadata_users;
static bp_source instance,*source=&instance;
constexpr size_t SCE_KERNEL_PAGE_SIZE=16384;
#include "cache-backing-under-test.h"
static int bw_cache_init_with_storage(Cache*,void *storage,size_t bytes){CHECK(storage==cache_storage&&bytes==BW_CACHE_BYTES);caches++;borrowed_caches++;errno=cache_errno;return cache_result;}
int bp_source_close(bp_source *p){CHECK(!cache_owner.load()&&!metadata_users.load()&&finished.load());source_closes++;if(p){std::free(p->scratch);p->scratch=nullptr;}return 0;}
void bp_source_performance(const bp_source *p,struct bp_source_performance *out){CHECK(p==source&&out);performance_snapshots++;std::memset(out,maximum_performance?0xff:0,sizeof *out);}
static int current(void *p){return guard(p);}
static char marker[512];
namespace sce::Ampr::Emu {static void kernelDebugLogf(const char *format,...){
    logs++;va_list args;va_start(args,format);int n=std::vsnprintf(marker,sizeof marker,format,args);va_end(args);
    CHECK(n>0&&(size_t)n<sizeof marker);if((size_t)n>max_marker)max_marker=(size_t)n;
    CHECK((size_t)n+sizeof("[AMPR_EMU] ")+1<512);errno=EINVAL;
}}
/* This connection fixture models registration, not the progress-log callback's
 * transport events. Require the exact worker callback and current source. */
static void bp_runtime_source_progress(void*,const struct bp_source_progress*,const struct bp_source_performance*){}
void bp_source_observe(bp_source *p,bp_source_progress_fn callback,void *opaque){
    CHECK(p==source&&callback==bp_runtime_source_progress&&!opaque);source_observes++;
}
#include "connect-worker-under-test.h"
#include "performance-report-under-test.h"
#include "cache-release-under-test.h"
static void reset(){
    std::free(instance.scratch);
    guards=caches=contexts=options=shares=ntreads=clears=disconnects=logs=guard_failure=borrowed_caches=source_closes=0;
    scratch_allocations=performance_snapshots=source_observes=0;scratch_failure=maximum_performance=false;
    cache_result=share_result=ntstatus=0;cache_errno=ENOMEM;context_failure=false;instance={};instance.current=guard;source=&instance;
    status=2;metadata_ready=true;finished=true;metadata_users=0;canceled=false;cache_owner=nullptr;errno=0;marker[0]=0;
}
static bp_connect_diagnostic diagnostic(){bp_connect_diagnostic d{};bp_source_connect_diagnostic(source,&d);return d;}
int main(){
    reset();cases++;CHECK(bp_source_connect(source)==0);auto d=diagnostic();CHECK(d.stage==0&&d.options_returned==5&&!d.ntstatus_valid);CHECK(guards==2&&caches==1&&contexts==1&&shares==1&&!ntreads&&!disconnects&&clears==1);
    reset();cases++;cache_result=-7;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_CACHE&&d.raw==-7&&d.error==ENOMEM&&!d.options_returned&&!d.ntstatus_valid);CHECK(caches==1&&!contexts&&!shares&&disconnects==1&&errno==EIO);CHECK(bp_source_connect(source)==-1);auto retained=diagnostic();CHECK(!std::memcmp(&d,&retained,sizeof d)&&caches==1);
    reset();cases++;context_failure=true;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_CONTEXT&&d.raw==0&&d.error==ENOBUFS&&!d.options_returned);CHECK(caches==1&&contexts==1&&!shares&&disconnects==1);
    reset();cases++;scratch_failure=true;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_SCRATCH&&d.raw==0&&d.error==ENOMEM&&scratch_allocations==1&&caches==1&&!contexts&&!shares&&disconnects==1&&!source->scratch);
    reset();cases++;share_result=-113;ntstatus=(int)0xc000006du;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_SHARE&&d.raw==-113&&d.error==ECONNREFUSED&&d.options_returned==5&&d.ntstatus_valid&&d.ntstatus==0xc000006du);CHECK(guards==1&&shares==1&&ntreads==1&&disconnects==1&&errno==EIO);
    reset();cases++;guard_failure=1;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_INITIAL_CURRENT&&d.raw==-17&&d.error==EBUSY);CHECK(guards==1&&!caches&&!contexts&&!shares&&!disconnects&&clears==1);
    reset();cases++;guard_failure=2;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_FINAL_CURRENT&&d.raw==-17&&d.error==EBUSY&&d.options_returned==5);CHECK(guards==2&&shares==1&&!ntreads&&disconnects==1);
    reset();cases++;instance.ready=1;CHECK(bp_source_connect(source)==-1);d=diagnostic();CHECK(d.stage==BP_CONNECT_STATE&&d.raw==2&&d.error==0&&!guards&&!caches&&!shares);
    reset();cases++;share_result=9;CHECK(bp_source_connect(source)==0);CHECK(!diagnostic().stage&&guards==2&&!ntreads); // existing <0 policy is unchanged
    reset();cases++;CHECK(bp_worker_initialize());CHECK(status==4&&guards==4&&logs==1&&errno==EBUSY);CHECK(std::strstr(marker,"ready=1 connectStage=0")&&std::strstr(marker,"optionsReturned=5")&&std::strstr(marker,"workerGuardStage=0 workerGuardRaw=0 workerGuardErrno=0"));CHECK(!bp_worker_initialize()&&logs==1);
    reset();cases++;guard_failure=1;CHECK(!bp_worker_initialize());CHECK(status==5&&guards==1&&!caches&&!shares&&logs==1&&errno==EBUSY);CHECK(std::strstr(marker,"initAttempted=0 result=-")&&std::strstr(marker,"workerGuardStage=1 workerGuardRaw=-17"));
    reset();cases++;share_result=-113;CHECK(!bp_worker_initialize());CHECK(status==5&&guards==2&&shares==1&&ntreads==1&&logs==1&&errno==EIO);CHECK(std::strstr(marker,"connectStage=5 connectRaw=-113")&&std::strstr(marker,"connectNtStatus=0 connectNtStatusValid=1"));
    reset();cases++;guard_failure=4;CHECK(!bp_worker_initialize());CHECK(status==5&&guards==4&&shares==1&&logs==1&&errno==EBUSY);CHECK(std::strstr(marker,"initAttempted=1 result=0 ready=0 connectStage=0")&&std::strstr(marker,"workerGuardStage=2 workerGuardRaw=-17"));
    reset();cases++;cache_result=INT_MIN;CHECK(!bp_worker_initialize());CHECK(std::strstr(marker,"connectRaw=-2147483648"));
    reset();cases++;cache_result=-2;cache_errno=0;CHECK(!bp_worker_initialize());d=diagnostic();CHECK(d.stage==BP_CONNECT_CACHE&&d.raw==-2&&d.error==0&&borrowed_caches==1&&!contexts&&!shares&&!options&&cache_owner.load()==source);CHECK(bp_runtime_release()==-1&&source_closes==0);bp_worker_finish();CHECK(!cache_owner.load()&&finished.load());CHECK(bp_runtime_release()==0&&source_closes==1&&source==nullptr);
    reset();cases++;CHECK(bp_worker_initialize());CHECK(cache_owner.load()==source&&borrowed_caches==1&&!finished.load());CHECK(bp_runtime_release()==-1&&source_closes==0);bp_worker_finish();CHECK(disconnects==1&&!cache_owner.load()&&finished.load());metadata_users=1;CHECK(bp_runtime_release()==-1&&source_closes==0);metadata_users=0;CHECK(bp_runtime_release()==0&&source_closes==1&&!source);
    reset();cases++;CHECK(bp_worker_initialize());bp_source *first=source;bp_source next{};next.current=guard;source=&next;status=2;unsigned before=caches;CHECK(!bp_worker_initialize());CHECK(cache_owner.load()==first&&caches==before&&shares==1&&std::strstr(marker,"result=-16 ready=0"));CHECK(bp_runtime_release()==-1&&source_closes==0);source=first;bp_worker_finish();CHECK(!cache_owner.load());
    reset();cases++;maximum_performance=true;errno=EBUSY;bp_runtime_report_performance();CHECK(performance_snapshots==1&&logs==2&&errno==EBUSY&&std::strstr(marker,"clockFaults=18446744073709551615"));
    reset();cases++;CHECK(bp_worker_initialize());CHECK(scratch_allocations==1&&source->scratch);bp_worker_finish();CHECK(performance_snapshots==1&&logs==3&&!source->scratch&&disconnects==1&&!cache_owner.load());
    reset();cases++;CHECK(bp_worker_initialize());CHECK(source_observes==1);bp_worker_finish();
    reset();
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualConnectAndWorker\":true,\"callsModeled\":true,\"maximumObservedMarkerBytes\":%zu,\"nativeHeapOrTransportProved\":false}\n",cases,checks,max_marker);
}
