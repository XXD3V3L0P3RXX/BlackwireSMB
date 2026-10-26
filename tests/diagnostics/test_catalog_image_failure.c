/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual catalog/container/decoder code with a read-only local SMB adapter.
 * Faults alter adapter replies only; the generated source is never modified. */
#define _DEFAULT_SOURCE 1
#include "library/source_catalog.h"
#include "library/send_diagnostic.h"
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

struct smb2fh { int fd; };
struct smb2_context { struct smb2fh *handle; };
static const char *fixture,*mode;
static struct bw_source_smb *source;
static unsigned checks,stats,heldstats,reads,opens,closes,destroys;
#define C(x) do { checks++;if(!(x)){fprintf(stderr,"catalog failure line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void metadata(const struct stat *s,struct smb2_stat_64 *v){
 memset(v,0,sizeof *v);v->smb2_type=SMB2_TYPE_FILE;
 /* Exercise exact integer logging above JavaScript's safe-number range. */
 v->smb2_ino=UINT64_C(9007199254740993)+(uint64_t)s->st_ino%1000000u;v->smb2_size=s->st_size;
 v->smb2_mtime=s->st_mtim.tv_sec;v->smb2_mtime_nsec=s->st_mtim.tv_nsec;
 v->smb2_ctime=s->st_ctim.tv_sec;v->smb2_ctime_nsec=s->st_ctim.tv_nsec;
}
static void change(struct smb2_stat_64 *s,const char *field){
 if(!strcmp(field,"type"))s->smb2_type=SMB2_TYPE_DIRECTORY;
 else if(!strcmp(field,"inode"))s->smb2_ino++;
 else if(!strcmp(field,"size"))s->smb2_size++;
 else if(!strcmp(field,"mtime_seconds"))s->smb2_mtime++;
 else if(!strcmp(field,"mtime_nanoseconds"))s->smb2_mtime_nsec++;
 else if(!strcmp(field,"ctime_seconds"))s->smb2_ctime++;
 else if(!strcmp(field,"ctime_nanoseconds"))s->smb2_ctime_nsec++;
 else C(0);
}
void smb2_destroy_context(struct smb2_context *c){
 C(c);destroys++;if(c->handle){C(!close(c->handle->fd));closes++;free(c->handle);}free(c);
}
int smb2_stat(struct smb2_context *c,const char *path,struct smb2_stat_64 *v){
 C(c&&!strcmp(path,"Neutral.ffpfsc"));stats++;
 if(!strcmp(mode,"admit-stat")&&stats==1){errno=EACCES;return -EACCES;}
 if(!strcmp(mode,"generation-stat")&&stats==2){errno=EIO;return -EIO;}
 struct stat s;C(!stat(fixture,&s));metadata(&s,v);
 if(!strcmp(mode,"admit-metadata")&&stats==1)v->smb2_type=SMB2_TYPE_DIRECTORY;
 if(!strncmp(mode,"named-",6)&&stats>=2)change(v,mode+6);
 return 0;
}
int smb2_fstat(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *v){
 C(c&&c->handle==h);heldstats++;
 if(!strcmp(mode,"generation-fstat")){errno=EIO;return -EIO;}
 struct stat s;C(!fstat(h->fd,&s));metadata(&s,v);
 if(!strncmp(mode,"held-",5))change(v,mode+5);
 return 0;
}
struct smb2fh *smb2_open(struct smb2_context *c,const char *path,int flags){
 C(c&&!c->handle&&!strcmp(path,"Neutral.ffpfsc")&&flags==O_RDONLY);opens++;
 if(!strcmp(mode,"open-readonly")){errno=EACCES;return NULL;}
 int fd=open(fixture,O_RDONLY|O_NOFOLLOW);C(fd>=0);
 c->handle=calloc(1,sizeof *c->handle);C(c->handle);c->handle->fd=fd;return c->handle;
}
int smb2_close(struct smb2_context *c,struct smb2fh *h){
 C(c&&c->handle==h);C(!close(h->fd));closes++;free(h);c->handle=NULL;
 if(!strcmp(mode,"close")||!strcmp(mode,"pread-eof-close")){errno=EIO;return -EIO;}return 0;
}
uint32_t smb2_get_max_read_size(struct smb2_context *c){C(c);return !strcmp(mode,"pread-max-read")?0:65536;}
int smb2_pread(struct smb2_context *c,struct smb2fh *h,uint8_t *bytes,uint32_t size,uint64_t at){
 C(c&&c->handle==h&&size<=65536);reads++;
 if(!strcmp(mode,"pread-error")){errno=ETIMEDOUT;return -ETIMEDOUT;}
 if(!strcmp(mode,"pread-eof")||!strcmp(mode,"pread-eof-close"))return 0;
 if(!strcmp(mode,"pread-oversize"))return (int)size+1;
 if(!strcmp(mode,"deadline"))source->deadline=0;
 if(!strcmp(mode,"short-read")&&size>1)size/=2;
 ssize_t n=pread(h->fd,bytes,size,(off_t)at);return n<0?-errno:(int)n;
}
int bw_source_smb_list(struct bw_source_smb *s,const char *path,bw_source_visit_fn visit,void *opaque){
 C(s==source&&s->smb&&!path[0]);struct stat st;C(!stat(fixture,&st));struct smb2_stat_64 meta;metadata(&st,&meta);
 struct bw_source_info info={.size=meta.smb2_size,.inode=meta.smb2_ino,
 .mtime_ticks=UINT64_C(116444736000000000)+meta.smb2_mtime*10000000u+meta.smb2_mtime_nsec/100,
 .ctime_ticks=UINT64_C(116444736000000000)+meta.smb2_ctime*10000000u+meta.smb2_ctime_nsec/100};
 return visit(opaque,"Neutral.ffpfsc",&info);
}
int bw_source_smb_file_info(struct bw_source_smb *s,const char *path,struct bw_source_info *out){(void)s;(void)path;(void)out;C(0);return -1;}
int bw_source_smb_small_file(struct bw_source_smb *s,const char *p,size_t n,void **b,size_t *z,struct bw_source_info *i){(void)s;(void)p;(void)n;(void)b;(void)z;(void)i;C(0);return -1;}
int main(int argc,char **argv){
 C(argc==3);fixture=argv[1];mode=argv[2];struct timespec now;C(!clock_gettime(CLOCK_MONOTONIC,&now));
 struct bw_source_smb s={.deadline=now.tv_sec+now.tv_nsec/1e9+120};source=&s;s.smb=calloc(1,sizeof *s.smb);C(s.smb);
 struct bw_source_catalog catalog={0};int result=bw_source_catalog_scan(&s,&catalog);
 int owner_present=s.smb!=NULL;const char *reason=catalog.error?catalog.error:"none";
 C(catalog.count==1);int container_result=catalog.items[0].container_error;
 const char *stage=bw_source_catalog_stage_name(catalog.items);
 if(!strcmp(mode,"valid")||!strcmp(mode,"short-read")){C(!result&&catalog.items[0].state==BW_CATALOG_CONTAINER_READY);}
 else C(result==-1);
 if(s.smb)smb2_destroy_context(s.smb);
 C(destroys==1);C(opens<=1&&closes<=1);
 unsigned side_effects=0;errno=EDOM;
 bw_send_log("diagnostic-inert-probe","sideEffects=%u",++side_effects);
 C(errno==EDOM);
#if defined(BW_SEND_DIAGNOSTIC) && BW_SEND_DIAGNOSTIC
 C(side_effects==1);
#else
 C(side_effects==0);
#endif
 printf("{\"passed\":true,\"checks\":%u,\"result\":%d,\"containerResult\":%d,\"stage\":\"%s\",\"reason\":\"%s\",\"ownerPresentBeforeCleanup\":%s,\"stats\":%u,\"heldStats\":%u,\"reads\":%u,\"opens\":%u,\"closes\":%u,\"destroys\":%u}\n",
 checks,result,container_result,stage,reason,owner_present?"true":"false",stats,heldstats,reads,opens,closes,destroys);
 bw_source_catalog_destroy(&catalog);return 0;
}
