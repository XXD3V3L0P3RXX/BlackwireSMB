#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_NATIVE_SETUP_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
#include "native_setup.h"
#include "launch_timing.h"
#include "native_json.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <sys/file.h>
static int same(const struct stat *a,const struct stat *b){
    return S_ISREG(b->st_mode)&&b->st_nlink==1&&v2_identity(a,b)&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
int bw_native_setup_config_valid(const struct bw_share_config *p){
    if(!p)return 0;const char *values[]={p->server,p->share,p->user,p->password};const size_t limits[]={sizeof p->server,sizeof p->share,sizeof p->user,sizeof p->password};
    for(unsigned i=0;i<4;i++){size_t n=strnlen(values[i],limits[i]);if(n==limits[i]||!bw_json_utf8(values[i]))return 0;
        for(size_t j=0;j<n;j++)if((unsigned char)values[i][j]<32||values[i][j]==127)return 0;}
    return *p->server&&*p->share&&!strpbrk(p->server,"/\\")&&!strpbrk(p->share,"/\\:");
}
static int root_open(const char *base,struct prepare_v2 *anchor){
    memset(anchor,0,sizeof *anchor);if(!base||v2_copy(anchor->root,sizeof anchor->root,base))return -1;
    int fd=open(base,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd>=0&&!v2_root_current(anchor,fd)){close(fd);fd=-1;}return fd;
}
static int read_at(int root,const struct prepare_v2 *anchor,struct bw_share_config *config,struct bw_setup_generation *generation,char bytes[4096]){
    bw_share_clear(config);memset(generation,0,sizeof *generation);struct stat directory,opened,named,after;int fd=-1,result=BW_SETUP_UNSAFE;
    if(!v2_root_current(anchor,root)||fstat(root,&directory))goto done;
    generation->root_device=directory.st_dev;generation->root_inode=directory.st_ino;
    fd=v2_openat(root,"share.conf",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0){
        if(errno==ENOENT&&cp_fstatat(root,"share.conf",&named,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT&&v2_root_current(anchor,root))result=BW_SETUP_MISSING;
        goto done;
    }
    if(fstat(fd,&opened)||!S_ISREG(opened.st_mode)||opened.st_nlink!=1||opened.st_size<0||opened.st_size>4096||
        cp_fstatat(root,"share.conf",&named,AT_SYMLINK_NOFOLLOW)||!same(&opened,&named))goto done;
    if(v2_read_all(fd,bytes,(size_t)opened.st_size))goto done;
    char extra;if(read(fd,&extra,1)!=0||fstat(fd,&after)||!same(&opened,&after)||cp_fstatat(root,"share.conf",&after,AT_SYMLINK_NOFOLLOW)||!same(&opened,&after)||!v2_root_current(anchor,root))goto done;
    generation->present=1;generation->file=opened;cp_sha_bytes(bytes,(size_t)opened.st_size,generation->sha256);
    result=bw_share_parse(config,bytes,(size_t)opened.st_size)||!bw_native_setup_config_valid(config)?BW_SETUP_INVALID:BW_SETUP_VALID;
done:
    if(fd>=0&&close(fd))result=BW_SETUP_UNSAFE;
    if(result!=BW_SETUP_VALID)bw_share_clear(config);if(result<0)memset(generation,0,sizeof *generation);return result;
}
int bw_native_setup_read(const char *base,struct bw_share_config *config,struct bw_setup_generation *generation){
    BW_LT_COUNT(BW_LT_SETTINGS_READS,1);
    if(!config||!generation)return BW_SETUP_UNSAFE;bw_share_clear(config);memset(generation,0,sizeof *generation);
    struct prepare_v2 anchor;int root=root_open(base,&anchor);if(root<0)return BW_SETUP_UNSAFE;char bytes[4096]={0};
    int result=read_at(root,&anchor,config,generation,bytes);bw_native_sensitive_clear(bytes,sizeof bytes);
    if(close(root)){result=BW_SETUP_UNSAFE;bw_share_clear(config);memset(generation,0,sizeof *generation);}return result;
}
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){
    return a&&b&&a->root_device==b->root_device&&a->root_inode==b->root_inode&&a->present==b->present&&
        (!a->present||(same(&a->file,&b->file)&&!memcmp(a->sha256,b->sha256,32)));
}
static int write_all(int fd,const void *bytes,size_t size){
    size_t at=0;while(at<size){ssize_t n=write(fd,(const char *)bytes+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;at+=(size_t)n;}return 0;
}
static int regular(int parent,const char *leaf,int fd,size_t size,struct stat *out){
    struct stat a,b;
    if(fstat(fd,&a)||!S_ISREG(a.st_mode)||a.st_nlink!=1||a.st_size<0||(uint64_t)a.st_size!=size||
        cp_fstatat(parent,leaf,&b,AT_SYMLINK_NOFOLLOW)||!same(&a,&b))return -1;
    if(out)*out=a;return 0;
}
static int verify(int root,const char *leaf,int fd,const char *bytes,size_t size){
    char checked[4096];if(size>sizeof checked||regular(root,leaf,fd,size,NULL))return -1;
    size_t at=0;int result=-1;while(at<size){ssize_t n=pread(fd,checked+at,size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;at+=(size_t)n;}
    result=memcmp(checked,bytes,size)||regular(root,leaf,fd,size,NULL)?-1:0;
done:
    bw_native_sensitive_clear(checked,sizeof checked);return result;
}
static int lock_open(int root){
    static const char marker[]="Blackwire private settings lock v1\n";const char *leaf=".blackwire-settings.lock";
    int fd=v2_openat(root,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600),created=fd>=0;
    if(fd<0&&errno==EEXIST)fd=v2_openat(root,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;
    if((created&&(fchmod(fd,0600)||write_all(fd,marker,sizeof marker-1)||fsync(fd)))||
        verify(root,leaf,fd,marker,sizeof marker-1)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}
    return fd;
}
int bw_native_setup_store(const char *base,const struct bw_share_config *next,const struct bw_setup_generation *expected,const char *nonce,int (*idle)(void *),void *opaque,struct bw_setup_saved *saved){
    if(saved)memset(saved,0,sizeof *saved);
    if(!saved||!expected||!idle||!nonce||strlen(nonce)!=32||!bw_native_setup_config_valid(next))return BW_SETUP_SAVE_UNSAFE;
    for(unsigned i=0;i<32;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return BW_SETUP_SAVE_UNSAFE;
    char encoded[1024],before[4096]={0},checked[4096]={0},temp[96];struct bw_share_config prior={0},after={0};struct bw_setup_generation current={0},again={0};struct prepare_v2 anchor;
    int root=-1,lock=-1,backup=-1,fd=-1,final=-1,result=BW_SETUP_SAVE_IO;
    int n=snprintf(encoded,sizeof encoded,"server=%s\nshare=%s\nuser=%s\npassword=%s\npath=\n",next->server,next->share,next->user,next->password);
    if(n<0||(size_t)n>=sizeof encoded)goto done;root=root_open(base,&anchor);if(root<0){result=BW_SETUP_SAVE_UNSAFE;goto done;}
    int status=read_at(root,&anchor,&prior,&current,before);
    if(status<0){result=BW_SETUP_SAVE_UNSAFE;goto done;}if(!bw_native_setup_generation_equal(expected,&current)){result=BW_SETUP_SAVE_STALE;goto done;}
    if(idle(opaque)){result=BW_SETUP_SAVE_BUSY;goto done;}
    lock=lock_open(root);if(lock<0)goto done;
    if(!v2_root_current(&anchor,root))goto done;
    snprintf(temp,sizeof temp,".blackwire-share-%s.tmp",nonce);
    if(current.present){
        snprintf(saved->backup,sizeof saved->backup,"share.conf.before-%s.bak",nonce);
        backup=v2_openat(root,saved->backup,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
        if(backup<0||fchmod(backup,0600)||write_all(backup,before,(size_t)current.file.st_size)||fsync(backup)||verify(root,saved->backup,backup,before,(size_t)current.file.st_size))goto done;
    }
    fd=v2_openat(root,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
    if(fd<0||fchmod(fd,0600)||write_all(fd,encoded,(size_t)n)||fsync(fd)||verify(root,temp,fd,encoded,(size_t)n)||fsync(root))goto done;
    if(idle(opaque)){result=BW_SETUP_SAVE_BUSY;goto done;}
    status=read_at(root,&anchor,&after,&again,checked);if(status<0){result=BW_SETUP_SAVE_UNSAFE;goto done;}
    if(!bw_native_setup_generation_equal(expected,&again)){result=BW_SETUP_SAVE_STALE;goto done;}
    if(!v2_root_current(&anchor,root))goto done;
    if(!current.present){
        /* The console filesystem does not support hard links. Exclusively
         * reserve a new final inode and copy already verified small settings.
         * This is not atomic complete-file publication: after reservation,
         * any failure is unconfirmed and the partial file is preserved. */
        final=v2_openat(root,"share.conf",O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
        if(final<0){result=errno==EEXIST?BW_SETUP_SAVE_STALE:BW_SETUP_SAVE_IO;goto done;}
        saved->published=1;result=BW_SETUP_SAVE_UNCONFIRMED;
        if(fchmod(final,0600)||idle(opaque)||!v2_root_current(&anchor,root)||verify(root,"share.conf",final,"",0)||write_all(final,encoded,(size_t)n)||fsync(final)||
            verify(root,"share.conf",final,encoded,(size_t)n)||verify(root,temp,fd,encoded,(size_t)n)||cp_unlinkat(root,temp))goto done;
        int old=fd;fd=final;final=-1;if(close(old))goto done;
    }else{
        if(cp_renameat(root,temp,root,"share.conf"))goto done;saved->published=1;
    }
    if(!v2_root_current(&anchor,root)||verify(root,"share.conf",fd,encoded,(size_t)n)||fsync(root)){result=BW_SETUP_SAVE_UNCONFIRMED;goto done;}
    saved->verified=1;result=BW_SETUP_SAVE_OK;
done:
    bw_share_clear(&prior);bw_share_clear(&after);bw_native_sensitive_clear(encoded,sizeof encoded);bw_native_sensitive_clear(before,sizeof before);bw_native_sensitive_clear(checked,sizeof checked);
    if(fd>=0&&close(fd)&&saved->published)result=BW_SETUP_SAVE_UNCONFIRMED;
    if(final>=0&&close(final)&&saved->published)result=BW_SETUP_SAVE_UNCONFIRMED;
    if(backup>=0&&close(backup))result=saved->published?BW_SETUP_SAVE_UNCONFIRMED:BW_SETUP_SAVE_IO;
    if(lock>=0)close(lock);if(root>=0&&close(root)&&saved->published)result=BW_SETUP_SAVE_UNCONFIRMED;
    if(result!=BW_SETUP_SAVE_OK)saved->verified=0;return result;
}
int bw_native_setup_idle(void *opaque){(void)opaque;return v2_games_stopped("/data/shadowmount/debug.log");}
int bw_native_setup_nonce(char out[33]){
    unsigned char bytes[16];arc4random_buf(bytes,sizeof bytes);unsigned nonzero=0;
    for(unsigned i=0;i<sizeof bytes;i++){nonzero|=bytes[i];snprintf(out+2*i,3,"%02x",bytes[i]);}bw_native_sensitive_clear(bytes,sizeof bytes);
    if(!nonzero){memset(out,0,33);return -1;}return 0;
}
