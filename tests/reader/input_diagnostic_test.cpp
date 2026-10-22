/* Actual generated Input/diagnostic code with modeled saved-original calls.
 * No console/filesystem access or native permission claim. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include "init-phases-under-test.h"
static unsigned checks,cases,opens,stats,reads,closes,issued,clocks,sleeps;
static unsigned fail_open,fail_stat,fail_close;
static int open_error=EACCES;
static bool cancel_read,eof_read,wrong_named,deny_all,fail_clock,fail_sleep,bad_clock,reverse_clock,cancel_wait,oversleep;
static uint64_t now_us;
static struct stat sample;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"input-diagnostic line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static int fake_open(const char*,int flags,...){CHECK(flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));opens++;if(deny_all||opens==fail_open){errno=open_error;return -1;}return (int)++issued+10;}
static int fake_clock_gettime(clockid_t id,struct timespec *out){CHECK(id==CLOCK_MONOTONIC);clocks++;if(fail_clock){errno=EIO;return -1;}out->tv_sec=bad_clock?-1:(time_t)(now_us/1000000u);out->tv_nsec=(long)(now_us%1000000u)*1000;return 0;}
static int fake_usleep(useconds_t delay){CHECK(delay==20000);sleeps++;if(fail_sleep){errno=EINTR;return -1;}now_us=reverse_clock?now_us-1:now_us+(oversleep?10000001u:delay);if(cancel_wait)cancel_read=true;return 0;}
static int fake_stat(int fd,struct stat *out){stats++;if(stats==fail_stat){errno=EIO;return -1;}*out=sample;if(wrong_named&&fd==12)out->st_ino++;errno=EACCES;return 0;}
static ssize_t fake_read(int fd,void *out,size_t n,off_t at){CHECK(fd==11&&at==0&&n==5);reads++;if(eof_read){errno=0;return 0;}std::memcpy(out,"input",5);return 5;}
static int fake_close(int fd){CHECK(fd==11||fd==12);closes++;if(closes==fail_close){errno=EIO;return -1;}return 0;}
struct Ops {int(*open)(const char*,int,...);int(*stat)(int,struct stat*);ssize_t(*pread)(int,void*,size_t,off_t);int(*close)(int);}ops{fake_open,fake_stat,fake_read,fake_close};
static int current(void*){return cancel_read?-1:0;}
#define clock_gettime fake_clock_gettime
#define usleep fake_usleep
#include "input-diagnostic-under-test.h"
#undef clock_gettime
#undef usleep
static unsigned logs;
namespace sce::Ampr::Emu {static void kernelDebugLogf(const char* format,...){CHECK(std::strstr(format,"[BWREAD_INIT1]")!=nullptr);logs++;errno=ERANGE;}}
#include "init-report-under-test.h"
static void reset(){opens=stats=reads=closes=fail_open=fail_stat=fail_close=issued=clocks=sleeps=0;open_error=EACCES;cancel_read=eof_read=wrong_named=deny_all=fail_clock=fail_sleep=bad_clock=reverse_clock=cancel_wait=oversleep=false;now_us=1000000;private_wait={};sample={};sample.st_mode=S_IFREG|0600;sample.st_size=5;sample.st_nlink=0;sample.st_uid=0;sample.st_dev=3;sample.st_ino=7;sample.st_mtim.tv_sec=100;sample.st_ctim.tv_sec=101;init_failure={};}
int main(){
    reset();cases++;{Input input(1);CHECK(input.load("fixed",4096,0600));CHECK(opens==2&&stats==3&&reads==1&&closes==1&&!init_failure.operation);CHECK(input.clear()&&closes==2);}
    reset();cases++;deny_all=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.input==2&&init_failure.operation==1&&init_failure.result==-1&&init_failure.error==EACCES);CHECK(input.clear()&&!stats&&!reads&&!closes);CHECK(sleeps==500&&opens==500&&private_wait.last-private_wait.begin==10000000u);}
    reset();cases++;fail_open=1;{Input input(2);CHECK(input.load("fixed",4096,0600));CHECK(opens==3&&sleeps==1&&!init_failure.operation);CHECK(input.clear()&&closes==2);}
    reset();cases++;fail_open=1;{Input input(1);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==1&&init_failure.error==EACCES&&!clocks&&!sleeps&&opens==1);CHECK(input.clear());}
    reset();cases++;fail_open=1;open_error=ENOENT;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==1&&init_failure.error==ENOENT&&!clocks&&!sleeps&&opens==1);CHECK(input.clear());}
    reset();cases++;fail_open=1;fail_clock=true;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==14&&init_failure.result==-1&&init_failure.error==EIO&&!sleeps);CHECK(input.clear());}
    reset();cases++;fail_open=1;bad_clock=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==16&&init_failure.result==0&&init_failure.error==0&&!sleeps);CHECK(input.clear());}
    reset();cases++;fail_open=1;fail_sleep=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==15&&init_failure.result==-1&&init_failure.error==EINTR&&opens==1);CHECK(input.clear());}
    reset();cases++;deny_all=true;reverse_clock=true;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==16&&opens==1&&sleeps==1);CHECK(input.clear());}
    reset();cases++;fail_open=1;cancel_wait=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==5&&opens==1&&sleeps==1&&!stats);CHECK(input.clear());}
    reset();cases++;now_us=11000000;private_wait={true,now_us-10000000u,now_us};deny_all=true;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==16&&init_failure.field==2&&opens==0&&!sleeps);CHECK(input.clear());}
    reset();cases++;fail_open=1;oversleep=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==1&&init_failure.result==-1&&init_failure.error==EACCES&&opens==1&&sleeps==1);CHECK(input.clear()&&!issued&&!closes);}
    reset();cases++;fail_open=2;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==7&&init_failure.error==EACCES&&!sleeps&&!clocks);CHECK(input.clear()&&closes==1);}
    reset();cases++;fail_stat=1;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.input==3&&init_failure.operation==2&&init_failure.result==-1&&init_failure.error==EIO);CHECK(input.clear()&&closes==1&&!reads);}
    reset();cases++;sample.st_mode=S_IFREG|0644;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==3&&init_failure.field==4&&init_failure.actual==0644&&init_failure.expected==0600&&init_failure.error==0);CHECK(input.clear()&&stats==1&&!reads);}
    reset();cases++;eof_read=true;{Input input(1);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==6&&init_failure.result==0&&init_failure.error==0&&init_failure.expected==5);CHECK(input.clear()&&stats==1&&reads==1);}
    reset();cases++;wrong_named=true;fail_close=1;{Input input(3);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.input==3&&init_failure.operation==9&&init_failure.field==11&&init_failure.actual==8&&init_failure.expected==7&&init_failure.error==0);CHECK(input.clear()&&closes==2&&stats==3);}
    reset();cases++;cancel_read=true;{Input input(2);CHECK(!input.load("fixed",4096,0600));CHECK(init_failure.operation==5&&init_failure.error==0&&!reads);CHECK(input.clear()&&stats==1);}
    reset();cases++;{Input input(1);CHECK(input.load("fixed",4096,0600));fail_close=2;CHECK(!input.clear());CHECK(init_failure.operation==11&&init_failure.result==-1&&init_failure.error==EIO);}
    cases++;for(unsigned i=0;i<9;i++){errno=EBUSY;bp_init_report(BP_INIT_ENTER,0);CHECK(errno==EBUSY);}CHECK(logs==7);
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualInputAndReporter\":true,\"savedOriginalCallsModeled\":true,\"maximumPhaseAttempts\":%u,\"privateWaitBudgetUs\":10000000,\"nativePermissionProved\":false}\n",cases,checks,logs);return 0;
}
