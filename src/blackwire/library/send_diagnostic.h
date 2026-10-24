#ifndef BW_SEND_DIAGNOSTIC_H
#define BW_SEND_DIAGNOSTIC_H
/* Private diagnostic build: bounded best-effort logging, no authority or retry changes. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef BW_SEND_LOG_PATH
#define BW_SEND_LOG_PATH "/data/blackwire/send-diagnostic.log"
#endif
#define BW_SEND_LOG_LIMIT (1024u*1024u)
static inline void bw_send_log(const char *event,const char *format,...) __attribute__((format(printf,2,3)));
static inline void bw_send_log(const char *event,const char *format,...) {
    int saved=errno;char line[2048];struct timespec at={0};
    (void)clock_gettime(CLOCK_MONOTONIC,&at);
    int prefix=snprintf(line,sizeof line,"sendlog.1 t=%lld.%03ld pid=%ld event=%s ",
        (long long)at.tv_sec,at.tv_nsec/1000000,(long)getpid(),event);
    if(prefix<0||(size_t)prefix>=sizeof line-2){errno=saved;return;}
    va_list ap;va_start(ap,format);int n=vsnprintf(line+prefix,sizeof line-(size_t)prefix-1,format,ap);va_end(ap);
    if(n<0){errno=saved;return;}
    size_t used=(size_t)prefix+(size_t)n;if(used>sizeof line-2)used=sizeof line-2;
    for(size_t i=0;i<used;i++)if((unsigned char)line[i]<32)line[i]=' ';
    line[used++]='\n';
    int fd=open(BW_SEND_LOG_PATH,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd>=0){
        struct stat st;
        if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==geteuid()&&st.st_size>=0&&
           (unsigned long long)st.st_size+used<=BW_SEND_LOG_LIMIT)(void)write(fd,line,used);
        (void)close(fd);
    }
    errno=saved;
}
#endif
