#ifndef BW_STAGE_WRITER_PS5_H
#define BW_STAGE_WRITER_PS5_H
#include "stage_writer.h"
#include <sys/syscall.h>

/* SDK pwritev(fd,iov,count,off_t), SYS290 on PS5 amd64. Capture carry so
 * positive raw FreeBSD errno cannot masquerade as successful byte count.
 * Shared verbatim by the reader candidate and its owned scratch-file probe. */
_Static_assert(SYS_pwritev==290,"inspected PS5 pwritev syscall");
_Static_assert(sizeof(off_t)==8,"amd64 pwritev offset ABI");
static inline ssize_t bw_stage_pwritev_ps5(void *opaque,int fd,const struct iovec *iov,
                                          int count,uint64_t offset) {
    (void)opaque;
    register uint64_t r10 __asm__("r10")=offset;
    uint64_t raw; unsigned char carry;
    __asm__ __volatile__("syscall" : "=a"(raw),"=@ccc"(carry),"+r"(r10)
        : "a"((uint64_t)SYS_pwritev),"D"((long)fd),"S"(iov),"d"((long)count)
        : "rcx","r11","memory");
    return bw_stage_syscall_result(raw,carry);
}
#endif
