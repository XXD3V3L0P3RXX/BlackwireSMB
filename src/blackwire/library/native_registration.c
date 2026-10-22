#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_registration.h"
#include "native_prepared_target.h"
#include "source_param.h"
#include "registration_param_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct blob {unsigned char bytes[BW_SOURCE_PARAM_LIMIT+1];size_t size;struct stat identity;};
static int same(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
    a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int directory(const char *path,struct stat *out){
    char canonical[PATH_MAX];struct stat st;if(!realpath(path,canonical)||strcmp(path,canonical)||lstat(path,&st)||!S_ISDIR(st.st_mode))return -1;if(out)*out=st;return 0;
}
static int read_blob(const char *path,struct blob *out){
    char canonical[PATH_MAX];struct stat held,named;int fd=-1,result=-1;
    if(!realpath(path,canonical)||strcmp(path,canonical))return -1;fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,&out->identity)||!S_ISREG(out->identity.st_mode)||out->identity.st_nlink!=1||out->identity.st_size<=0||out->identity.st_size>BW_SOURCE_PARAM_LIMIT)goto done;
    out->size=(size_t)out->identity.st_size;size_t at=0;
    while(at<out->size){ssize_t n=pread(fd,out->bytes+at,out->size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;at+=(size_t)n;}
    if(fstat(fd,&held)||lstat(path,&named)||!S_ISREG(named.st_mode)||named.st_nlink!=1||!same(&out->identity,&held)||!same(&held,&named)||
       !realpath(path,canonical)||strcmp(path,canonical))goto done;
    out->bytes[out->size]=0;result=0;
done:
    if(close(fd))result=-1;return result;
}
static int selected_valid(const struct bw_reader_selection *selected){
    char root[75];if(bw_native_selected_root(selected,root))return -1;
    if(!selected||strnlen(selected->title,10)!=9||memcmp(selected->title,"PPSA",4)||
       strnlen(selected->game,75)!=74||memcmp(selected->game,selected->title,9)||selected->game[9]!='_')return -1;
    for(unsigned i=4;i<9;i++)if(selected->title[i]<'0'||selected->title[i]>'9')return -1;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",selected->game[i]))return -1;return 0;
}
static int verify(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected,struct blob *blobs){
    if(!base||!apps||!database||selected_valid(selected))return -1;
    char root[2048],app[2048],mount[2048],source_param[2048],app_param[2048];
    char component[75];if(bw_native_selected_root(selected,component))return -1;
    int a=snprintf(root,sizeof root,"%s/games/%s",base,component),b=snprintf(app,sizeof app,"%s/%s",apps,selected->title);
    if(a<0||a>=(int)sizeof root||b<0||b>=(int)sizeof app)return -1;
    struct stat root_id,app_id,root_after,app_after;if(directory(root,&root_id)||directory(app,&app_id))return -1;
    a=snprintf(mount,sizeof mount,"%s/mount.lnk",app);b=snprintf(source_param,sizeof source_param,"%s/sce_sys/param.json",root);
    int c=snprintf(app_param,sizeof app_param,"%s/sce_sys/param.json",app);if(a<0||a>=(int)sizeof mount||b<0||b>=(int)sizeof source_param||c<0||c>=(int)sizeof app_param)return -1;
    struct blob *link=blobs,*source=blobs+1,*staged=blobs+2,*again=blobs+3;
    if(read_blob(mount,link)||link->size>2049||read_blob(source_param,source)||read_blob(app_param,staged))return -1;
    size_t length=link->size;if(length&&link->bytes[length-1]=='\n')length--;if(length&&link->bytes[length-1]=='\r')length--;
    if(length!=strlen(root)||memcmp(link->bytes,root,length)||bw_registration_param_pair(source->bytes,source->size,staged->bytes,staged->size)<0)return -1;
    char title[10],content[128];if(bw_source_param_identity(source->bytes,source->size,title,content)||strcmp(title,selected->title))return -1;
    if(bw_registration_db_match(database,title,content)!=1)return -1;
    const char *paths[]={mount,source_param,app_param};const struct blob *before[]={link,source,staged};
    for(unsigned i=0;i<3;i++)if(read_blob(paths[i],again)||!same(&before[i]->identity,&again->identity)||again->size!=before[i]->size||memcmp(again->bytes,before[i]->bytes,again->size))return -1;
    return directory(root,&root_after)||directory(app,&app_after)||root_id.st_dev!=root_after.st_dev||root_id.st_ino!=root_after.st_ino||
        app_id.st_dev!=app_after.st_dev||app_id.st_ino!=app_after.st_ino?-1:0;
}
int bw_native_registration_verify(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected){
    struct blob *blobs=calloc(4,sizeof *blobs);if(!blobs)return -1;int result=verify(base,apps,database,selected,blobs);free(blobs);return result;
}
enum bw_registration_state bw_native_registration_state(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected){
    if(!base||!apps||!database||selected_valid(selected))return BW_REG_UNKNOWN;
    struct blob *blobs=calloc(4,sizeof *blobs);if(!blobs)return BW_REG_UNKNOWN;
    enum bw_registration_state result=BW_REG_UNKNOWN;if(!verify(base,apps,database,selected,blobs)){result=BW_REG_MATCH;goto done;}
    char root[2048],app[2048],param[2048],mount[2048],title[10],content[128];struct stat st;
    char component[75];if(bw_native_selected_root(selected,component))goto done;
    int a=snprintf(root,sizeof root,"%s/games/%s",base,component),b=snprintf(app,sizeof app,"%s/%s",apps,selected->title);
    if(a<0||a>=(int)sizeof root||b<0||b>=(int)sizeof app||directory(root,NULL)||directory(apps,NULL))goto done;
    a=snprintf(param,sizeof param,"%s/sce_sys/param.json",root);b=snprintf(mount,sizeof mount,"%s/mount.lnk",app);
    if(a<0||a>=(int)sizeof param||b<0||b>=(int)sizeof mount||read_blob(param,blobs)||bw_source_param_identity(blobs->bytes,blobs->size,title,content)||strcmp(title,selected->title))goto done;
    int present=bw_registration_db_match(database,title,content);if(present<0)goto done;
    if(lstat(app,&st)){
        if(errno==ENOENT)result=present?BW_REG_CONFLICT:BW_REG_ABSENT;goto done;
    }
    if(directory(app,NULL)||read_blob(mount,blobs+1))goto done;
    size_t n=blobs[1].size;if(n&&blobs[1].bytes[n-1]=='\n')n--;if(n&&blobs[1].bytes[n-1]=='\r')n--;
    result=n==strlen(root)&&!memcmp(blobs[1].bytes,root,n)?BW_REG_PENDING:BW_REG_CONFLICT;
done:
    free(blobs);return result;
}
