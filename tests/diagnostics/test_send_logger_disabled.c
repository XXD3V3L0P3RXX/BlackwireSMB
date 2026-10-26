#define _POSIX_C_SOURCE 200809L
#include "send_diagnostic.h"
#include <assert.h>

static unsigned evaluated, io_calls;
static int observe(void) { evaluated++; return 17; }
int __wrap_open(const char *path, int flags, ...) { (void)path; (void)flags; io_calls++; return -1; }
ssize_t __wrap_write(int fd, const void *bytes, size_t size) { (void)fd; (void)bytes; (void)size; io_calls++; return -1; }
int __wrap_close(int fd) { (void)fd; io_calls++; return -1; }
int __wrap_fstat(int fd, struct stat *st) { (void)fd; (void)st; io_calls++; return -1; }
int __wrap_clock_gettime(clockid_t clock, struct timespec *at) { (void)clock; (void)at; io_calls++; return -1; }
pid_t __wrap_getpid(void) { io_calls++; return -1; }
int main(void) {
    errno = EACCES;
    bw_send_log("release-private-event", "release-private-format=%d", observe());
    assert(errno == EACCES);
    assert(evaluated == 0);
    assert(io_calls == 0);
    return 0;
}
