/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real local container/status observations. Only complete SMP HTTP snapshots
 * and injected POSIX failures are modeled; no mutation API may be called. */
#define _POSIX_C_SOURCE 200809L
#include "native_cleanup_remote.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks,opens,closes,manuals,fences,injected;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"CHECK failed at %d: %s\n",__LINE__,#x);exit(2);}}while(0)
static char image[PATH_MAX],status_path[PATH_MAX],status_parent[PATH_MAX],scenario[80],fd_path[4096][PATH_MAX];
static int remember(int fd){if(fd>=0){CHECK(fd<4096);char proc[64];snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);ssize_t n=readlink(proc,fd_path[fd],PATH_MAX-1);CHECK(n>0);fd_path[fd][n]=0;opens++;}return fd;}
int nrc_test_open(const char *p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=(mode_t)va_arg(ap,int);va_end(ap);}return remember(open(p,flags,mode));}
int nrc_test_openat(int dir,const char *p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=(mode_t)va_arg(ap,int);va_end(ap);}return remember(openat(dir,p,flags,mode));}
int nrc_test_close(int fd){
    int fail=fd>=0&&fd<4096&&!injected&&((!strcmp(scenario,"image-close")&&!strcmp(fd_path[fd],image))||
        (!strcmp(scenario,"status-close")&&!strcmp(fd_path[fd],status_path))||
        (!strcmp(scenario,"status-parent-close")&&!strcmp(fd_path[fd],status_parent)));
    int rc=close(fd);CHECK(!rc);closes++;if(fd>=0&&fd<4096)fd_path[fd][0]=0;
    if(fail){injected++;errno=EIO;return -1;}return rc;
}
int nrc_test_fstatat(int dir,const char *p,struct stat *s,int flags){return fstatat(dir,p,s,flags);}
ssize_t nrc_test_write(int fd,const void *p,size_t n){return write(fd,p,n);}
int nrc_test_fsync(int fd){return fsync(fd);}
static void append_byte(const char *path){int fd=open(path,O_WRONLY|O_APPEND);CHECK(fd>=0);CHECK(write(fd,"x",1)==1);CHECK(!close(fd));}
ssize_t nrc_test_pread(int fd,void *p,size_t n,off_t at){
    int is_image=fd>=0&&fd<4096&&!strcmp(fd_path[fd],image),is_status=fd>=0&&fd<4096&&!strcmp(fd_path[fd],status_path);
    if(is_image&&!strcmp(scenario,"image-eof")){injected++;return 0;}
    if(is_image&&!strcmp(scenario,"image-eintr")&&!injected){injected++;errno=EINTR;return -1;}
    ssize_t got=pread(fd,p,n,at);
    if(!injected&&((is_image&&!strcmp(scenario,"image-drift"))||(is_status&&!strcmp(scenario,"status-drift")))){
        injected++;append_byte(is_image?image:status_path);
    }
    if(!injected&&is_status&&!strcmp(scenario,"image-during-status")){injected++;append_byte(image);}
    return got;
}
static int current(void *opaque){CHECK(opaque==image);fences++;return !strcmp(scenario,"authority-lost")&&fences>10?-1:0;}
static void doc(struct bw_sm_document *out,const char *text){out->size=strlen(text);out->bytes=malloc(out->size);CHECK(out->bytes);memcpy(out->bytes,text,out->size);}
void bw_sm_document_clear(struct bw_sm_document *p){free(p->bytes);memset(p,0,sizeof *p);}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *out){
    CHECK(c&&!c->current(c->opaque)&&kind==BW_SM_VERSION&&!strcmp(root,"/"));memset(out,0,sizeof *out);out->uninstall_supported=1;return 0;
}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    CHECK(c&&!c->current(c->opaque));
    if(kind==BW_SM_OBSERVE_GAMES){doc(out,"{\"status\":0,\"count\":0,\"games\":[]}");return 0;}
    CHECK(kind==BW_SM_OBSERVE_MANUAL_PATHS);manuals++;
    if(manuals==2&&!strcmp(scenario,"status-between-snapshots")){
        int fd=open(status_path,O_WRONLY|O_APPEND);CHECK(fd>=0);CHECK(write(fd,"\n",1)==1);CHECK(!close(fd));
    }
    if(manuals==2&&!strcmp(scenario,"image-between-snapshots"))append_byte(image);
    char text[PATH_MAX+128];CHECK(snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[\"%s\"]}",image)>0);doc(out,text);return 0;
}
int bw_sm_uninstall(const struct bw_sm_connection *c,const char title[10],struct bw_sm_uninstall_reply *out){(void)c;(void)title;(void)out;CHECK(0);return -1;}
int bw_sm_unmount(const struct bw_sm_connection *c,const char title[10],struct bw_sm_unmount_reply *out){(void)c;(void)title;(void)out;CHECK(0);return -1;}
int main(int argc,char **argv){
    CHECK(argc==6&&strlen(argv[1])<sizeof image&&strlen(argv[2])<sizeof status_path&&strlen(argv[3])<sizeof scenario);
    strcpy(image,argv[1]);strcpy(status_path,argv[2]);strcpy(scenario,argv[3]);
    int expected=atoi(argv[4]),release=atoi(argv[5]);strcpy(status_parent,status_path);char *slash=strrchr(status_parent,'/');CHECK(slash&&slash!=status_parent);*slash=0;
    struct bw_nrc_subject subject={0};strcpy(subject.title,"PPSA90000");strcpy(subject.content,"NEUTRAL-SELECTED");strcpy(subject.source,"/data/blackwire/games/neutral-selected-source");
    if(!strcmp(scenario,"source-overlap")){strcpy(subject.source,image);char *part=strrchr(subject.source,'/');CHECK(part&&part!=subject.source);*part=0;}
    struct bw_nrc_remote remote={.connection={.current=current,.opaque=image},.fixture_manual_status_path=status_path};
    int rc=bw_nrc_remote_sources(&remote,&subject);CHECK((rc==0)==!expected);CHECK(remote.observation_release_unknown==(unsigned)release);CHECK(opens==closes);
    printf("{\"passed\":true,\"checks\":%u,\"refused\":%s,\"releaseUnknown\":%s,\"localReadBytes\":%llu,\"manualSnapshots\":%u,\"injections\":%u,\"substage\":\"%s\",\"consoleActions\":0}\n",checks,rc?"true":"false",remote.observation_release_unknown?"true":"false",(unsigned long long)remote.local_observation_bytes,manuals,injected,remote.substage?remote.substage:"");
    return 0;
}
