/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only comparison of a container against an independently unpacked tree.
 * No game data is embedded or emitted by this test. */
#define _POSIX_C_SOURCE 200809L
#include "container_source.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct input { int fd;const char *path,*folder;struct stat admitted;struct bw_container image;
    uint64_t calls,bytes,compared,largest_size,sequence_calls,sequence_bytes;unsigned files,spans;
    char largest[BW_MANIFEST_MAX_PATH+1];};
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int generation(void *opaque){struct input *p=opaque;struct stat held,named;
    return fstat(p->fd,&held)||stat(p->path,&named)||!same(&p->admitted,&held)||!same(&held,&named)?-1:0;}
static int exact(int fd,uint64_t at,void *out,size_t size){
    for(size_t n=0;n<size;){ssize_t got=pread(fd,(char *)out+n,size-n,(off_t)(at+n));if(got<=0)return -1;n+=(size_t)got;}return 0;}
static int read_image(void *opaque,uint64_t at,void *out,size_t size){struct input *p=opaque;
    p->calls++;p->bytes+=size;return exact(p->fd,at,out,size);}
static int compare(void *opaque,const char *path,const struct bw_pfs_inode *node){
    struct input *p=opaque;char full[4096];struct stat before,after;int n=snprintf(full,sizeof full,"%s/%s",p->folder,path);
    if(n<0||(size_t)n>=sizeof full)return -1;
    int fd=open(full,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;
    int result=-1;unsigned char *left=malloc(65536),*right=malloc(65536);
    if(!left||!right||fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_size<0||(uint64_t)before.st_size!=node->logical_size)goto done;
    struct bw_container_file token={0};if(bw_container_file_open(&p->image,path,&token))goto done;
#ifdef BW_COMPARE_ALL_BYTES
    for(uint64_t at=0;at<node->logical_size;){
        size_t size=node->logical_size-at<65519?(size_t)(node->logical_size-at):65519;
        if(bw_container_file_read(&token,at,left,size)||exact(fd,at,right,size)||memcmp(left,right,size))goto done;
        p->spans++;p->compared+=size;at+=size;
    }
#else
    for(unsigned i=0;i<3&&node->logical_size;i++){
        size_t size=node->logical_size<65536?(size_t)node->logical_size:65536;
        uint64_t at=i==0?0:i==1?(node->logical_size-size)/2:node->logical_size-size;
        if(bw_container_file_read(&token,at,left,size)||exact(fd,at,right,size)||memcmp(left,right,size))goto done;
        p->spans++;p->compared+=size;
    }
#endif
    if(fstat(fd,&after)||!same(&before,&after))goto done;
    if(node->logical_size>p->largest_size){p->largest_size=node->logical_size;strcpy(p->largest,path);}
    p->files++;result=0;
done:free(left);free(right);if(close(fd))result=-1;return result;
}
static int sequence(struct input *p,unsigned mib){
    if(!mib||!p->largest_size)return 0;
    char full[4096];int n=snprintf(full,sizeof full,"%s/%s",p->folder,p->largest);if(n<0||(size_t)n>=sizeof full)return -1;
    int fd=open(full,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;struct bw_container_file token={0};
    unsigned char *left=malloc(BW_PFSC_MAX_READ),*right=malloc(BW_PFSC_MAX_READ);int result=-1;
    uint64_t calls=p->calls,bytes=p->bytes,limit=(uint64_t)mib*1024u*1024u;
    if(limit>p->largest_size)limit=p->largest_size;
    if(!left||!right||bw_container_file_open(&p->image,p->largest,&token))goto done;
    for(uint64_t at=0;at<limit;){size_t size=limit-at>BW_PFSC_MAX_READ?BW_PFSC_MAX_READ:(size_t)(limit-at);
        if(bw_container_file_read(&token,at,left,size)||exact(fd,at,right,size)||memcmp(left,right,size))goto done;
        p->spans++;p->compared+=size;at+=size;
    }
    p->sequence_calls=p->calls-calls;p->sequence_bytes=p->bytes-bytes;result=0;
done:free(left);free(right);if(close(fd))result=-1;return result;
}
int main(int argc,char **argv){
    if(argc<3||argc>4){fprintf(stderr,"usage: container-compare IMAGE UNPACKED-FOLDER [SEQUENTIAL-MIB]\n");return 2;}
    struct input *p=calloc(1,sizeof *p);if(!p)return 2;p->path=argv[1];p->folder=argv[2];p->fd=open(p->path,O_RDONLY|O_NOFOLLOW);
    int result=p->fd<0||fstat(p->fd,&p->admitted)?BW_PFS_IO:0;
    struct bw_container_ops ops={read_image,generation};
    if(!result)result=bw_container_open(&p->image,&ops,p,(uint64_t)p->admitted.st_size);
    if(!result)result=bw_container_walk(&p->image,compare,p,NULL);
    if(!result&&argc==4)result=sequence(p,(unsigned)strtoul(argv[3],NULL,10));
    if(!result)result=generation(p);
    printf("{\"passed\":%s,\"error\":%d,\"containerKind\":%d,\"filesCompared\":%u,\"rangesCompared\":%u,\"bytesCompared\":%" PRIu64 ",\"imageReadCalls\":%" PRIu64 ",\"imageReadBytes\":%" PRIu64 ",\"sequentialCalls\":%" PRIu64 ",\"sequentialBytes\":%" PRIu64 "}\n",
        result?"false":"true",result,p->image.kind,p->files,p->spans,p->compared,p->calls,p->bytes,p->sequence_calls,p->sequence_bytes);
    if(p->fd>=0)close(p->fd);free(p);return result?1:0;
}
