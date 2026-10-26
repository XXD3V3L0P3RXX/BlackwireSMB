#define _DEFAULT_SOURCE 1
#include "native_setup.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
static int idle(void *p){(void)p;return 0;}
static void write_file(const char *p,const void *data,size_t n){int fd=open(p,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);assert(write(fd,data,n)==(ssize_t)n);assert(!close(fd));}
int main(int argc,char **argv){
    assert(argc==2);char conf[1024],target[1024];assert(snprintf(conf,sizeof conf,"%s/share.conf",argv[1])>0);assert(snprintf(target,sizeof target,"%s/original",argv[1])>0);
    struct bw_share_config c={0},loaded={0};struct bw_setup_generation generation={0},after={0};struct bw_setup_saved saved={0};unsigned cases=0;
    assert(bw_native_setup_read(argv[1],&loaded,&generation)==BW_SETUP_MISSING);cases++;
    strcpy(c.server,"192.0.2.10");strcpy(c.share,"FixtureLibrary");strcpy(c.user,"project-account");strcpy(c.password,"fixture-password");
    assert(bw_native_setup_store(argv[1],&c,&generation,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",idle,NULL,&saved)==BW_SETUP_SAVE_OK&&saved.published&&saved.verified);
    assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_VALID);assert(!strcmp(loaded.password,c.password));cases++;
    assert(!rename(conf,target));assert(!symlink(target,conf));assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_UNSAFE);assert(!loaded.password[0]);assert(!unlink(conf));cases++;
    assert(!link(target,conf));assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_UNSAFE);assert(!unlink(conf));cases++;
    write_file(conf,"not a valid settings document\n",30);assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_INVALID);assert(!unlink(conf));cases++;
    char large[4097];memset(large,'x',sizeof large);write_file(conf,large,sizeof large);assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_UNSAFE);assert(!unlink(conf));cases++;
    assert(!rename(target,conf));assert(bw_native_setup_read(argv[1],&loaded,&after)==BW_SETUP_VALID&&!strcmp(loaded.password,c.password));cases++;
    int fd=open(BW_SEND_LOG_PATH,O_RDONLY);assert(fd>=0);char log[16384]={0};assert(read(fd,log,sizeof log-1)>0);assert(!close(fd));
    assert(strstr(log,"event=settings-read-failure"));assert(strstr(log,"stage=config-open"));assert(strstr(log,"stage=config-metadata"));assert(strstr(log,"stage=config-parse"));
    assert(!strstr(log,"fixture-password")&&!strstr(log,"project-account")&&!strstr(log,"192.0.2.10"));
    printf("{\"passed\":true,\"cases\":%u,\"freshSaveAndReadBack\":true,\"unsafeFilesStillRefused\":true,\"diagnosticsExcludeCredentials\":true,\"originalSettingsPreserved\":true}\n",cases);return 0;
}
