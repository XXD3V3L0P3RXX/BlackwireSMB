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
struct nr_io {int result,error;};
static int io_failed(struct nr_io *io,int error){if(io&&!io->result){io->result=-1;io->error=error;}return -1;}
static int noted(struct bw_native_registration_observation *o,enum bw_registration_reason reason,int raw,int error){
    if(o){if(o->first_reason==BW_REG_NOT_OBSERVED){o->first_reason=reason;o->first_raw_result=raw;o->first_error_snapshot=error;}
        o->reason=reason;o->raw_result=raw;o->error_snapshot=error;}return -1;
}
static int io_noted(struct bw_native_registration_observation *o,enum bw_registration_reason reason,const struct nr_io *io){return noted(o,reason,io->result,io->error);}
const char *bw_registration_state_name(enum bw_registration_state state){
    switch(state){case BW_REG_MATCH:return "match";case BW_REG_ABSENT:return "absent";case BW_REG_PENDING:return "pending";case BW_REG_CONFLICT:return "conflict";default:return "unknown";}
}
const char *bw_native_registration_reason_name(enum bw_registration_reason reason){
    static const char *const names[]={"not_observed","invalid_argument","allocation_failed","path_limit","source_directory_unavailable",
        "apps_parent_unavailable","app_directory_missing","app_directory_unavailable","app_mount_missing","app_mount_unreadable",
        "app_mount_differs","source_param_unreadable","source_param_invalid","app_param_unreadable","param_pair_differs",
        "database_unknown","database_conflict","database_absent","database_row_without_app","app_lookup_failed","observation_changed",
        "exact_match","title_absent","exact_partial"};
    return (unsigned)reason<sizeof names/sizeof names[0]?names[reason]:"unknown";
}
static int same(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
    a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int directory(const char *path,struct stat *out,struct nr_io *io){
    if(io)*io=(struct nr_io){0};char canonical[PATH_MAX];struct stat st;
    if(!realpath(path,canonical))return io_failed(io,errno);
    if(strcmp(path,canonical))return io_failed(io,0);
    if(lstat(path,&st))return io_failed(io,errno);
    if(!S_ISDIR(st.st_mode))return io_failed(io,0);if(out)*out=st;return 0;
}
static int read_blob(const char *path,struct blob *out,struct nr_io *io){
    if(io)*io=(struct nr_io){0};
    char canonical[PATH_MAX];struct stat held,named;int fd=-1,result=-1;
    if(!realpath(path,canonical))return io_failed(io,errno);if(strcmp(path,canonical))return io_failed(io,0);
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return io_failed(io,errno);
    if(fstat(fd,&out->identity)){io_failed(io,errno);goto done;}
    if(!S_ISREG(out->identity.st_mode)||out->identity.st_nlink!=1||out->identity.st_size<=0||out->identity.st_size>BW_SOURCE_PARAM_LIMIT){io_failed(io,0);goto done;}
    out->size=(size_t)out->identity.st_size;size_t at=0;
    while(at<out->size){ssize_t n=pread(fd,out->bytes+at,out->size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0){io_failed(io,n<0?errno:0);goto done;}at+=(size_t)n;}
    if(fstat(fd,&held)){io_failed(io,errno);goto done;}if(lstat(path,&named)){io_failed(io,errno);goto done;}
    if(!S_ISREG(named.st_mode)||named.st_nlink!=1||!same(&out->identity,&held)||!same(&held,&named)){io_failed(io,0);goto done;}
    if(!realpath(path,canonical)){io_failed(io,errno);goto done;}if(strcmp(path,canonical)){io_failed(io,0);goto done;}
    out->bytes[out->size]=0;result=0;
done:
    if(close(fd)){io_failed(io,errno);result=-1;}return result;
}
static int selected_valid(const struct bw_reader_selection *selected){
    char root[75];if(bw_native_selected_root(selected,root))return -1;
    if(!selected||strnlen(selected->title,10)!=9||memcmp(selected->title,"PPSA",4)||
       strnlen(selected->game,75)!=74||memcmp(selected->game,selected->title,9)||selected->game[9]!='_')return -1;
    for(unsigned i=4;i<9;i++)if(selected->title[i]<'0'||selected->title[i]>'9')return -1;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",selected->game[i]))return -1;return 0;
}
static int verify(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected,struct blob *blobs,struct bw_native_registration_observation *o){
    if(!base||!apps||!database||selected_valid(selected))return noted(o,BW_REG_INVALID_ARGUMENT,-1,0);
    char root[2048],app[2048],mount[2048],source_param[2048],app_param[2048];
    char component[75];if(bw_native_selected_root(selected,component))return noted(o,BW_REG_INVALID_ARGUMENT,-1,0);
    int a=snprintf(root,sizeof root,"%s/games/%s",base,component),b=snprintf(app,sizeof app,"%s/%s",apps,selected->title);
    if(a<0||a>=(int)sizeof root||b<0||b>=(int)sizeof app)return noted(o,BW_REG_PATH_LIMIT,-1,0);
    struct nr_io io={0};struct stat root_id,app_id,root_after,app_after;
    if(directory(root,&root_id,&io))return io_noted(o,BW_REG_SOURCE_DIRECTORY_UNAVAILABLE,&io);
    if(directory(app,&app_id,&io))return io_noted(o,io.error==ENOENT?BW_REG_APP_DIRECTORY_MISSING:BW_REG_APP_DIRECTORY_UNAVAILABLE,&io);
    a=snprintf(mount,sizeof mount,"%s/mount.lnk",app);b=snprintf(source_param,sizeof source_param,"%s/sce_sys/param.json",root);
    int c=snprintf(app_param,sizeof app_param,"%s/sce_sys/param.json",app);if(a<0||a>=(int)sizeof mount||b<0||b>=(int)sizeof source_param||c<0||c>=(int)sizeof app_param)return noted(o,BW_REG_PATH_LIMIT,-1,0);
    struct blob *link=blobs,*source=blobs+1,*staged=blobs+2,*again=blobs+3;
    if(read_blob(mount,link,&io))return io_noted(o,io.error==ENOENT?BW_REG_MOUNT_MISSING:BW_REG_MOUNT_UNREADABLE,&io);
    if(link->size>2049)return noted(o,BW_REG_MOUNT_UNREADABLE,-1,0);
    if(read_blob(source_param,source,&io))return io_noted(o,BW_REG_SOURCE_PARAM_UNREADABLE,&io);
    if(read_blob(app_param,staged,&io))return io_noted(o,BW_REG_APP_PARAM_UNREADABLE,&io);
    size_t length=link->size;if(length&&link->bytes[length-1]=='\n')length--;if(length&&link->bytes[length-1]=='\r')length--;
    if(length!=strlen(root)||memcmp(link->bytes,root,length))return noted(o,BW_REG_MOUNT_DIFFERS,-1,0);
    if(bw_registration_param_pair(source->bytes,source->size,staged->bytes,staged->size)<0)return noted(o,BW_REG_PARAM_PAIR_DIFFERS,-1,0);
    char title[10],content[128];if(bw_source_param_identity(source->bytes,source->size,title,content)||strcmp(title,selected->title))return noted(o,BW_REG_SOURCE_PARAM_INVALID,-1,0);
    struct bw_registration_db_observation db={0};int present=bw_registration_db_observe(database,title,content,&db);if(o)o->database=db;
    if(present!=1)return noted(o,present==0?BW_REG_DATABASE_ABSENT:db.state==BW_REG_DB_CONFLICT?BW_REG_DATABASE_CONFLICT:BW_REG_DATABASE_UNKNOWN,db.sqlite_result,db.error_snapshot);
    const char *paths[]={mount,source_param,app_param};const struct blob *before[]={link,source,staged};
    for(unsigned i=0;i<3;i++){
        if(read_blob(paths[i],again,&io))return io_noted(o,BW_REG_OBSERVATION_CHANGED,&io);
        if(!same(&before[i]->identity,&again->identity)||again->size!=before[i]->size||memcmp(again->bytes,before[i]->bytes,again->size))return noted(o,BW_REG_OBSERVATION_CHANGED,-1,0);
    }
    if(directory(root,&root_after,&io)||directory(app,&app_after,&io))return io_noted(o,BW_REG_OBSERVATION_CHANGED,&io);
    if(root_id.st_dev!=root_after.st_dev||root_id.st_ino!=root_after.st_ino||app_id.st_dev!=app_after.st_dev||app_id.st_ino!=app_after.st_ino)return noted(o,BW_REG_OBSERVATION_CHANGED,-1,0);
    if(o)o->reason=BW_REG_EXACT_MATCH;return 0;
}
int bw_native_registration_verify(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected){
    struct blob *blobs=calloc(4,sizeof *blobs);if(!blobs)return -1;int result=verify(base,apps,database,selected,blobs,NULL);free(blobs);return result;
}
enum bw_registration_state bw_native_registration_observe(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected,struct bw_native_registration_observation *out){
    struct bw_native_registration_observation observation={.state=BW_REG_UNKNOWN,.database={.state=BW_REG_DB_UNKNOWN}};
    if(!out)out=&observation;else *out=observation;
    if(!base||!apps||!database||selected_valid(selected)){noted(out,BW_REG_INVALID_ARGUMENT,-1,0);return BW_REG_UNKNOWN;}
    struct blob *blobs=calloc(4,sizeof *blobs);if(!blobs){noted(out,BW_REG_ALLOCATION_FAILED,-1,0);return BW_REG_UNKNOWN;}
    enum bw_registration_state result=BW_REG_UNKNOWN;if(!verify(base,apps,database,selected,blobs,out)){result=BW_REG_MATCH;goto done;}
    char root[2048],app[2048],param[2048],mount[2048],title[10],content[128];struct stat st;struct nr_io io={0};
    char component[75];if(bw_native_selected_root(selected,component)){noted(out,BW_REG_INVALID_ARGUMENT,-1,0);goto done;}
    int a=snprintf(root,sizeof root,"%s/games/%s",base,component),b=snprintf(app,sizeof app,"%s/%s",apps,selected->title);
    if(a<0||a>=(int)sizeof root||b<0||b>=(int)sizeof app){noted(out,BW_REG_PATH_LIMIT,-1,0);goto done;}
    if(directory(root,NULL,&io)){io_noted(out,BW_REG_SOURCE_DIRECTORY_UNAVAILABLE,&io);goto done;}
    if(directory(apps,NULL,&io)){io_noted(out,BW_REG_APPS_PARENT_UNAVAILABLE,&io);goto done;}
    a=snprintf(param,sizeof param,"%s/sce_sys/param.json",root);b=snprintf(mount,sizeof mount,"%s/mount.lnk",app);
    if(a<0||a>=(int)sizeof param||b<0||b>=(int)sizeof mount){noted(out,BW_REG_PATH_LIMIT,-1,0);goto done;}
    if(read_blob(param,blobs,&io)){io_noted(out,BW_REG_SOURCE_PARAM_UNREADABLE,&io);goto done;}
    if(bw_source_param_identity(blobs->bytes,blobs->size,title,content)||strcmp(title,selected->title)){noted(out,BW_REG_SOURCE_PARAM_INVALID,-1,0);goto done;}
    unsigned prior_release_unknown=out->database.release_or_identity_unknown;
    int present=bw_registration_db_observe(database,title,content,&out->database);
    out->database.release_or_identity_unknown|=prior_release_unknown;
    if(present<0){noted(out,out->database.state==BW_REG_DB_CONFLICT?BW_REG_DATABASE_CONFLICT:BW_REG_DATABASE_UNKNOWN,out->database.sqlite_result,out->database.error_snapshot);goto done;}
    if(lstat(app,&st)){
        int error=errno;if(error==ENOENT){result=present?BW_REG_CONFLICT:BW_REG_ABSENT;noted(out,present?BW_REG_DATABASE_ROW_WITHOUT_APP:BW_REG_TITLE_ABSENT,-1,error);}
        else noted(out,BW_REG_APP_LOOKUP_FAILED,-1,error);goto done;
    }
    if(directory(app,NULL,&io)){io_noted(out,BW_REG_APP_DIRECTORY_UNAVAILABLE,&io);goto done;}
    if(read_blob(mount,blobs+1,&io)){io_noted(out,io.error==ENOENT?BW_REG_MOUNT_MISSING:BW_REG_MOUNT_UNREADABLE,&io);goto done;}
    size_t n=blobs[1].size;if(n&&blobs[1].bytes[n-1]=='\n')n--;if(n&&blobs[1].bytes[n-1]=='\r')n--;
    result=n==strlen(root)&&!memcmp(blobs[1].bytes,root,n)?BW_REG_PENDING:BW_REG_CONFLICT;
    noted(out,result==BW_REG_PENDING?BW_REG_EXACT_PARTIAL:BW_REG_MOUNT_DIFFERS,0,0);
done:
    out->state=result;free(blobs);return result;
}
enum bw_registration_state bw_native_registration_state(const char *base,const char *apps,const char *database,const struct bw_reader_selection *selected){return bw_native_registration_observe(base,apps,database,selected,NULL);}
