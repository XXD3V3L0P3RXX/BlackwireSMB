#include "share_config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void clear(void *data,size_t size) { volatile unsigned char *p=data;while (size--) *p++=0; }
void bw_share_clear(struct bw_share_config *p) { if (p) clear(p,sizeof *p); }
static int share_same(const char *a,const char *b) {
    while (*a && *b) {
        unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if (x>='A' && x<='Z') x+=32;if (y>='A' && y<='Z') y+=32;
        if (x!=y) return 0;
    }
    return !*a && !*b;
}
int bw_share_select(struct bw_share_config *out,const struct bw_share_config *configured,const char *share) {
    if (!out || !configured) return -1;
    struct bw_share_config selected={0};int result=-1;
    if (!memchr(configured->server,0,sizeof configured->server) ||
        !memchr(configured->share,0,sizeof configured->share) ||
        !memchr(configured->share2,0,sizeof configured->share2) ||
        !memchr(configured->user,0,sizeof configured->user) ||
        !memchr(configured->password,0,sizeof configured->password) ||
        !*configured->server || !*configured->share ||
        strpbrk(configured->server,"/\\") || strpbrk(configured->share,"/\\:") ||
        strpbrk(configured->share2,"/\\:") ||
        (*configured->share2 && share_same(configured->share,configured->share2))) goto done;
    const char *chosen=share && *share?share:configured->share;
    if (strcmp(chosen,configured->share) && (!*configured->share2 || strcmp(chosen,configured->share2))) goto done;
    selected=*configured;memset(selected.share,0,sizeof selected.share);
    memcpy(selected.share,chosen,strlen(chosen)+1);memset(selected.share2,0,sizeof selected.share2);result=0;
done:
    bw_share_clear(out);if (!result) *out=selected;bw_share_clear(&selected);return result;
}
static int copy(char *out,size_t capacity,const char *value,size_t length) {
    if (length>=capacity) return -1;
    for (size_t i=0;i<length;i++) if ((unsigned char)value[i]<32 || value[i]==127) return -1;
    memcpy(out,value,length);out[length]=0;return 0;
}
int bw_share_parse(struct bw_share_config *out,const char *data,size_t size) {
    if (!out) return -1;
    bw_share_clear(out);
    if (!data || !size || size>4096 || memchr(data,0,size)) return -1;
    unsigned seen=0;
    for (size_t at=0;at<size;) {
        size_t end=at;while (end<size && data[end]!='\n') end++;
        size_t length=end-at;
        if (length && data[at+length-1]=='\r') length--;
        const char *line=data+at;
        size_t key=0;while (key<length && line[key]!='=') key++;
        char *target=NULL;size_t capacity=0;unsigned bit=0;
        if (key<length) {
            if (key==6 && !memcmp(line,"server",6)) { target=out->server;capacity=sizeof out->server;bit=1; }
            if (key==5 && !memcmp(line,"share",5)) { target=out->share;capacity=sizeof out->share;bit=2; }
            if (key==6 && !memcmp(line,"share2",6)) { target=out->share2;capacity=sizeof out->share2;bit=32; }
            if (key==4 && !memcmp(line,"user",4)) { target=out->user;capacity=sizeof out->user;bit=4; }
            if (key==8 && !memcmp(line,"password",8)) { target=out->password;capacity=sizeof out->password;bit=8; }
            if (key==4 && !memcmp(line,"path",4)) {
                if ((seen&16) || (length-key-1 && !(length-key-1==1 && line[key+1]=='/'))) goto bad;
                seen|=16;
            }
            if (target) {
                if ((seen&bit) || copy(target,capacity,line+key+1,length-key-1)) goto bad;
                seen|=bit;
            }
        }
        at=end<size?end+1:end;
    }
    if ((seen&7)!=7 || !*out->server || !*out->share ||
        strpbrk(out->server,"/\\\r\n") || strpbrk(out->share,"/\\:") ||
        strpbrk(out->share2,"/\\:") || (*out->share2 && share_same(out->share,out->share2))) goto bad;
    return 0;
bad:
    bw_share_clear(out);return -1;
}
static int same(const struct stat *a,const struct stat *b) {
    return S_ISREG(b->st_mode) && b->st_nlink==1 && a->st_dev==b->st_dev && a->st_ino==b->st_ino &&
        a->st_size==b->st_size && a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
int bw_share_load(struct bw_share_config *out) {
    const char *base="/data/blackwire",*path="/data/blackwire/share.conf";
    char resolved[4096],data[4096];struct stat root,root_after,named,opened,after;
    int result=-1,fd=-1;
    if (!out) return -1;
    bw_share_clear(out);
    if (!realpath(base,resolved) || strcmp(resolved,base) || lstat(base,&root) || !S_ISDIR(root.st_mode) ||
        lstat(path,&named) || !S_ISREG(named.st_mode) || named.st_nlink!=1 || named.st_size<1 || named.st_size>4096) goto done;
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0 || fstat(fd,&opened) || !same(&named,&opened)) goto done;
    for (size_t at=0;at<(size_t)opened.st_size;) {
        ssize_t got=read(fd,data+at,(size_t)opened.st_size-at);
        if (got<0 && errno==EINTR) continue;
        if (got<=0 || (size_t)got>(size_t)opened.st_size-at) goto done;
        at+=(size_t)got;
    }
    char extra;
    if (read(fd,&extra,1)!=0 || fstat(fd,&after) || !same(&opened,&after) || lstat(path,&after) || !same(&opened,&after) ||
        lstat(base,&root_after) || !S_ISDIR(root_after.st_mode) || root_after.st_dev!=root.st_dev || root_after.st_ino!=root.st_ino) goto done;
    result=bw_share_parse(out,data,(size_t)opened.st_size);
done:
    clear(data,sizeof data);
    if (fd>=0 && close(fd)) result=-1;
    if (result) bw_share_clear(out);
    return result;
}
