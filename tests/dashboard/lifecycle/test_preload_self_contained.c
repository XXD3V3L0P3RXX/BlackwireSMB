#define _DEFAULT_SOURCE 1
#include "preload_bundle.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,guards;static int denied;
static int current(void *p){CHECK(p==&guards);guards++;return denied?-1:0;}
static double now(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static void path(char out[4096],const char *root,const char *leaf){int n=snprintf(out,4096,"%s/%s",root,leaf);CHECK(n>0&&n<4096);}
static int exists(const char *path){struct stat s;return !lstat(path,&s);}
static void bytes(const char *name,const void *data,size_t size){int fd=open(name,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0755);CHECK(fd>=0);CHECK(write(fd,data,size)==(ssize_t)size);CHECK(!close(fd));}
static unsigned entries(const char *name){DIR *d=opendir(name);CHECK(d);unsigned n=0;struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;CHECK(!closedir(d));return n;}
int main(int argc,char **argv){
 CHECK(argc==3);const char *base=argv[1];int baseline=!strcmp(argv[2],"baseline");
 char root[4096],folder[4096],reader[4096],sentinel[4096];path(root,base,"fresh-game");CHECK(!mkdir(root,0755));
 path(sentinel,root,"startup.bin");const char original[]="already verified game startup";bytes(sentinel,original,sizeof original);
 path(folder,root,"fakelib2");path(reader,folder,"libSceAmpr.sprx");
 struct bw_preload_bundle_request q={root,current,&guards,now()+120};struct bw_preload_bundle_result d={0};
 int r=bw_preload_bundle_install_checked(&q,&d);
 if(baseline){CHECK(r==-1&&!d.mutation_started&&!exists(folder));CHECK(d.reason);printf("{\"passed\":true,\"beforeRefusedMissingGlobal\":true,\"reason\":\"%s\",\"checks\":%u}\n",d.reason,checks);return 0;}
 if(r)fprintf(stderr,"result%d stage%s reason%s error%d\n",r,d.stage?d.stage:"",d.reason?d.reason:"",d.system_error);
 CHECK(!r&&d.mutation_started&&!d.reason&&!strcmp(d.stage,"complete"));CHECK(entries(folder)==1);
 struct stat before,after;CHECK(!lstat(reader,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1&&(before.st_mode&07777)==0755);
 CHECK((uint64_t)before.st_size==bw_preload_embedded_image.size);
 uint8_t *body=malloc(bw_preload_embedded_image.size);CHECK(body);int fd=open(reader,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);
 CHECK(read(fd,body,bw_preload_embedded_image.size)==(ssize_t)bw_preload_embedded_image.size&&!memcmp(body,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size));CHECK(!close(fd));free(body);
 CHECK(!bw_preload_bundle_verify_checked(&q,&d)&&!d.mutation_started&&!d.reason);CHECK(!lstat(reader,&after)&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino);
 CHECK(!bw_preload_bundle_install_checked(&q,&d)&&!d.mutation_started);CHECK(entries(folder)==1);
 fd=open(reader,O_RDWR|O_NOFOLLOW);CHECK(fd>=0);uint8_t b=0;CHECK(pread(fd,&b,1,0)==1);b^=1;CHECK(pwrite(fd,&b,1,0)==1&&!close(fd));
 CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_hash_differs"));
 /* A symlinked game-library directory must not be traversed or overwritten. */
 path(root,base,"linked-game");CHECK(!mkdir(root,0755));path(folder,root,"fakelib");CHECK(!symlink(base,folder));q.root=root;
 CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started);path(folder,root,"fakelib2");CHECK(!exists(folder));
 path(root,base,"foreign-companion-game");CHECK(!mkdir(root,0755));path(folder,root,"fakelib");CHECK(!mkdir(folder,0755));path(reader,folder,"foreign-backport.sprx");bytes(reader,"preserve",8);q.root=root;
 CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started);path(folder,root,"fakelib2");CHECK(!exists(folder)&&exists(reader));
 path(root,base,"expired-game");CHECK(!mkdir(root,0755));q.root=root;q.deadline=now()-1;
 CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started);
 fd=open(sentinel,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);char saved[sizeof original];CHECK(read(fd,saved,sizeof saved)==sizeof saved&&!memcmp(saved,original,sizeof original)&&!close(fd));
 printf("{\"passed\":true,\"readerOnlyCreatedAndVerified\":true,\"tamperingRefused\":true,\"foreignBackportsPreserved\":true,\"checks\":%u,\"guards\":%u,\"consoleActions\":0}\n",checks,guards);
 return 0;
}
