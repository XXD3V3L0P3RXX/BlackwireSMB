#include "native_session.h"
#include "native_process_result.h"
#include <ps5/kernel.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t,char *);
int sceLncUtilKillApp(uint32_t);

static int running(void *opaque,uint32_t *out){
    (void)opaque;int result=sceLncUtilGetAppIdOfRunningBigApp();if(result<=0)return -1;*out=(uint32_t)result;return 0;
}
static int title(void *opaque,uint32_t app,char out[16]){
    (void)opaque;char bytes[128]={0};int result=sceLncUtilGetAppTitleId(app,bytes);
    if(result)return -1;unsigned n=0;while(n<sizeof bytes&&bytes[n])n++;
    if(n!=9)return -1;for(unsigned i=0;i<=n;i++)out[i]=bytes[i];return 0;
}
static int process(void *opaque,int32_t pid,struct bw_stop_identity *out){
    (void)opaque;if(pid<=1||pid==getpid()||kernel_get_fw_version()!=BW_STOP_PROFILE_FW)return -1;
    unsigned char bytes[16384];size_t size=sizeof bytes;int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PID,pid};
    _Static_assert(SYS___sysctl==202,"Native read-only sysctl ABI");
    register uint64_t r10 __asm__("r10")=(uintptr_t)&size,r8 __asm__("r8")=0,r9 __asm__("r9")=0;
    uint64_t value;unsigned char carry;
    __asm__ __volatile__("syscall" : "=a"(value),"=@ccc"(carry),"+r"(r10),"+r"(r8),"+r"(r9)
        : "a"((uint64_t)SYS___sysctl),"D"((uintptr_t)mib),"S"((uint64_t)4),"d"((uintptr_t)bytes) : "rcx","r11","memory");
    int present=bw_native_process_result(value,carry,size,sizeof bytes);if(present!=1)return present;
    return bw_stop_identity_parse(bytes,size,BW_STOP_PROFILE_FW,pid,out)?-1:1;
}
static int64_t wall(void *opaque){
    (void)opaque;struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t)||t.tv_sec<=0||t.tv_sec>INT64_MAX/1000000||t.tv_nsec<0||t.tv_nsec>=1000000000L)return 0;
    int64_t sec=(int64_t)t.tv_sec*1000000;if(t.tv_nsec/1000>INT64_MAX-sec)return 0;return sec+t.tv_nsec/1000;
}
static int close_owned(void *opaque,uint32_t app){(void)opaque;return sceLncUtilKillApp(app);}
void bw_native_session_ps5_ops(struct bw_session_ops *out){*out=(struct bw_session_ops){NULL,running,title,process,wall,close_owned};}
