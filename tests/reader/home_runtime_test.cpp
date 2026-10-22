/* Actual Input/home admission/metadata prepare/failure path, modeled SDK/FS. */
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include "library/reader_selection.h"
#include "reader/preload/home_client.h"
}
static unsigned cases,checks,scenario,claims,hashes,notifications,exits,logs,sleeps,opened,closed,admitted;
static unsigned claim_logs,getters;
static uint64_t tick;static bool claimed,changed;static std::atomic<bool> canceled{false};
static const char accepted_run[]="1234567890abcdef1234567890abcdef";
static const char other_run[]="abcdef1234567890abcdef1234567890";
static char selection_bytes[4097];static size_t selection_size;
static const char map_bytes[]="full-manifest-fixture";
static const char share_bytes[]="share-fixture";
static unsigned fd_kind[128];static bool fd_live[128];static unsigned next_fd;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"home-runtime case%u line%d: %s\n",scenario,__LINE__,#x);std::exit(1);}}while(0)
enum {PLAY,PLAY_WAIT,HOME,PARTIAL,PARTIAL_DENIED,WRONG_MODE,IO_ERROR,CLAIM_REFUSED,NEVER_PUBLISHED,WRONG_RUN,MAP_CHANGED,NO_EXIT,NO_NOTICE,INPUT_CLOSE_ERROR,CANCELED_HOME,NAMED_CLOSE_ERROR,LAST_SCENARIO};
struct bp_source {} fixture_source;static bp_source *source;
static std::atomic<unsigned> status{0};static std::atomic<bool> metadata_ready{false};static std::atomic<bp_source*> cache_owner{nullptr};
static int current(void*){return canceled.load()?-1:0;}
static int kind(const char *path){if(!std::strcmp(path,"/app0/blackwire.files"))return 1;if(std::strstr(path,".selection"))return 2;if(std::strstr(path,".share"))return 3;CHECK(false);return 0;}
static int fake_open(const char *path,int flags,...){
    CHECK(flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));unsigned k=(unsigned)kind(path);
    if(k!=1){
        if(scenario==IO_ERROR){errno=EIO;return -1;}
        if(scenario==PARTIAL||scenario==PARTIAL_DENIED){if(k==2){errno=ENOENT;return -1;}if(scenario==PARTIAL_DENIED){errno=EACCES;return -1;}}
        if(scenario==PLAY_WAIT&&sleeps<2){errno=EACCES;return -1;}
        if(scenario!=PLAY&&scenario!=PLAY_WAIT&&scenario!=WRONG_MODE&&scenario!=PARTIAL&&scenario!=PARTIAL_DENIED){
            if(!claimed||scenario==NEVER_PUBLISHED){errno=ENOENT;return -1;}
            if(sleeps<2){errno=k==3||sleeps?EACCES:ENOENT;return -1;}
        }
    }
    unsigned fd=++next_fd;CHECK(fd<128);fd_kind[fd]=k;fd_live[fd]=true;opened++;return (int)fd;
}
static int fake_stat(int fd,struct stat *st){
    CHECK(fd>0&&fd<128&&fd_live[fd]);unsigned k=fd_kind[fd];*st={};st->st_dev=7;st->st_ino=k+(changed&&k==1?100:0);st->st_nlink=1;st->st_uid=20;
    st->st_mode=S_IFREG|((k==1||scenario==WRONG_MODE)?0644:0600);
    st->st_size=(off_t)(k==1?sizeof map_bytes-1:k==2?selection_size:sizeof share_bytes-1);st->st_mtim.tv_sec=10;st->st_ctim.tv_sec=11;return 0;
}
static ssize_t fake_pread(int fd,void *out,size_t size,off_t at){
    CHECK(fd>0&&fd<128&&fd_live[fd]&&at==0);unsigned k=fd_kind[fd];const void *body=k==1?(const void*)map_bytes:k==2?(const void*)selection_bytes:(const void*)share_bytes;
    size_t expected=k==1?sizeof map_bytes-1:k==2?selection_size:sizeof share_bytes-1;CHECK(size==expected);std::memcpy(out,body,size);return (ssize_t)size;
}
static int fake_close(int fd){CHECK(fd>0&&fd<128&&fd_live[fd]);fd_live[fd]=false;closed++;if(claimed&&fd_kind[fd]==1&&(scenario==INPUT_CLOSE_ERROR||(scenario==NAMED_CLOSE_ERROR&&closed==3))){errno=EIO;return -1;}return 0;}
static int fake_clock_gettime(clockid_t which,struct timespec *st){CHECK(which==CLOCK_MONOTONIC);st->tv_sec=(time_t)(tick/1000000u);st->tv_nsec=(long)(tick%1000000u)*1000;return 0;}
static int fake_usleep(useconds_t delay){CHECK(delay==20000);sleeps++;tick+=delay;return 0;}
static pid_t fake_getpid(){return 234;}
static int fake_notice(int device,void *raw,size_t size,int flags){
    CHECK(device==0&&flags==0&&size==3120&&raw);notifications++;const char *message=(const char*)raw+45;
    CHECK(std::strstr(message,"Blackwire")&&(!std::strstr(message,"password")));return 0;
}
static void fake_exit(int code){CHECK(code==0);exits++;}
static void *amprResolveLibkernelFunction(const char *name){
    if(!std::strcmp(name,"_exit"))return scenario==NO_EXIT?nullptr:reinterpret_cast<void*>(fake_exit);
    if(!std::strcmp(name,"sceKernelSendNotificationRequest"))return scenario==NO_NOTICE?nullptr:reinterpret_cast<void*>(fake_notice);
    CHECK(false);return nullptr;
}
namespace sce::Ampr::Emu {static void kernelDebugLogf(const char *format,...){
    logs++;
    if(std::strstr(format,"[BWREAD_HOME_CLIENT1]")){
        claim_logs++;va_list args;va_start(args,format);
        CHECK(va_arg(args,unsigned)==BW_HC_NONCE);CHECK(va_arg(args,unsigned)==1);
        CHECK(va_arg(args,long long)==-1);CHECK(va_arg(args,int)==EPERM);CHECK(va_arg(args,unsigned)==0);
        va_end(args);CHECK(!std::strstr(format,"nonce")&&!std::strstr(format,"password"));errno=ERANGE;
    }
}}
struct Ops {int(*open)(const char*,int,...);int(*stat)(int,struct stat*);ssize_t(*pread)(int,void*,size_t,off_t);int(*close)(int);}ops{};
enum {kAmprLibkernelHook_open,kAmprLibkernelHook_fstat,kAmprLibkernelHook_pread,kAmprLibkernelHook_close};
template<class T>static T ampr_fixed_kernel_slot(int id){switch(id){case kAmprLibkernelHook_open:return reinterpret_cast<T>(fake_open);case kAmprLibkernelHook_fstat:return reinterpret_cast<T>(fake_stat);case kAmprLibkernelHook_pread:return reinterpret_cast<T>(fake_pread);default:return reinterpret_cast<T>(fake_close);}}
extern "C" void bw_home_client_map_sha256(const void *raw,size_t size,uint8_t out[32]){CHECK(size==sizeof map_bytes-1&&!std::memcmp(raw,map_bytes,size));hashes++;std::memset(out,0x42,32);}
extern "C" int bw_home_client_claim(int32_t pid,const uint8_t hash[32],uint64_t deadline,char run[33]){
    claims++;CHECK(claims==1&&pid==234&&hash[0]==0x42&&deadline==tick+20000000u);claimed=true;
    if(scenario==CLAIM_REFUSED){errno=EIO;return -1;}
    if(scenario==MAP_CHANGED)changed=true;
    if(scenario==CANCELED_HOME)canceled.store(true);
    std::memcpy(run,accepted_run,33);return 0;
}
static int bp_source_admit(bp_source **out,const void*,size_t,const void*,size_t,const void*,size_t,int(*guard)(void*),void *opaque){CHECK(!guard(opaque));admitted++;*out=&fixture_source;return 0;}
extern "C" void bw_home_client_diagnostic(struct bw_home_client_failure *out){getters++;*out={BW_HC_NONCE,1,-1,EPERM,0};}
static int bp_source_close(bp_source*){return 0;}
#define clock_gettime fake_clock_gettime
#define usleep fake_usleep
#define getpid fake_getpid
#include "input-diagnostic-under-test.h"
#include "home-admission-under-test.h"
#include "metadata-helpers-under-test.h"
#include "metadata-prepare-under-test.h"
#include "home-failure-under-test.h"
#undef clock_gettime
#undef usleep
#undef getpid
static void reset(){
    claims=hashes=notifications=exits=logs=sleeps=opened=closed=admitted=claim_logs=getters=0;next_fd=3;tick=1000000u;claimed=changed=false;canceled.store(false);
    std::memset(fd_kind,0,sizeof fd_kind);std::memset(fd_live,0,sizeof fd_live);home_start={};home_notice=nullptr;home_exit=nullptr;private_wait={};init_failure={};
    source=nullptr;status.store(0);metadata_ready.store(false);cache_owner.store(nullptr);
    struct bw_reader_selection selected{};std::strcpy(selected.source,"Fixture");std::strcpy(selected.title,"PPSA02325");
    std::memset(selected.binding_hex,'a',64);std::memset(selected.prepared_record_hex,'b',64);std::snprintf(selected.game,sizeof selected.game,"PPSA02325_%s",selected.binding_hex);
    std::strcpy(selected.run,scenario==WRONG_RUN?other_run:accepted_run);selected.source_kind=BW_PROVIDER_FOLDER;selected.source_policy=1;selected.eboot_size=32;selected.manifest_count=2;selected.manifest_hash=123;
    CHECK(!bw_reader_selection_serialize(&selected,selection_bytes,sizeof selection_bytes,&selection_size));
}
int main(){
    for(scenario=0;scenario<LAST_SCENARIO;scenario++){
        reset();cases++;bool result=bp_metadata_prepare();bool success=scenario==PLAY||scenario==PLAY_WAIT||scenario==HOME;
        CHECK(result==success);CHECK(opened==closed);CHECK((admitted==1)==success);
        bool home=scenario!=PLAY&&scenario!=PLAY_WAIT&&scenario!=PARTIAL&&scenario!=PARTIAL_DENIED&&scenario!=WRONG_MODE&&scenario!=IO_ERROR;
        CHECK(home_start.active==home);CHECK(claims<=1);
        if(scenario==CLAIM_REFUSED){CHECK(init_failure.operation==19&&init_failure.error==0);CHECK(claim_logs==1&&getters==1&&errno==EIO);CHECK(!bp_metadata_prepare()&&claim_logs==1&&claims==1);}
        else CHECK(!claim_logs&&!getters);
        if(success){CHECK(metadata_ready.load());CHECK(bp_metadata_prepare()&&claims==(scenario==HOME?1u:0u));}
        if(!home)CHECK(!claims&&!hashes&&!exits);
        if(scenario==PLAY_WAIT)CHECK(sleeps==2&&!home_start.active&&private_wait.started);
        if(scenario==HOME)CHECK(claims==1&&hashes==1&&sleeps==2&&home_start.inputs_released);
        if(scenario==NEVER_PUBLISHED)CHECK(tick==21000000u&&claims==1&&init_failure.operation==16);
        if(scenario==WRONG_RUN)CHECK(init_failure.operation==20&&!admitted);
        if(scenario==MAP_CHANGED)CHECK(init_failure.operation==9&&!admitted);
        if(!success){CHECK(bp_home_startup_failure(0)==(home?-1:0));CHECK(exits==(home&&scenario!=NO_EXIT&&scenario!=INPUT_CLOSE_ERROR&&scenario!=NAMED_CLOSE_ERROR?1u:0u));}
    }
    scenario=HOME;reset();cases++;home_start.active=true;home_start.inputs_released=true;home_notice=fake_notice;home_exit=fake_exit;
    CHECK(bp_home_startup_failure(-1)==-1&&notifications==1&&!exits);
    home_start.inputs_released=false;CHECK(bp_home_startup_failure(0)==-1&&!exits);
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualRuntimeAdmission\":true,\"actualSelectionCodec\":true,\"nativeExitProved\":false}\n",cases,checks);return 0;
}
