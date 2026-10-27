/* Exercise the actual main.c trace section; no console or network peer. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned checks, send_calls;
static int recording, last_flags, last_fd;
static size_t last_size;
static void check(int ok, int line) {
    checks++;
    if (!ok) { fprintf(stderr, "trace check failed at line %d\n", line); exit(1); }
}
#define CHECK(x) check(!!(x), __LINE__)
ssize_t __real_send(int, const void *, size_t, int);
ssize_t __wrap_send(int fd, const void *bytes, size_t size, int flags) {
    if (recording) { send_calls++; last_fd=fd; last_size=size; last_flags=flags; }
    return __real_send(fd, bytes, size, flags);
}

#if defined(BW_STARTUP_TRACE) && (!defined(BW_STARTUP_TRACE_STDOUT_ONLY) || !BW_STARTUP_TRACE_STDOUT_ONLY)
enum file_fault { FILE_SHORT=1, FILE_PARTIAL_ENOSPC, FILE_PARTIAL_ZERO,
    FILE_PARTIAL_EINTR, FILE_EINTR, FILE_ZERO, FILE_OVERLONG, FILE_CLOSE_ERROR };
static int file_fault, file_watched=-1;
static unsigned file_writes, file_closes, file_syncs;
/* Fault only the actual production helper's selected file descriptor. A
 * partial write really reaches the file before the subsequent refusal. */
static ssize_t trace_file_write(int fd, const void *bytes, size_t size) {
    if (fd!=file_watched) return write(fd,bytes,size);
    file_writes++;
    if (file_writes==1 && (file_fault==FILE_SHORT || file_fault==FILE_PARTIAL_ENOSPC ||
        file_fault==FILE_PARTIAL_ZERO || file_fault==FILE_PARTIAL_EINTR || file_fault==FILE_CLOSE_ERROR))
        return write(fd,bytes,size>7?7:size);
    if (file_fault==FILE_SHORT) return write(fd,bytes,size);
    if (file_fault==FILE_ZERO || file_fault==FILE_PARTIAL_ZERO) return 0;
    if (file_fault==FILE_EINTR || file_fault==FILE_PARTIAL_EINTR) { errno=EINTR;return -1; }
    if (file_fault==FILE_OVERLONG) return (ssize_t)size+1;
    errno=ENOSPC;return -1;
}
static int trace_file_close(int fd) {
    if (fd!=file_watched) return close(fd);
    file_closes++;int rc=close(fd);
    if (file_fault==FILE_CLOSE_ERROR) { errno=EIO;return -1; }
    return rc;
}
static int trace_file_fsync(int fd) {
    if (fd==file_watched) file_syncs++;
    return fsync(fd);
}
#define write trace_file_write
#define close trace_file_close
#define fsync trace_file_fsync
#endif

/* Any accidental trace filesystem call in stdout-only mode must fail the
 * link, even if a test directory would otherwise hide its attempted write. */
#if defined(BW_STARTUP_TRACE_STDOUT_ONLY) && BW_STARTUP_TRACE_STDOUT_ONLY
extern int trace_filesystem_open_forbidden(const char *, int, ...);
extern int trace_filesystem_close_forbidden(int);
extern ssize_t trace_filesystem_write_forbidden(int, const void *, size_t);
extern int trace_filesystem_fstat_forbidden(int, struct stat *);
extern int trace_filesystem_lstat_forbidden(const char *, struct stat *);
extern int trace_filesystem_fsync_forbidden(int);
#define open trace_filesystem_open_forbidden
#define close trace_filesystem_close_forbidden
#define write trace_filesystem_write_forbidden
#define fstat trace_filesystem_fstat_forbidden
#define lstat trace_filesystem_lstat_forbidden
#define fsync trace_filesystem_fsync_forbidden
#endif
#include "startup-trace-under-test.h"
#undef open
#undef close
#undef write
#undef fstat
#undef lstat
#undef fsync

static int redirect_stdout(int replacement) {
    int old=dup(STDOUT_FILENO);
    CHECK(old>=0 && dup2(replacement, STDOUT_FILENO)==STDOUT_FILENO);
    return old;
}
static void restore_stdout(int old) {
    recording=0;
    CHECK(dup2(old, STDOUT_FILENO)==STDOUT_FILENO && close(old)==0);
}
static void reset_trace(void) {
#ifdef BW_STARTUP_TRACE
    startup_trace_rows=0;
#endif
    send_calls=0;last_flags=last_fd=0;last_size=0;
}
static void mark(const char *phase, int expected_calls) {
    (void)phase; /* The disabled trace macro intentionally consumes no input. */
    errno=EDOM;recording=1;startup_mark(phase);recording=0;
    CHECK(errno==EDOM && send_calls==(unsigned)expected_calls);
    if (expected_calls) CHECK(last_fd==STDOUT_FILENO && last_size>0 && last_size<160 && last_flags==(MSG_DONTWAIT|MSG_NOSIGNAL));
}
static size_t collect(int fd, char *bytes, size_t capacity) {
    size_t used=0;
    for (;;) {
        ssize_t n=recv(fd, bytes+used, capacity-1-used, MSG_DONTWAIT);
        if (n<0) { CHECK(errno==EAGAIN || errno==EWOULDBLOCK);break; }
        if (!n) break;
        used+=(size_t)n;CHECK(used<capacity-1);
    }
    bytes[used]=0;return used;
}
static double seconds(void) {
    struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}

int main(int argc, char **argv) {
    CHECK(argc==2);reset_trace();
    if (!strcmp(argv[1],"off")) {
        errno=E2BIG;startup_trace_open();startup_mark("off");startup_trace_finish();
        CHECK(errno==E2BIG && !send_calls);
    } else if (!strcmp(argv[1],"files")) {
#if defined(BW_STARTUP_TRACE) && !BW_STARTUP_TRACE_STDOUT_ONLY
        int pipefd[2];CHECK(!pipe(pipefd));int old=redirect_stdout(pipefd[1]);close(pipefd[1]);
        errno=E2BIG;startup_trace_open();CHECK(errno==E2BIG && startup_trace_fd>=0);
        mark("file.compatibility",1);errno=ERANGE;startup_trace_finish();CHECK(errno==ERANGE && startup_trace_fd==-1);restore_stdout(old);close(pipefd[0]);
        char path[1024],data[512];snprintf(path,sizeof path,"%s/startup-core-01-%ld.log",BW_STARTUP_TRACE_DIRECTORY,(long)getpid());
        FILE *f=fopen(path,"rb");CHECK(f!=NULL);size_t n=fread(data,1,sizeof data-1,f);data[n]=0;CHECK(!fclose(f));
        CHECK(n>0 && strstr(data,"row=1 file.compatibility\n")!=NULL);
        struct stat st;CHECK(!stat(path,&st) && (st.st_mode&0777)==0600);
#else
        CHECK(0);
#endif
    } else if (!strncmp(argv[1],"file-",5)) {
#if defined(BW_STARTUP_TRACE) && !BW_STARTUP_TRACE_STDOUT_ONLY
        const char *names[]={"file-short","file-partial-enospc","file-partial-zero",
            "file-partial-eintr","file-eintr","file-zero","file-overlong","file-close-error"};
        for(unsigned i=0;i<sizeof names/sizeof names[0];i++)if(!strcmp(argv[1],names[i]))file_fault=(int)i+1;
        CHECK(file_fault>=FILE_SHORT && file_fault<=FILE_CLOSE_ERROR);
        int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));int old=redirect_stdout(pair[0]);close(pair[0]);
        errno=E2BIG;startup_trace_open();CHECK(errno==E2BIG && startup_trace_fd>=0);file_watched=startup_trace_fd;
        mark("file.first",1);
        int healthy=file_fault==FILE_SHORT;
        unsigned first_writes=file_fault==FILE_PARTIAL_EINTR||file_fault==FILE_EINTR?4u:
            file_fault==FILE_ZERO||file_fault==FILE_OVERLONG?1u:2u;
        CHECK(file_writes==first_writes && file_closes==(healthy?0u:1u) && file_syncs==(healthy?1u:0u));
        CHECK(healthy?startup_trace_fd==file_watched:startup_trace_fd==-1);
        char path[1024];snprintf(path,sizeof path,"%s/startup-core-01-%ld.log",BW_STARTUP_TRACE_DIRECTORY,(long)getpid());
        struct stat before,after;CHECK(!stat(path,&before));
        off_t partial=file_fault==FILE_EINTR||file_fault==FILE_ZERO||file_fault==FILE_OVERLONG?0:7;
        CHECK(healthy?before.st_size>7:before.st_size==partial);
        mark("file.after",2);CHECK(!stat(path,&after));
        CHECK(file_writes==first_writes+(healthy?1u:0u) && file_closes==(healthy?0u:1u));
        CHECK(healthy?after.st_size>before.st_size:after.st_size==before.st_size);
        CHECK(file_syncs==(healthy?2u:0u));
        errno=ERANGE;startup_trace_finish();CHECK(errno==ERANGE && startup_trace_fd==-1 && file_closes==1);
        errno=E2BIG;startup_trace_finish();CHECK(errno==E2BIG && file_closes==1);file_watched=-1;
        restore_stdout(old);char output[512];size_t output_size=collect(pair[1],output,sizeof output);close(pair[1]);
        CHECK(output_size>0 && strstr(output,"row=1 file.first\n") && strstr(output,"row=2 file.after\n"));
        char data[512];FILE *f=fopen(path,"rb");CHECK(f!=NULL);size_t n=fread(data,1,sizeof data-1,f);data[n]=0;CHECK(!fclose(f));
        if(healthy)CHECK(strstr(data,"row=1 file.first\n") && strstr(data,"row=2 file.after\n"));
        else CHECK(n==(size_t)partial && (!n || !memcmp(data,"BW core",7)));
#else
        CHECK(0);
#endif
    } else {
#if defined(BW_STARTUP_TRACE) && BW_STARTUP_TRACE_STDOUT_ONLY
        errno=E2BIG;startup_trace_open();startup_trace_finish();CHECK(errno==E2BIG);
        if (!strcmp(argv[1],"stream") || !strcmp(argv[1],"limit") || !strcmp(argv[1],"long")) {
            int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));int old=redirect_stdout(pair[0]);close(pair[0]);
            if (!strcmp(argv[1],"stream")) mark("stream.first",1);
            else if (!strcmp(argv[1],"long")) {char phase[400];memset(phase,'x',sizeof phase-1);phase[sizeof phase-1]=0;mark(phase,0);CHECK(startup_trace_rows==1);}
            else {for(unsigned i=0;i<40;i++) mark("bounded.row",i<32?(int)i+1:32);CHECK(startup_trace_rows==32);}
            restore_stdout(old);char data[8192];size_t n=collect(pair[1],data,sizeof data);close(pair[1]);
            if (!strcmp(argv[1],"long")) CHECK(n==0);
            else {unsigned rows=0;size_t span=0;for(size_t i=0;i<n;i++){span++;if(data[i]=='\n'){CHECK(span<160);rows++;span=0;}}
                CHECK(!span && rows==(!strcmp(argv[1],"stream")?1u:32u));CHECK(n<32u*160u);CHECK(strstr(data,"BW core-isolation-01 pid=")!=NULL);}
        } else if (!strcmp(argv[1],"closed-peer")) {
            int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));int old=redirect_stdout(pair[0]);close(pair[0]);close(pair[1]);mark("closed.peer",1);restore_stdout(old);
        } else if (!strcmp(argv[1],"closed-fd")) {
            int old=dup(STDOUT_FILENO);CHECK(old>=0 && !close(STDOUT_FILENO));mark("closed.fd",1);restore_stdout(old);
        } else if (!strcmp(argv[1],"non-socket")) {
            int pipefd[2];CHECK(!pipe(pipefd));int old=redirect_stdout(pipefd[1]);close(pipefd[1]);mark("non.socket",1);restore_stdout(old);char byte;CHECK(read(pipefd[0],&byte,1)==0);close(pipefd[0]);
        } else if (!strcmp(argv[1],"backpressure")) {
            int pair[2],size=1024;CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));CHECK(!setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&size,sizeof size));
            char fill[512]={0};size_t total=0;for(;;){ssize_t n=send(pair[0],fill,sizeof fill,MSG_DONTWAIT|MSG_NOSIGNAL);if(n<0){CHECK(errno==EAGAIN || errno==EWOULDBLOCK);break;}CHECK(n>0);total+=(size_t)n;CHECK(total<=262144);}
            int old=redirect_stdout(pair[0]);close(pair[0]);double begin=seconds();alarm(3);mark("backpressure",1);alarm(0);CHECK(seconds()-begin<2.0);restore_stdout(old);close(pair[1]);
        } else CHECK(0);
#else
        CHECK(0);
#endif
    }
    printf("{\"passed\":true,\"case\":\"%s\",\"checks\":%u,\"consoleActions\":0}\n",argv[1],checks);
    return 0;
}
