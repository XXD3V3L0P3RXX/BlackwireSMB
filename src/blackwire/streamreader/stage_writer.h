#ifndef BW_STAGE_WRITER_H
#define BW_STAGE_WRITER_H
#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>

#define BW_STAGE_VECTOR_MAX 8u
#define BW_STAGE_PIECE_MAX 65536u

/* POSIX-style synchronous callback: bytes written, or -1 with errno set.
 * It must finish consuming buffers before returning. No asynchronous writes. */
typedef ssize_t (*bw_stage_pwritev_fn)(void *opaque,int fd,const struct iovec *iov,
                                     int count,uint64_t offset);
/* Exact bounded write to an already verified/owned fixed-size staging file.
 * Copies <=8 iovecs on stack, never changes input iovecs/data, never allocates.
 * Every piece<=64KiB; source buffers must stay valid/stable until return.
 * offset/limit<=INT64_MAX and the complete range must fit limit before any
 * callback. Zero-length pieces/empty vector are a no-op. Does not inspect or
 * resize fd: caller verifies its identity/size and owns all writes to it.
 * Short writes advance private iovecs+offset; EINTR retries without advancing.
 * Zero/oversized/invalid callback results fail EIO. Failure can leave a written
 * prefix: do not submit the failed native read or reuse it as valid data. */
int bw_stage_writev_exact(int fd,const struct iovec *iov,unsigned count,
                          uint64_t offset,uint64_t limit,
                          bw_stage_pwritev_fn writer,void *opaque);
/* Convert a raw amd64 syscall result with captured carry to POSIX. A positive
 * raw errno must never be interpreted as a successful short byte count. */
ssize_t bw_stage_syscall_result(uint64_t raw,int carry);
#endif
