#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "native_startup_hint.h"
#include "native_project_recipe.h"
#include "source_plan.h"
#include "../streamreader/filemap.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BW_NATIVE_PRELOAD
static int command_name(const char *name){
    const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;char folded[32];size_t n=strlen(leaf);
    if(n>=sizeof folded)return 0;
    for(size_t i=0;i<=n;i++)folded[i]=leaf[i]>='A'&&leaf[i]<='Z'?(char)(leaf[i]+('a'-'A')):leaf[i];
    return !strcmp(folded,"ue4commandline.txt")||!strcmp(folded,"uecommandline.txt")||!strcmp(folded,"commandline.txt");
}
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int read_command(const char *path,uint64_t size,unsigned char bytes[BW_NATIVE_PROJECT_COMMAND_LIMIT]){
    struct stat before,after,named;int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK),result=-1;
    if(fd<0)return -1;
    if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<0||(uint64_t)before.st_size!=size)goto done;
    size_t used=0;
    while(used<(size_t)size){ssize_t n=pread(fd,bytes+used,(size_t)size-used,(off_t)used);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;used+=(size_t)n;}
    if(fstat(fd,&after)||lstat(path,&named)||!S_ISREG(named.st_mode)||named.st_nlink!=1||!same(&before,&after)||!same(&after,&named))goto done;
    result=0;
done:
    if(close(fd))result=-1;return result;
}
#endif
int bw_native_legacy_startup_upgrade(const char *base,const struct bw_provider_binding *identity,
    const struct bw_native_prepared *prepared,unsigned *out){
    if(!out)return -1;*out=0;
    if(!base||!identity||!prepared||prepared->state!=BW_PREPARED_VALID||prepared->prepared_policy||
        identity->kind!=BW_PROVIDER_FOLDER||strcmp(identity->binding.game,prepared->game))return -1;
    char root[1024];int n=snprintf(root,sizeof root,"%s/games/%s",base,prepared->game);if(n<0||(size_t)n>=sizeof root)return -1;
    struct bw_files files={0};int result=-1;unsigned upgrade=0;
#ifndef BW_NATIVE_PRELOAD
    unsigned commands=0;
#endif
    if(bw_files_load(&files,root,"blackwire.files"))goto done;
    for(uint32_t i=0;i<bw_files_count(&files);i++){
        struct bw_manifest_record row;if(bw_files_get(&files,i,&row))goto done;
        if(row.flags==BW_MANIFEST_STREAM&&bw_source_startup_metadata(row.path,row.size))upgrade=1;
#ifndef BW_NATIVE_PRELOAD
        if(!command_name(row.path))continue;
        if(++commands>8||row.flags!=BW_MANIFEST_LOCAL||!row.size||row.size>BW_NATIVE_PROJECT_COMMAND_LIMIT){upgrade=1;continue;}
        char path[2048],requested[250],destination[241];unsigned char bytes[BW_NATIVE_PROJECT_COMMAND_LIMIT];
        n=snprintf(path,sizeof path,"%s/%s",root,row.path);if(n<0||(size_t)n>=sizeof path||read_command(path,row.size,bytes))goto done;
        int parsed=bw_native_project_command(bytes,(size_t)row.size,requested,destination);
        if(parsed<0){upgrade=1;continue;}if(!parsed)continue;
        int present=0;for(uint32_t j=0;j<bw_files_count(&files);j++){struct bw_manifest_record target;if(bw_files_get(&files,j,&target))goto done;
            if(!strcmp(target.path,destination)&&target.flags==BW_MANIFEST_LOCAL&&target.size){present=1;break;}}
        if(!present)upgrade=1;
#endif
    }
    struct bw_native_prepared after={0};
    if(bw_native_send_inspect(base,identity,&after)||after.state!=BW_PREPARED_VALID||memcmp(after.record_sha256,prepared->record_sha256,32))goto done;
    *out=upgrade;result=0;
done:
    bw_files_destroy(&files);return result;
}
