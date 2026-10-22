/* kinfo_proc is a public BSD interface. Select the SDK's default BSD namespace
 * in this translation unit; a caller's POSIX-only flags hide its own typedefs. */
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#include "startup_runtime_idle.h"
#include <ps5/kernel.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
/* Same read-only sysctl ABI as existing native_session_ps5.c, and the public
 * PM process-record layout. A changed SDK/profile refuses at build/runtime. */
_Static_assert(SYS___sysctl==202,"read-only sysctl ABI");
_Static_assert(offsetof(struct kinfo_proc,ki_structsize)==0,"process size ABI");
_Static_assert(offsetof(struct kinfo_proc,ki_pid)==72&&sizeof(((struct kinfo_proc *)0)->ki_pid)==4,"process PID ABI");
_Static_assert(offsetof(struct kinfo_proc,ki_comm)==447&&sizeof(((struct kinfo_proc *)0)->ki_comm)==20,"process name ABI");
_Static_assert(offsetof(struct kinfo_proc,ki_rssize)==264&&sizeof(((struct kinfo_proc *)0)->ki_rssize)==8,"process RSS ABI");
_Static_assert(offsetof(struct kinfo_proc,ki_start)==336,"process birth ABI");
static uint32_t firmware(void *opaque){(void)opaque;return kernel_get_fw_version();}
static struct bw_src_process_result query(void *opaque,void *buffer,size_t capacity){
    (void)opaque;size_t size=capacity;int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
    register uint64_t r10 __asm__("r10")=(uintptr_t)&size,r8 __asm__("r8")=0,r9 __asm__("r9")=0;uint64_t value;unsigned char carry;
    __asm__ __volatile__("syscall" : "=a"(value),"=@ccc"(carry),"+r"(r10),"+r"(r8),"+r"(r9)
        : "a"((uint64_t)SYS___sysctl),"D"((uintptr_t)mib),"S"((uint64_t)4),"d"((uintptr_t)buffer) : "rcx","r11","memory");
    return (struct bw_src_process_result){value,carry,size};
}
int bw_startup_idle_ps5(void *opaque){static const struct bw_src_idle_ops ops={firmware,query,NULL};return bw_src_idle_evaluate(opaque,&ops);}

#include "idle_log_cache.h"
int bw_startup_idle_ps5_cached(const struct bw_startup_idle_request *r,struct bw_idle_log_cache *cache,const char operation[33]){
    static const struct bw_src_idle_ops ops={firmware,query,NULL};return bw_src_idle_evaluate_cached(r,&ops,cache,operation);
}

int bw_startup_adopt_ps5(const struct bw_startup_idle_request *r,const struct bw_stop_identity *game,const char title[10]){
    static const struct bw_src_idle_ops ops={firmware,query,NULL};return bw_src_adopt_evaluate(r,&ops,game,title);
}
