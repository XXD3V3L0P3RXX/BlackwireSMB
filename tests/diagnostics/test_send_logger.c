#define _POSIX_C_SOURCE 200809L
#define BW_SEND_LOG_PATH "/tmp/blackwire-send-log-test/log"
#include "send_diagnostic.h"
#include <assert.h>
#include <string.h>
int main(void){
    assert(!mkdir("/tmp/blackwire-send-log-test",0700));
    errno=ENOENT;bw_send_log("directory-failure","stage=reader-upgrade-material path=/data/test errno=%d\n",ENOENT);assert(errno==ENOENT);
    int fd=open(BW_SEND_LOG_PATH,O_RDONLY);assert(fd>=0);char bytes[4096]={0};ssize_t n=read(fd,bytes,sizeof bytes-1);assert(n>0);close(fd);
    assert(strstr(bytes,"stage=reader-upgrade-material path=/data/test errno=2"));assert(bytes[n-1]=='\n');assert(strchr(bytes,'\n')==bytes+n-1);
    fd=open(BW_SEND_LOG_PATH,O_WRONLY);assert(fd>=0);assert(!ftruncate(fd,BW_SEND_LOG_LIMIT));close(fd);
    errno=EACCES;bw_send_log("cap","must not append");assert(errno==EACCES);struct stat st;assert(!stat(BW_SEND_LOG_PATH,&st));assert(st.st_size==BW_SEND_LOG_LIMIT);
    assert(!rename(BW_SEND_LOG_PATH,"/tmp/blackwire-send-log-test/target"));
    assert(!symlink("/tmp/blackwire-send-log-test/target",BW_SEND_LOG_PATH));errno=ERANGE;bw_send_log("symlink","must not follow");assert(errno==ERANGE);assert(!stat("/tmp/blackwire-send-log-test/target",&st));assert(st.st_size==BW_SEND_LOG_LIMIT);
    assert(!unlink(BW_SEND_LOG_PATH));assert(!link("/tmp/blackwire-send-log-test/target",BW_SEND_LOG_PATH));
    fd=open(BW_SEND_LOG_PATH,O_WRONLY);assert(fd>=0);assert(!ftruncate(fd,0));close(fd);bw_send_log("hardlink","must not write");assert(!stat(BW_SEND_LOG_PATH,&st));assert(!st.st_size);
    assert(!unlink(BW_SEND_LOG_PATH));assert(!unlink("/tmp/blackwire-send-log-test/target"));
    assert(!mkfifo(BW_SEND_LOG_PATH,0600));bw_send_log("fifo","must not block");assert(!unlink(BW_SEND_LOG_PATH));
    assert(!rmdir("/tmp/blackwire-send-log-test"));
    errno=EINVAL;bw_send_log("unavailable","missing parent must be ignored");assert(errno==EINVAL);
    puts("Send diagnostic logger checks passed.");return 0;
}
