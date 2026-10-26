/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual catalog admission and diagnostics; synthetic local SMB model only. */
#define _DEFAULT_SOURCE 1
#include "library/source_catalog.h"
#include "library/compressed_data.h"
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
static char **inputs;
static int count;
static unsigned checks,opens,reads,stats,heldstats;
static unsigned prepared_files,hashed_files,copied_files;static uint64_t hashed_bytes,copied_bytes;
struct smb2fh { int fd,index; };
struct smb2_context { struct smb2fh *handle; };
#define C(x) do { checks++;if(!(x)){fprintf(stderr,"private catalog line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static const char *name(int i){const char *p=strrchr(inputs[i],'/');return p?p+1:inputs[i];}
static int index_for(const char *path){for(int i=0;i<count;i++)if(!strcmp(path,name(i)))return i;return -1;}
static void convert(const struct stat *s,struct smb2_stat_64 *v){
 memset(v,0,sizeof *v);v->smb2_type=SMB2_TYPE_FILE;v->smb2_ino=s->st_ino;v->smb2_size=s->st_size;
 v->smb2_mtime=s->st_mtim.tv_sec;v->smb2_mtime_nsec=s->st_mtim.tv_nsec;
 v->smb2_ctime=s->st_ctim.tv_sec;v->smb2_ctime_nsec=s->st_ctim.tv_nsec;
}
void smb2_destroy_context(struct smb2_context *c){C(c);if(c->handle){close(c->handle->fd);free(c->handle);}free(c);}
int smb2_stat(struct smb2_context *c,const char *path,struct smb2_stat_64 *v){stats++;C(c);int i=index_for(path);C(i>=0);struct stat s;if(stat(inputs[i],&s))return -EIO;convert(&s,v);return 0;}
int smb2_fstat(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *v){heldstats++;C(c&&c->handle==h);struct stat s;if(fstat(h->fd,&s))return -EIO;convert(&s,v);return 0;}
struct smb2fh *smb2_open(struct smb2_context *c,const char *path,int mode){C(c&&!c->handle&&mode==O_RDONLY);int i=index_for(path);C(i>=0);int fd=open(inputs[i],O_RDONLY|O_NOFOLLOW);if(fd<0)return NULL;c->handle=calloc(1,sizeof *c->handle);C(c->handle);c->handle->fd=fd;c->handle->index=i;opens++;return c->handle;}
int smb2_close(struct smb2_context *c,struct smb2fh *h){C(c&&c->handle==h);int result=close(h->fd);free(h);c->handle=NULL;return result;}
uint32_t smb2_get_max_read_size(struct smb2_context *c){C(c);return 65536;}
int smb2_pread(struct smb2_context *c,struct smb2fh *h,uint8_t *bytes,uint32_t size,uint64_t at){reads++;C(c&&c->handle==h&&size<=65536);ssize_t n=pread(h->fd,bytes,size,(off_t)at);return n<0?-errno:(int)n;}
int bw_source_smb_list(struct bw_source_smb *s,const char *path,bw_source_visit_fn visit,void *opaque){
 C(s&&s->smb&&!path[0]);for(int i=0;i<count;i++){struct stat st;C(!stat(inputs[i],&st));
 struct bw_source_info info={.size=st.st_size,.inode=st.st_ino,
  .mtime_ticks=UINT64_C(116444736000000000)+(uint64_t)st.st_mtim.tv_sec*10000000u+st.st_mtim.tv_nsec/100,
  .ctime_ticks=UINT64_C(116444736000000000)+(uint64_t)st.st_ctim.tv_sec*10000000u+st.st_ctim.tv_nsec/100};
 if(visit(opaque,name(i),&info))return -1;}return 0;
}
int bw_source_smb_file_info(struct bw_source_smb *s,const char *p,struct bw_source_info *i){(void)s;(void)p;(void)i;C(0);return -1;}
int bw_source_smb_small_file(struct bw_source_smb *s,const char *p,size_t n,void **b,size_t *z,struct bw_source_info *i){(void)s;(void)p;(void)n;(void)b;(void)z;(void)i;C(0);return -1;}
void bw_source_smb_progress(struct bw_source_smb *p,enum bw_source_phase phase,uint32_t files,uint32_t total,uint64_t bytes,uint64_t byte_total){
 p->progress.phase=phase;p->progress.files_done=files;p->progress.files_total=total;p->progress.bytes_done=bytes;p->progress.bytes_total=byte_total;
}
static int operation_current(void *opaque){struct bw_source_smb *s=opaque;struct timespec t;return !s->smb||clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec+t.tv_nsec/1e9>=s->deadline;}
static int consume(void *opaque,const void *bytes,size_t size){uint64_t *total=opaque;C(bytes&&size&&size<=BW_COMPRESSED_DATA_CHUNK);*total+=size;return 0;}
static void prepare_copy(struct bw_source_smb *s,const struct bw_catalog_item *item){
 C(item->state==BW_CATALOG_CONTAINER_READY);
 struct bw_container_smb *image=calloc(1,sizeof *image);C(image);C(!bw_container_smb_open_owned(image,&s->smb,item->source));
 struct bw_source_binding binding={0};strcpy(binding.source,item->source);strcpy(binding.title,item->title_id);strcpy(binding.game,"PPSA09999_fixture");
 struct bw_compressed_data data={0};C(!bw_compressed_data_prepare(&data,image,&binding,32u*1024u*1024u,s,operation_current,s));
 C(data.ready&&data.manifest.count==item->namespace_files);C(data.manifest.path_bytes==item->namespace_path_bytes);
 prepared_files=data.manifest.count;hashed_files=data.hashed_files;hashed_bytes=data.hashed_bytes;
 for(uint32_t i=0;i<data.manifest.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&data.manifest,i,&row));
  if(row.flags==BW_MANIFEST_LOCAL){uint64_t copied=0;C(!bw_compressed_data_copy_local(&data,i,consume,&copied));C(copied==row.size);copied_files++;copied_bytes+=copied;}}
 C(copied_files==data.map.local_count&&copied_bytes==data.map.local_bytes&&hashed_files==copied_files&&hashed_bytes==copied_bytes);
 C(!bw_compressed_data_current(&data));bw_compressed_data_destroy(&data);C(!bw_container_smb_close(image));free(image);
}
int main(int argc,char **argv){
 C(!strcmp(bw_source_catalog_error_name(NULL),"not_applicable"));C(!strcmp(bw_source_catalog_stage_name(NULL),"unknown"));
 C(argc>=2);int prepare=!strcmp(argv[1],"--prepare");inputs=argv+1+prepare;count=argc-1-prepare;C(count>0);
 struct timespec now;C(!clock_gettime(CLOCK_MONOTONIC,&now));
 struct bw_source_smb s={.deadline=now.tv_sec+now.tv_nsec/1e9+120};s.smb=calloc(1,sizeof *s.smb);C(s.smb);
 struct bw_source_catalog catalog={0};int result=bw_source_catalog_scan(&s,&catalog);
 if(prepare){C(!result&&catalog.count==1);prepare_copy(&s,catalog.items);}
 char *json=NULL;size_t length=0;C(!bw_source_catalog_json(&catalog,&json,&length));
 printf("{\"checks\":%u,\"result\":%d,\"items\":%zu,\"opens\":%u,\"payloadReads\":%u,\"namedStats\":%u,\"heldStats\":%u,\"preparedFiles\":%u,\"hashedFiles\":%u,\"hashedBytes\":%llu,\"copiedFiles\":%u,\"copiedBytes\":%llu,\"catalog\":",checks,result,catalog.count,opens,reads,stats,heldstats,prepared_files,hashed_files,(unsigned long long)hashed_bytes,copied_files,(unsigned long long)copied_bytes);
 fwrite(json,1,length,stdout);free(json);printf(",\"internalErrors\":[");
 for(size_t i=0;i<catalog.count;i++){if(i)printf(",");printf("%d",catalog.items[i].container_error);}puts("]}");
 bw_source_catalog_destroy(&catalog);if(s.smb)smb2_destroy_context(s.smb);return 0;
}
