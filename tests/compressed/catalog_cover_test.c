/* Actual catalog and compressed-icon reads with a read-only local SMB model. */
#define _DEFAULT_SOURCE 1
#include "library/source_catalog.h"
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static unsigned checks,cases,destroys,opens;
static int fail_read,fail_close,changed;
static const char *input;
struct smb2fh {int fd;};
struct smb2_context {struct smb2fh *handle;};
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"cover line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void convert(const struct stat *s,struct smb2_stat_64 *v){
 memset(v,0,sizeof *v);v->smb2_type=SMB2_TYPE_FILE;v->smb2_ino=s->st_ino;v->smb2_size=s->st_size;
 v->smb2_mtime=s->st_mtim.tv_sec+changed;v->smb2_mtime_nsec=s->st_mtim.tv_nsec;
 v->smb2_ctime=s->st_ctim.tv_sec;v->smb2_ctime_nsec=s->st_ctim.tv_nsec;
}
void smb2_destroy_context(struct smb2_context *c){C(c);if(c->handle){close(c->handle->fd);free(c->handle);}free(c);destroys++;}
int smb2_stat(struct smb2_context *c,const char *path,struct smb2_stat_64 *v){C(c&&!strcmp(path,"fixture.ffpfsc"));struct stat s;if(stat(input,&s))return -EIO;convert(&s,v);return 0;}
int smb2_fstat(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *v){C(c&&c->handle==h);struct stat s;if(fstat(h->fd,&s))return -EIO;convert(&s,v);return 0;}
struct smb2fh *smb2_open(struct smb2_context *c,const char *path,int mode){C(c&&!c->handle&&!strcmp(path,"fixture.ffpfsc")&&mode==O_RDONLY);int fd=open(input,O_RDONLY|O_NOFOLLOW);if(fd<0)return NULL;c->handle=calloc(1,sizeof *c->handle);C(c->handle);c->handle->fd=fd;opens++;return c->handle;}
int smb2_close(struct smb2_context *c,struct smb2fh *h){C(c&&c->handle==h);int result=close(h->fd);free(h);c->handle=NULL;if(fail_close){fail_close=0;return -EIO;}return result;}
uint32_t smb2_get_max_read_size(struct smb2_context *c){C(c);return 65536;}
int smb2_pread(struct smb2_context *c,struct smb2fh *h,uint8_t *bytes,uint32_t size,uint64_t at){C(c&&c->handle==h&&size<=65536);if(fail_read){fail_read=0;return -EIO;}ssize_t n=pread(h->fd,bytes,size,(off_t)at);return n<0?-errno:(int)n;}
int bw_source_smb_list(struct bw_source_smb *s,const char *path,bw_source_visit_fn visit,void *opaque){
 C(s&&s->smb&&!path[0]);struct stat st;C(!stat(input,&st));
 struct bw_source_info info={.size=st.st_size,.inode=st.st_ino,
  .mtime_ticks=UINT64_C(116444736000000000)+(uint64_t)st.st_mtim.tv_sec*10000000u+st.st_mtim.tv_nsec/100,
  .ctime_ticks=UINT64_C(116444736000000000)+(uint64_t)st.st_ctim.tv_sec*10000000u+st.st_ctim.tv_nsec/100};
 return visit(opaque,"fixture.ffpfsc",&info);
}
int bw_source_smb_file_info(struct bw_source_smb *s,const char *p,struct bw_source_info *i){(void)s;(void)p;(void)i;C(0);return -1;}
int bw_source_smb_small_file(struct bw_source_smb *s,const char *p,size_t n,void **b,size_t *z,struct bw_source_info *i){(void)s;(void)p;(void)n;(void)b;(void)z;(void)i;C(0);return -1;}
static double now(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static void reset(struct bw_source_smb *s){if(s->smb)smb2_destroy_context(s->smb);memset(s,0,sizeof *s);s->smb=calloc(1,sizeof *s->smb);C(s->smb);s->deadline=now()+120;fail_read=fail_close=changed=0;}
static void no_bytes(struct bw_source_smb *s,const struct bw_catalog_item *item){void *bytes=(void *)1;size_t size=123;C(bw_source_catalog_container_cover(s,item,BW_SOURCE_SMALL_FILE_LIMIT,&bytes,&size)==-1);C(!bytes&&!size);}
int main(int argc,char **argv){
 C(argc==4);input=argv[1];size_t expected=(size_t)strtoull(argv[2],NULL,10);int variant=atoi(argv[3]);
 struct bw_source_smb s={0};reset(&s);struct bw_source_catalog catalog={0};
 C(!bw_source_catalog_scan(&s,&catalog));C(catalog.count==1&&catalog.items[0].state==BW_CATALOG_CONTAINER_READY);
 struct bw_catalog_item item=catalog.items[0];struct bw_source_info empty={0};C(!memcmp(&item.icon,&empty,sizeof empty));
 cases++;C(item.cover==(expected>0&&expected<=BW_SOURCE_SMALL_FILE_LIMIT));C(item.logical_icon_size==(item.cover?expected:0));
 void *bytes=NULL;size_t size=0;unsigned before=opens;
 if(!item.cover){C(bw_source_catalog_container_cover(&s,&item,BW_SOURCE_SMALL_FILE_LIMIT,&bytes,&size)==1);C(!bytes&&!size&&opens==before);}
 else{
  cases++;C(!bw_source_catalog_container_cover(&s,&item,BW_SOURCE_SMALL_FILE_LIMIT,&bytes,&size));C(size==expected&&bytes&&!memcmp(bytes,"\x89PNG\r\n\x1a\n",8));free(bytes);
  cases++;before=opens;C(bw_source_catalog_container_cover(&s,&item,expected-1,&bytes,&size)==1);C(!bytes&&!size&&opens==before);
  cases++;reset(&s);struct bw_catalog_item different=item;different.image.smb2_ino++;no_bytes(&s,&different);
  cases++;reset(&s);changed=1;no_bytes(&s,&item);
  cases++;reset(&s);fail_read=1;unsigned destroyed=destroys;no_bytes(&s,&item);C(!s.smb&&destroys==destroyed+1);
  cases++;reset(&s);fail_close=1;no_bytes(&s,&item);C(!s.smb);
  cases++;reset(&s);s.deadline=now()-1;before=opens;no_bytes(&s,&item);C(opens==before);
  cases++;reset(&s);different=item;different.logical_icon_size++;no_bytes(&s,&different);
 }
 bw_source_catalog_destroy(&catalog);if(s.smb)smb2_destroy_context(s.smb);
 printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"variant\":%d,\"localSmbModel\":true}\n",cases,checks,variant);return 0;
}
