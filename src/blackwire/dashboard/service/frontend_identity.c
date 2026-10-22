/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "frontend_identity.h"
#include "source_param.h"
#include "copy_sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BD_FRONTEND_IDENTITY_HOST_TEST
#include "copy_fs.h"
#endif

/* Native *at stubs can return a positive error without setting errno.
 * Keep the existing normalized syscall boundary; host fixtures use POSIX. */
static int frontend_fstatat(int parent,const char *name,struct stat *out,int flags) {
#ifdef BD_FRONTEND_IDENTITY_HOST_TEST
    return fstatat(parent,name,out,flags);
#else
    return cp_fstatat(parent,name,out,flags);
#endif
}
static int frontend_openat(int parent,const char *name,int flags) {
#ifdef BD_FRONTEND_IDENTITY_HOST_TEST
    return openat(parent,name,flags);
#else
    return v2_openat(parent,name,flags,0);
#endif
}

enum {FILE_BOOT,FILE_SOURCE_PARAM,FILE_LINK,FILE_REGISTERED_PARAM,FILE_COUNT};
enum {DIR_SOURCE,DIR_REGISTERED,DIR_SOURCE_SYS,DIR_REGISTERED_SYS,DIR_COUNT};
#define HASH_CHUNK 32768u

struct checked_directory {int fd;struct stat stat;};
struct checked_file {int fd;struct stat stat;};

static int nonzero(const uint8_t hash[32]) {
    unsigned value=0;
    for (size_t i=0;i<32;i++) value|=hash[i];
    return value!=0;
}
static int expected_valid(const struct bd_frontend_package *p) {
    return p&&p->eboot.bytes>=16&&p->eboot.bytes<=BD_FRONTEND_BOOT_LIMIT&&
        p->param.bytes>0&&p->param.bytes<=BD_FRONTEND_PARAM_LIMIT&&
        p->mount_link.bytes>0&&p->mount_link.bytes<=BD_FRONTEND_LINK_LIMIT&&
        nonzero(p->eboot.sha256)&&nonzero(p->param.sha256)&&nonzero(p->mount_link.sha256);
}
static void hash_u64(struct cp_sha *hash,uint64_t value) {
    uint8_t bytes[8];
    for (unsigned i=0;i<8;i++) bytes[i]=(uint8_t)(value>>(8u*i));
    cp_sha_update(hash,bytes,sizeof bytes);
}
static void package_fingerprint(const struct bd_frontend_package *p,uint8_t out[32]) {
    static const char domain[]="blackwire-dashboard-expected-installed-snapshot-v1";
    struct cp_sha hash;
    cp_sha_init(&hash);
    cp_sha_update(&hash,domain,sizeof domain);
    cp_sha_update(&hash,BD_FRONTEND_TITLE,sizeof BD_FRONTEND_TITLE);
    cp_sha_update(&hash,BD_FRONTEND_SOURCE,sizeof BD_FRONTEND_SOURCE);
    cp_sha_update(&hash,BD_FRONTEND_REGISTERED,sizeof BD_FRONTEND_REGISTERED);
    const struct bd_frontend_file *files[]={&p->eboot,&p->param,&p->mount_link};
    for (size_t i=0;i<sizeof files/sizeof files[0];i++) {
        hash_u64(&hash,files[i]->bytes);
        cp_sha_update(&hash,files[i]->sha256,32);
    }
    cp_sha_final(&hash,out);
}
static int path_valid(const char *path) {
    return path&&path[0]=='/'&&strnlen(path,BD_SOURCE_BYTES)<BD_SOURCE_BYTES&&
        path[1]!=0&&path[strlen(path)-1]!='/';
}
static int initialize(struct bd_frontend_owner *out,const struct bd_frontend_package *p,
                       const char *source,const char *registered) {
    if (!out) return -1;
    struct bd_frontend_package held={0};
    if (p) held=*p;
    char from[BD_SOURCE_BYTES]={0},to[BD_SOURCE_BYTES]={0};
    int good=expected_valid(p)&&path_valid(source)&&path_valid(registered)&&strcmp(source,registered);
    if (good) {
        memcpy(from,source,strlen(source)+1);
        memcpy(to,registered,strlen(registered)+1);
    }
    memset(out,0,sizeof *out);
    if (!good) return -1;
    out->expected=held;
    memcpy(out->source,from,sizeof from);
    memcpy(out->registered,to,sizeof to);
    out->initialized=1;
    return 0;
}
int bd_frontend_init(struct bd_frontend_owner *out,const struct bd_frontend_package *p) {
    return initialize(out,p,BD_FRONTEND_SOURCE,BD_FRONTEND_REGISTERED);
}
#ifdef BD_FRONTEND_IDENTITY_HOST_TEST
int bd_frontend_init_at_for_test(struct bd_frontend_owner *out,const struct bd_frontend_package *p,
                                 const char *source,const char *registered) {
    return initialize(out,p,source,registered);
}
#endif
static int same(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int canonical_directory(const char *path,struct stat *out) {
    char actual[PATH_MAX];
    return !path_valid(path)||!realpath(path,actual)||strcmp(path,actual)||lstat(path,out)||
        !S_ISDIR(out->st_mode)?-1:0;
}
static int root_open(const char *path,struct checked_directory *out) {
    struct stat named,opened;
    if (canonical_directory(path,&named)) return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return -1;
    if (fstat(fd,&opened)||!same(&named,&opened)) { (void)close(fd);return -1; }
    out->fd=fd;out->stat=opened;
    return 0;
}
static int child_open(int parent,const char *name,struct checked_directory *out) {
    struct stat named,opened;
    if (frontend_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(named.st_mode)) return -1;
    int fd=frontend_openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return -1;
    if (fstat(fd,&opened)||!same(&named,&opened)) { (void)close(fd);return -1; }
    out->fd=fd;out->stat=opened;
    return 0;
}
static int file_open(int parent,const char *name,const struct bd_frontend_file *expected,
                      int executable,struct checked_file *out) {
    struct stat named,opened;
    if (frontend_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||!S_ISREG(named.st_mode)||
        named.st_size<=0||(uint64_t)named.st_size!=expected->bytes||named.st_nlink!=1||
        (executable&&!(named.st_mode&0111))) return -1;
    int fd=frontend_openat(parent,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return -1;
    if (fstat(fd,&opened)||!same(&named,&opened)) { (void)close(fd);return -1; }
    out->fd=fd;out->stat=opened;
    return 0;
}
static int file_hash(const struct checked_file *file,const struct bd_frontend_file *expected,
                      uint8_t *capture,size_t capacity) {
    if (capture&&expected->bytes>capacity) return -1;
    uint8_t buffer[HASH_CHUNK],hash[32];
    uint64_t at=0;
    size_t calls=0,maximum=(size_t)((expected->bytes+HASH_CHUNK-1)/HASH_CHUNK)+32;
    unsigned interrupted=0;
    struct cp_sha sha;
    cp_sha_init(&sha);
    while (at<expected->bytes) {
        if (++calls>maximum) return -1;
        size_t count=expected->bytes-at>sizeof buffer?sizeof buffer:(size_t)(expected->bytes-at);
        ssize_t got=pread(file->fd,buffer,count,(off_t)at);
        if (got<0&&errno==EINTR) { if (++interrupted>8) return -1;continue; }
        if (got<=0||(size_t)got>count) return -1;
        cp_sha_update(&sha,buffer,(size_t)got);
        if (capture) memcpy(capture+(size_t)at,buffer,(size_t)got);
        at+=(uint64_t)got;
    }
    cp_sha_final(&sha,hash);
    return memcmp(hash,expected->sha256,32)?-1:0;
}
static int no_image_link(int app) {
    struct stat stat;
    static const char *const names[]={"mount_img.lnk","mount_image.lnk"};
    for(size_t i=0;i<sizeof names/sizeof names[0];i++)
        if(!frontend_fstatat(app,names[i],&stat,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
    return 0;
}
static int file_still(const struct checked_file *file,int parent,const char *name) {
    struct stat opened,named;
    return fstat(file->fd,&opened)||frontend_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||
        !same(&file->stat,&opened)||!same(&opened,&named)?-1:0;
}
static int directory_still(const struct checked_directory *dir,int parent,const char *name) {
    struct stat opened,named;
    return fstat(dir->fd,&opened)||frontend_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||
        !same(&dir->stat,&opened)||!same(&opened,&named)?-1:0;
}
static int root_still(const struct checked_directory *dir,const char *path) {
    struct stat opened,named;
    return fstat(dir->fd,&opened)||canonical_directory(path,&named)||
        !same(&dir->stat,&opened)||!same(&opened,&named)?-1:0;
}
static int parameter_valid(const uint8_t *bytes,size_t size) {
    char title[10];
    return bw_source_param_title(bytes,size,title)||strcmp(title,BD_FRONTEND_TITLE)?-1:0;
}
static int source_check(const struct bd_frontend_owner *owner) {
    struct checked_directory dirs[DIR_COUNT];
    struct checked_file files[FILE_COUNT];
    for (size_t i=0;i<DIR_COUNT;i++) dirs[i].fd=-1;
    for (size_t i=0;i<FILE_COUNT;i++) files[i].fd=-1;
    uint8_t *param=malloc((size_t)owner->expected.param.bytes),link[BD_FRONTEND_LINK_LIMIT];
    int result=-1;
    if (!param) goto done;
    if (root_open(owner->source,&dirs[DIR_SOURCE])||root_open(owner->registered,&dirs[DIR_REGISTERED])||
        child_open(dirs[DIR_SOURCE].fd,"sce_sys",&dirs[DIR_SOURCE_SYS])||
        child_open(dirs[DIR_REGISTERED].fd,"sce_sys",&dirs[DIR_REGISTERED_SYS])||
        no_image_link(dirs[DIR_REGISTERED].fd)||
        file_open(dirs[DIR_SOURCE].fd,"eboot.bin",&owner->expected.eboot,1,&files[FILE_BOOT])||
        file_open(dirs[DIR_SOURCE_SYS].fd,"param.json",&owner->expected.param,0,&files[FILE_SOURCE_PARAM])||
        file_open(dirs[DIR_REGISTERED].fd,"mount.lnk",&owner->expected.mount_link,0,&files[FILE_LINK])||
        file_open(dirs[DIR_REGISTERED_SYS].fd,"param.json",&owner->expected.param,0,&files[FILE_REGISTERED_PARAM])) goto done;
    if (file_hash(&files[FILE_BOOT],&owner->expected.eboot,NULL,0)||
        file_hash(&files[FILE_SOURCE_PARAM],&owner->expected.param,param,(size_t)owner->expected.param.bytes)||
        parameter_valid(param,(size_t)owner->expected.param.bytes)||
        file_hash(&files[FILE_REGISTERED_PARAM],&owner->expected.param,param,(size_t)owner->expected.param.bytes)||
        parameter_valid(param,(size_t)owner->expected.param.bytes)||
        file_hash(&files[FILE_LINK],&owner->expected.mount_link,link,sizeof link)) goto done;
    size_t link_size=(size_t)owner->expected.mount_link.bytes;
    if (link_size&&link[link_size-1]=='\n') {
        link_size--;
        if (link_size&&link[link_size-1]=='\r') link_size--;
    }
    if (link_size!=strlen(owner->source)||memcmp(link,owner->source,link_size)||
        file_still(&files[FILE_BOOT],dirs[DIR_SOURCE].fd,"eboot.bin")||
        file_still(&files[FILE_SOURCE_PARAM],dirs[DIR_SOURCE_SYS].fd,"param.json")||
        file_still(&files[FILE_REGISTERED_PARAM],dirs[DIR_REGISTERED_SYS].fd,"param.json")||
        file_still(&files[FILE_LINK],dirs[DIR_REGISTERED].fd,"mount.lnk")||
        directory_still(&dirs[DIR_SOURCE_SYS],dirs[DIR_SOURCE].fd,"sce_sys")||
        directory_still(&dirs[DIR_REGISTERED_SYS],dirs[DIR_REGISTERED].fd,"sce_sys")||
        root_still(&dirs[DIR_SOURCE],owner->source)||root_still(&dirs[DIR_REGISTERED],owner->registered)||
        no_image_link(dirs[DIR_REGISTERED].fd)) goto done;
    result=0;
done:
    for (size_t i=0;i<FILE_COUNT;i++) if (files[i].fd>=0&&close(files[i].fd)) result=-1;
    for (size_t i=0;i<DIR_COUNT;i++) if (dirs[i].fd>=0&&close(dirs[i].fd)) result=-1;
    free(param);
    return result;
}
static int owner_valid(const struct bd_frontend_owner *owner) {
    if (!owner||owner->initialized!=1||owner->admitted>1||!expected_valid(&owner->expected)||
        !path_valid(owner->source)||!path_valid(owner->registered)) return 0;
#ifndef BD_FRONTEND_IDENTITY_HOST_TEST
    if (strcmp(owner->source,BD_FRONTEND_SOURCE)||strcmp(owner->registered,BD_FRONTEND_REGISTERED)) return 0;
#endif
    return strcmp(owner->source,owner->registered)!=0;
}
static int identity_get(const struct bd_frontend_owner *owner,struct bd_frontend_identity *out) {
    if (!owner_valid(owner)||owner->admitted!=1||owner->original.firmware!=BW_STOP_PROFILE_FW||
        bw_stop_identity_admit(&owner->original.process,owner->original.admitted_wall_us)||
        memcmp(owner->original.title,BD_FRONTEND_TITLE,sizeof owner->original.title)) return -1;
    struct bd_frontend_identity expected={0};
    expected.pid=owner->original.process.pid;
    expected.app_id=owner->original.app_id;
    expected.birth_sec=(uint64_t)owner->original.process.start_sec;
    expected.birth_usec=(uint32_t)owner->original.process.start_usec;
    memcpy(expected.title_id,BD_FRONTEND_TITLE,sizeof expected.title_id);
    package_fingerprint(&owner->expected,expected.package_fingerprint);
    if (expected.app_id==0||expected.app_id>INT32_MAX||
        expected.pid!=owner->identity.pid||expected.app_id!=owner->identity.app_id||
        expected.birth_sec!=owner->identity.birth_sec||expected.birth_usec!=owner->identity.birth_usec||
        memcmp(expected.title_id,owner->identity.title_id,sizeof expected.title_id)||
        memcmp(expected.package_fingerprint,owner->identity.package_fingerprint,32)) return -1;
    *out=expected;
    return 0;
}
int bd_frontend_get(const struct bd_frontend_owner *owner,struct bd_frontend_identity *out) {
    if (!out) return -1;
    struct bd_frontend_identity held={0};
    int result=identity_get(owner,&held);
    memset(out,0,sizeof *out);
    if (!result) *out=held;
    return result;
}
static int identity_current(const struct rc_game_identity_ops *ops,const struct rc_game_identity *prior,
                             struct rc_game_identity *out) {
    return ops?rc_game_identity_current_with_ops(ops,prior,out):rc_game_identity_current(prior,out);
}
static int current(const struct bd_frontend_owner *owner,const struct rc_game_identity_ops *ops) {
    struct bd_frontend_identity expected;
    if (identity_get(owner,&expected)) return RC_GAME_UNKNOWN;
    struct rc_game_identity observed;
    int first=identity_current(ops,&owner->original,&observed);
    if (first!=RC_GAME_QUALIFIED&&first!=RC_GAME_ABSENT) return RC_GAME_UNKNOWN;
    if (source_check(owner)) return RC_GAME_UNKNOWN;
    int last=identity_current(ops,&owner->original,&observed);
    return first==last?last:RC_GAME_UNKNOWN;
}
int bd_frontend_current(const struct bd_frontend_owner *owner) { return current(owner,NULL); }
int bd_frontend_current_with_ops(const struct bd_frontend_owner *owner,const struct rc_game_identity_ops *ops) {
    return ops?current(owner,ops):RC_GAME_UNKNOWN;
}
struct bd_frontend_preparation {
    struct bd_frontend_owner owner;
    const struct rc_game_identity_ops *ops;
    struct checked_directory dirs[DIR_COUNT];
    struct checked_file files[FILE_COUNT];
    unsigned poisoned;
};
static int preparation_same(const struct stat *a,const struct stat *b){
    return same(a,b)&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
static int preparation_source_current(const struct bd_frontend_preparation *p){
    const struct checked_directory *d=p->dirs;const struct checked_file *f=p->files;
    const int parents[]={d[DIR_SOURCE].fd,d[DIR_SOURCE_SYS].fd,d[DIR_REGISTERED].fd,d[DIR_REGISTERED_SYS].fd};
    static const char *const names[]={"eboot.bin","param.json","mount.lnk","param.json"};
    for(size_t i=0;i<FILE_COUNT;i++){
        struct stat held,named;
        if(fstat(f[i].fd,&held)||frontend_fstatat(parents[i],names[i],&named,AT_SYMLINK_NOFOLLOW)||
           !preparation_same(&f[i].stat,&held)||!preparation_same(&held,&named))return -1;
    }
    if(directory_still(&d[DIR_SOURCE_SYS],d[DIR_SOURCE].fd,"sce_sys")||
       directory_still(&d[DIR_REGISTERED_SYS],d[DIR_REGISTERED].fd,"sce_sys")||
       root_still(&d[DIR_SOURCE],p->owner.source)||root_still(&d[DIR_REGISTERED],p->owner.registered)||
       no_image_link(d[DIR_REGISTERED].fd))return -1;
    for(size_t i=0;i<DIR_COUNT;i++){struct stat held;if(fstat(d[i].fd,&held)||!preparation_same(&d[i].stat,&held))return -1;}
    return 0;
}
static int preparation_process_current(const struct bd_frontend_preparation *p){
    struct bd_frontend_identity identity;struct rc_game_identity observed;
    return bd_frontend_get(&p->owner,&identity)||identity_current(p->ops,&p->owner.original,&observed)!=RC_GAME_QUALIFIED?-1:0;
}
int bd_frontend_preparation_current(struct bd_frontend_preparation *p){
    if(!p||p->poisoned)return -1;
    if(preparation_process_current(p)||preparation_source_current(p)||preparation_process_current(p)){p->poisoned=1;return -1;}
    return 0;
}
static int preparation_hash(struct bd_frontend_preparation *p){
    const struct bd_frontend_package *expected=&p->owner.expected;
    uint8_t *param=malloc((size_t)expected->param.bytes),link[BD_FRONTEND_LINK_LIMIT];int bad=1;
    if(!param)return -1;
    if(file_hash(&p->files[FILE_BOOT],&expected->eboot,NULL,0)||
       file_hash(&p->files[FILE_SOURCE_PARAM],&expected->param,param,(size_t)expected->param.bytes)||parameter_valid(param,(size_t)expected->param.bytes)||
       file_hash(&p->files[FILE_REGISTERED_PARAM],&expected->param,param,(size_t)expected->param.bytes)||parameter_valid(param,(size_t)expected->param.bytes)||
       file_hash(&p->files[FILE_LINK],&expected->mount_link,link,sizeof link))goto done;
    size_t n=(size_t)expected->mount_link.bytes;if(n&&link[n-1]=='\n'){n--;if(n&&link[n-1]=='\r')n--;}
    if(n!=strlen(p->owner.source)||memcmp(link,p->owner.source,n))goto done;bad=0;
done:free(param);return bad?-1:0;
}
int bd_frontend_preparation_control(struct bd_frontend_preparation *p){
    if(bd_frontend_preparation_current(p))return -1;
    if(preparation_hash(p)||bd_frontend_preparation_current(p)){p->poisoned=1;return -1;}return 0;
}
int bd_frontend_preparation_close(struct bd_frontend_preparation *p){
    if(!p)return 0;int bad=0;
    for(size_t i=0;i<FILE_COUNT;i++)if(p->files[i].fd>=0){if(close(p->files[i].fd))bad=1;p->files[i].fd=-1;}
    for(size_t i=0;i<DIR_COUNT;i++)if(p->dirs[i].fd>=0){if(close(p->dirs[i].fd))bad=1;p->dirs[i].fd=-1;}
    memset(p,0,sizeof *p);free(p);return bad?-1:0;
}
static int preparation_open(const struct bd_frontend_owner *owner,const struct rc_game_identity_ops *ops,struct bd_frontend_preparation **out){
    if(out)*out=NULL;struct bd_frontend_identity identity;
    if(!out||bd_frontend_get(owner,&identity))return -1;
    struct bd_frontend_preparation *p=calloc(1,sizeof *p);if(!p)return -1;p->owner=*owner;p->ops=ops;
    for(size_t i=0;i<DIR_COUNT;i++)p->dirs[i].fd=-1;for(size_t i=0;i<FILE_COUNT;i++)p->files[i].fd=-1;
    const struct bd_frontend_package *expected=&owner->expected;struct checked_directory *d=p->dirs;struct checked_file *f=p->files;
    if(preparation_process_current(p)||root_open(owner->source,&d[DIR_SOURCE])||root_open(owner->registered,&d[DIR_REGISTERED])||
       child_open(d[DIR_SOURCE].fd,"sce_sys",&d[DIR_SOURCE_SYS])||child_open(d[DIR_REGISTERED].fd,"sce_sys",&d[DIR_REGISTERED_SYS])||
       no_image_link(d[DIR_REGISTERED].fd)||file_open(d[DIR_SOURCE].fd,"eboot.bin",&expected->eboot,1,&f[FILE_BOOT])||
       file_open(d[DIR_SOURCE_SYS].fd,"param.json",&expected->param,0,&f[FILE_SOURCE_PARAM])||
       file_open(d[DIR_REGISTERED].fd,"mount.lnk",&expected->mount_link,0,&f[FILE_LINK])||
       file_open(d[DIR_REGISTERED_SYS].fd,"param.json",&expected->param,0,&f[FILE_REGISTERED_PARAM])||
       bd_frontend_preparation_control(p)){(void)bd_frontend_preparation_close(p);return -1;}
    *out=p;return 0;
}
int bd_frontend_preparation_open(const struct bd_frontend_owner *owner,struct bd_frontend_preparation **out){return preparation_open(owner,NULL,out);}
int bd_frontend_preparation_open_with_ops(const struct bd_frontend_owner *owner,const struct rc_game_identity_ops *ops,struct bd_frontend_preparation **out){
    if(!ops){if(out)*out=NULL;return -1;}return preparation_open(owner,ops,out);
}
static int admit(struct bd_frontend_owner *owner,int32_t pid,uint32_t app_id,
                   const struct rc_game_identity_ops *ops) {
    if (!owner_valid(owner)||pid<=1||app_id==0||app_id>INT32_MAX) return RC_GAME_UNKNOWN;
    if (owner->admitted) {
        if (pid!=owner->identity.pid||app_id!=owner->identity.app_id) return RC_GAME_UNKNOWN;
        return current(owner,ops);
    }
    struct rc_game_selection selection={pid,app_id,BD_FRONTEND_TITLE};
    struct rc_game_identity captured,after;
    int initial=ops?rc_game_identity_admit_with_ops(ops,&selection,&captured):
                    rc_game_identity_admit(&selection,&captured);
    if (initial!=RC_GAME_QUALIFIED) return initial;
    if (source_check(owner)||identity_current(ops,&captured,&after)!=RC_GAME_QUALIFIED) return RC_GAME_UNKNOWN;
    struct bd_frontend_identity identity={0};
    identity.pid=captured.process.pid;identity.app_id=captured.app_id;
    identity.birth_sec=(uint64_t)captured.process.start_sec;
    identity.birth_usec=(uint32_t)captured.process.start_usec;
    memcpy(identity.title_id,BD_FRONTEND_TITLE,sizeof identity.title_id);
    package_fingerprint(&owner->expected,identity.package_fingerprint);
    owner->original=captured;
    owner->identity=identity;
    owner->admitted=1;
    return RC_GAME_QUALIFIED;
}
int bd_frontend_admit(struct bd_frontend_owner *owner,int32_t pid,uint32_t app_id) {
    return admit(owner,pid,app_id,NULL);
}
int bd_frontend_admit_with_ops(struct bd_frontend_owner *owner,int32_t pid,uint32_t app_id,
                               const struct rc_game_identity_ops *ops) {
    return ops?admit(owner,pid,app_id,ops):RC_GAME_UNKNOWN;
}
