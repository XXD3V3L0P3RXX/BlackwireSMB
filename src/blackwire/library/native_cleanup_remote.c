/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_cleanup_remote.h"
#include "native_document.h"
#include "source_param.h"
#include "send_diagnostic.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/container_source.h"
#include "../streamreader/copy_sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define NRC_REMOTE_ITEMS 4096u
#define NRC_IMAGE_BYTES (64ULL*1024u*1024u)
#define NRC_OBSERVATION_BYTES (256ULL*1024u*1024u)
#define NRC_STATUS_BYTES (1024u*1024u)
struct status_snapshot {unsigned seen,present,images_seen,images;struct stat parent,file;unsigned char hash[32],image_hash[32];};
enum observation { NRC_STRICT,NRC_REGISTRATION,NRC_ABSENT,NRC_PREPARE_ABSENT,NRC_PREPARE_REGISTERED };
static int preparing(unsigned policy){return policy==NRC_PREPARE_ABSENT||policy==NRC_PREPARE_REGISTERED;}
struct collection {
    struct bw_nrc_remote *remote;
    const struct bw_nrc_subject *subject;
    unsigned seen,count,declared,owned_rows,policy;
    struct status_snapshot *status;
    struct cp_sha images;
    unsigned image_count;
};
static int current(struct bw_nrc_remote *r){return !r||!r->connection.current||r->connection.current(r->connection.opaque)?-1:0;}
static int fail(struct bw_nrc_remote *r,const char *reason){if(r)r->reason=reason;return -1;}
static int decimal(const void *raw,size_t size,unsigned maximum,unsigned *out){
    const unsigned char *p=raw;unsigned n=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>maximum||n>(maximum-d)/10)return -1;n=n*10+d;}*out=n;return 0;
}
static int boolean(const void *raw,size_t size,unsigned *out){
    if(size==4&&!memcmp(raw,"true",4)){*out=1;return 0;}if(size==5&&!memcmp(raw,"false",5)){*out=0;return 0;}return -1;
}
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
        a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int literal_path(const char *path){
    if(!path||path[0]!='/'||strnlen(path,BW_NRC_PATH_BYTES)>=BW_NRC_PATH_BYTES||!path[1])return -1;
    for(const char *p=path+1;*p;){const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        if(!n||(n==1&&*p=='.')||(n==2&&!memcmp(p,"..",2)))return -1;
        for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return -1;
        if(!end)break;p=end+1;if(!*p)return -1;
    }return 0;
}
static int subject_shape(const struct bw_nrc_subject *s){
    if(!s||strnlen(s->title,10)!=9||memcmp(s->title,"PPSA",4)||!s->content[0]||
       !memchr(s->content,0,sizeof s->content)||literal_path(s->source))return -1;
    for(unsigned i=4;i<9;i++)if(s->title[i]<'0'||s->title[i]>'9')return -1;return 0;
}
static int observation_charge(struct bw_nrc_remote *r,size_t size){
    if(r->local_observation_bytes>NRC_OBSERVATION_BYTES||size>NRC_OBSERVATION_BYTES-r->local_observation_bytes){r->substage="manual_observation_budget";r->observation_error=0;return -1;}
    r->local_observation_bytes+=size;return 0;
}
struct local_image {struct bw_nrc_remote *remote;const char *path;int fd,failed,error;struct stat admitted;uint64_t bytes;const char *stage;};
static int image_current(void *opaque){
    struct local_image *p=opaque;struct stat held,named;char real[PATH_MAX];
    if(p->failed)return -1;
    if(current(p->remote)){p->failed=1;p->stage="manual_image_authority";return -1;}
    if(fstat(p->fd,&held)||lstat(p->path,&named)||!realpath(p->path,real)){
        p->failed=1;p->error=errno;p->stage="manual_image_generation_stat";return -1;
    }
    if(!S_ISREG(held.st_mode)||strcmp(real,p->path)||!same(&p->admitted,&held)||!same(&held,&named)){
        p->failed=1;p->stage="manual_image_generation_changed";return -1;
    }return 0;
}
static int image_read(void *opaque,uint64_t at,void *out,size_t size){
    struct local_image *p=opaque;
    if(p->failed||current(p->remote)||size>BW_PFSC_MAX_READ||at>(uint64_t)p->admitted.st_size||
       size>(uint64_t)p->admitted.st_size-at||at>INT64_MAX||size>(uint64_t)INT64_MAX-at||
       size>NRC_IMAGE_BYTES-p->bytes||observation_charge(p->remote,size)){
        p->failed=1;p->stage="manual_image_read_bounds_or_budget";return -1;
    }
    p->bytes+=size;
    for(size_t used=0;used<size;){
        if(current(p->remote)){p->failed=1;p->stage="manual_image_read_authority";return -1;}
        ssize_t got=pread(p->fd,(unsigned char *)out+used,size-used,(off_t)(at+used));
        if(got<0&&errno==EINTR)continue;
        if(got<=0||(size_t)got>size-used){p->failed=1;p->error=got<0?errno:0;p->stage="manual_image_read";return -1;}
        used+=(size_t)got;
    }
    if(current(p->remote)){p->failed=1;p->stage="manual_image_read_authority";return -1;}return 0;
}
struct image_param {struct bw_container *image;unsigned candidates,root;};
static int image_component(const struct bw_container *image,const char *bytes,size_t size,const char *ascii){
    int exfat=image->kind>=BW_CONTAINER_EXFAT&&image->kind<=BW_CONTAINER_WRAPPED_EXFAT;size_t at=0,n=0;
    while(at<size){uint32_t c=(unsigned char)bytes[at++];unsigned more=c<128?0:c<224?1:c<240?2:3;
        if(more){c&=(1u<<(6-more))-1;for(unsigned i=0;i<more;i++){if(at==size)return 0;c=(c<<6)|((unsigned char)bytes[at++]&63);}}
        if(!ascii[n])return 0;
        uint32_t a=(unsigned char)ascii[n++];if(a>='a'&&a<='z')a-=32;
        if(exfat){if(c>65535)return 0;c=image->exfat.upcase[c];}
        else if(c>='a'&&c<='z')c-=32;
        if(c!=a)return 0;
    }return !ascii[n];
}
static int image_param_visit(void *opaque,const char *path,const struct bw_pfs_inode *node){
    struct image_param *p=opaque;const char *last=strrchr(path,'/'),*parent=path;(void)node;
    if(!last)return 0;for(const char *at=path;at<last;at++)if(*at=='/')parent=at+1;
    if(image_component(p->image,parent,(size_t)(last-parent),"sce_sys")&&image_component(p->image,last+1,strlen(last+1),"param.json")){
        p->candidates++;if(!strcmp(path,"sce_sys/param.json"))p->root++;
    }return 0; /* Never accept provisional metadata before the complete walk. */
}
static int manual_image_title(struct bw_nrc_remote *r,const char *path,const struct stat *named,char title[10]){
    int fd=-1,rc=-1,error=0;const char *stage="manual_image_open";unsigned char *bytes=NULL;
    struct bw_container *image=NULL;struct local_image local={.remote=r,.path=path,.fd=-1,.admitted=*named};
    char parsed[10]={0},content[128];struct bw_container_file token={0};struct image_param found={0};
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0){error=errno;goto done;}local.fd=fd;
    if(!S_ISREG(named->st_mode)||named->st_nlink!=1||named->st_size<48||(uint64_t)named->st_size>BW_PFS_MAX_VOLUME){stage="manual_image_type_or_size";goto done;}
    if(image_current(&local))goto done;
    image=calloc(1,sizeof *image);if(!image){stage="manual_image_memory";goto done;}
    struct bw_container_ops ops={image_read,image_current};
    stage="manual_image_container_open";if(bw_container_open(image,&ops,&local,(uint64_t)named->st_size))goto done;
    found.image=image;stage="manual_image_namespace";if(bw_container_walk(image,image_param_visit,&found,NULL))goto done;
    stage="manual_image_unique_root_param";if(found.candidates!=1||found.root!=1)goto done;
    stage="manual_image_param_file";if(bw_container_file_open(image,"sce_sys/param.json",&token)||!token.size||token.size>BW_SOURCE_PARAM_LIMIT)goto done;
    bytes=malloc((size_t)token.size);if(!bytes){stage="manual_image_param_memory";goto done;}
    stage="manual_image_param_read";if(bw_container_file_read(&token,0,bytes,(size_t)token.size))goto done;
    stage="manual_image_param_parse";if(bw_source_param_identity(bytes,(size_t)token.size,parsed,content))goto done;
    stage="manual_image_final_generation";if(image_current(&local)||bw_container_validate(image))goto done;
    rc=0;
done:
    free(bytes);free(image);
    if(local.failed){stage=local.stage;error=local.error;rc=-1;}
    if(fd>=0&&close(fd)){stage="manual_image_close";error=errno;r->observation_release_unknown=1;rc=-1;}
    if(!rc&&current(r)){stage="manual_image_final_authority";error=0;rc=-1;}
    if(rc){memset(title,0,10);r->substage=stage;r->observation_error=error;}
    else memcpy(title,parsed,10);
    return rc;
}
static int status_space(unsigned char c){return c==' '||c=='\r'||c=='\v'||c=='\f';}
static char *status_trim(char *p){while(status_space((unsigned char)*p))p++;size_t n=strlen(p);while(n&&status_space((unsigned char)p[n-1]))p[--n]=0;return p;}
/* SMP's persisted status IDs are opaque MAX_TITLE_ID=32 byte fields, not
 * Blackwire's selected PPSA grammar. Never emulate a truncated/ambiguous ID. */
static int status_title(const char *p){size_t n=strlen(p);if(!n||n>=32||p[n-1]=='/')return -1;for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127)return -1;return 0;}
static int status_rows(unsigned char *bytes,size_t size,const struct bw_nrc_subject *subject,const char *image,const char title[10]){
    const char *seen[1024];unsigned count=0;size_t at=0;
    if(memchr(bytes,0,size))return -1;bytes[size]=0;
    while(at<size){char *line=(char *)bytes+at;char *end=memchr(line,'\n',size-at);size_t length=end?(size_t)(end-line):size-at;
        at+=length+!!end;if(length>BW_NRC_PATH_BYTES+512u)return -1;if(end)*end=0;
        if(!*status_trim(line))continue;
        char *fields[4]={line};for(unsigned i=1;i<4;i++){char *tab=strchr(fields[i-1],'\t');if(!tab)return -1;*tab=0;fields[i]=tab+1;}
        if(strchr(fields[3],'\t'))return -1;for(unsigned i=0;i<4;i++)fields[i]=status_trim(fields[i]);
        int installed=!strcasecmp(fields[0],"installed");
        if((!installed&&strcasecmp(fields[0],"deleted"))||status_title(fields[1])||literal_path(fields[2])||
           strlen(fields[2])>=1024||strlen(fields[3])>=256||count>=1024)return -1;
        for(unsigned i=0;i<count;i++)if(!strcmp(seen[i],fields[2]))return -1;seen[count++]=fields[2];
        if(installed&&(!strcmp(fields[1],subject->title)&&strcmp(fields[2],subject->source)))return -1;
        if(image&&installed&&!strcmp(fields[2],image)&&strcmp(fields[1],title))return -1;
    }return 0;
}
/* Positive exclusion only: stable persisted status must not still associate
 * any foreign path with this title. Never rewrite status or use it as ownership. */
static int manual_status_proof(struct bw_nrc_remote *r,const struct bw_nrc_subject *subject,
    const char *image,const char title[10],struct status_snapshot *snapshot){
    const char *path="/data/shadowmount/manual.status";
#ifdef BW_COPY_HOST_TEST
    if(r->fixture_manual_status_path)path=r->fixture_manual_status_path;
#ifdef BW_NRC_HOST_MANUAL_STATUS_PATH
    else path=BW_NRC_HOST_MANUAL_STATUS_PATH;
#else
    else {r->substage="manual_status_host_fixture_missing";r->observation_error=0;return -1;}
#endif
#endif
    char parent[PATH_MAX],real[PATH_MAX];int root=-1,fd=-1,rc=-1,error=0,present=0;const char *stage="manual_status_path";
    struct stat roots[2],file[3];unsigned char *bytes=NULL,*again=NULL,hash[32]={0};size_t size=0;
    if(literal_path(path)||strlen(path)>=sizeof parent||current(r))goto done;
    strcpy(parent,path);char *leaf=strrchr(parent,'/');if(!leaf||leaf==parent||!leaf[1])goto done;*leaf++=0;
    stage="manual_status_parent_open";root=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(root<0){error=errno;goto done;}
    stage="manual_status_parent_identity";if(fstat(root,roots)||lstat(parent,roots+1)||!realpath(parent,real)){error=errno;goto done;}
    if(!S_ISDIR(roots[0].st_mode)||strcmp(real,parent)||!same(roots,roots+1))goto done;
    stage="manual_status_open";fd=v2_openat(root,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0){if(errno!=ENOENT){error=errno;goto done;}
        stage="manual_status_absence";if(!cp_fstatat(root,leaf,file,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)goto done;
        cp_sha_bytes("absent",6,hash);
    }else{
        present=1;stage="manual_status_file_identity";
        if(fstat(fd,file)||cp_fstatat(root,leaf,file+1,AT_SYMLINK_NOFOLLOW)){error=errno;goto done;}
        if(!S_ISREG(file[0].st_mode)||file[0].st_nlink!=1||file[0].st_size<0||file[0].st_size>NRC_STATUS_BYTES||!same(file,file+1))goto done;
        size=(size_t)file[0].st_size;if(observation_charge(r,size*2))goto done;
        bytes=malloc(size+1);again=malloc(size?size:1);if(!bytes||!again){stage="manual_status_memory";goto done;}
        for(unsigned pass=0;pass<2;pass++)for(size_t at=0;at<size;){
            if(current(r)){stage="manual_status_read_authority";goto done;}
            stage="manual_status_read";ssize_t got=pread(fd,(pass?again:bytes)+at,size-at,(off_t)at);
            if(got<0&&errno==EINTR)continue;if(got<=0||(size_t)got>size-at){error=got<0?errno:0;goto done;}at+=(size_t)got;
        }
        stage="manual_status_changed";if(memcmp(bytes,again,size))goto done;cp_sha_bytes(bytes,size,hash);
        if(fstat(fd,file+1)||cp_fstatat(root,leaf,file+2,AT_SYMLINK_NOFOLLOW)){error=errno;goto done;}
        if(!same(file,file+1)||!same(file+1,file+2))goto done;
        stage="manual_status_association";if(status_rows(bytes,size,subject,image,title))goto done;
    }
    stage="manual_status_final_identity";
    if(fstat(root,roots+1)||!same(roots,roots+1)||lstat(parent,roots+1)||!same(roots,roots+1)||!realpath(parent,real)){error=errno;goto done;}
    if(strcmp(real,parent)||current(r))goto done;
    if(present){if(fstat(fd,file+1)||cp_fstatat(root,leaf,file+2,AT_SYMLINK_NOFOLLOW)){error=errno;goto done;}if(!same(file,file+1)||!same(file+1,file+2))goto done;}
    else if(!cp_fstatat(root,leaf,file,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)goto done;
    stage="manual_status_snapshot_changed";
    if(snapshot->seen&&(snapshot->present!=(unsigned)present||!same(&snapshot->parent,roots)||
       (present&&!same(&snapshot->file,file))||memcmp(snapshot->hash,hash,32)))goto done;
    rc=0;
done:
    free(bytes);free(again);
    if(fd>=0&&close(fd)){stage="manual_status_close";error=errno;r->observation_release_unknown=1;rc=-1;}
    if(root>=0&&close(root)){stage="manual_status_parent_close";error=errno;r->observation_release_unknown=1;rc=-1;}
    if(!rc&&current(r)){stage="manual_status_final_authority";error=0;rc=-1;}
    if(rc){r->substage=stage;r->observation_error=error;}
    else if(!snapshot->seen){snapshot->seen=1;snapshot->present=(unsigned)present;snapshot->parent=roots[0];if(present)snapshot->file=file[0];memcpy(snapshot->hash,hash,32);}
    return rc;
}
/* A different manual path is excluded only by its real, stable metadata. A
 * missing or unreadable path could be another copy of this title, so it is
 * unknown rather than inferred from its directory name. */
static int manual_title(struct bw_nrc_remote *r,const char *path,char title[10],unsigned *image,struct stat *image_identity){
    int root=-1,sys=-1,fd=-1,result=-1,error=0;struct stat roots[2],syss[2],file[3];char canonical[PATH_MAX];
    unsigned char *bytes=NULL;char content[128];const char *stage="manual_path_or_authority";
    memset(title,0,10);if(image)*image=0;
#define M_CALL(name,call) do{stage=name;if(call){error=errno;goto done;}}while(0)
#define M_TEST(name,predicate) do{stage=name;if(predicate){error=0;goto done;}}while(0)
    M_TEST("manual_path_or_authority",literal_path(path)||current(r));
    M_CALL("manual_realpath",!realpath(path,canonical));M_TEST("manual_noncanonical",strcmp(canonical,path));
    M_CALL("manual_named_type",lstat(path,roots));
    if(S_ISREG(roots[0].st_mode)){int rc=manual_image_title(r,path,roots,title);if(!rc){if(image)*image=1;if(image_identity)*image_identity=roots[0];}return rc;}
    stage="manual_open_root";root=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(root<0){error=errno;goto done;}
    M_CALL("manual_root_stat",fstat(root,roots));M_TEST("manual_root_type",!S_ISDIR(roots[0].st_mode));
    stage="manual_open_sce_sys";sys=v2_openat(root,"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(sys<0){error=errno;goto done;}
    M_CALL("manual_sce_sys_stat",fstat(sys,syss));M_TEST("manual_sce_sys_type",!S_ISDIR(syss[0].st_mode));
    stage="manual_open_param";fd=v2_openat(sys,"param.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0){error=errno;goto done;}
    M_CALL("manual_param_stat",fstat(fd,file));M_TEST("manual_param_type",!S_ISREG(file[0].st_mode));
    M_TEST("manual_param_links",file[0].st_nlink!=1);M_TEST("manual_param_size",file[0].st_size<=0||file[0].st_size>BW_SOURCE_PARAM_LIMIT);
    size_t size=(size_t)file[0].st_size,at=0;bytes=malloc(size);M_TEST("manual_param_memory",!bytes);
    while(at<size){
        M_TEST("manual_read_authority",current(r));stage="manual_param_read";
        ssize_t got=pread(fd,bytes+at,size-at,(off_t)at);if(got<0&&errno==EINTR)continue;
        if(got<=0){error=got<0?errno:0;goto done;}at+=(size_t)got;
    }
    M_TEST("manual_param_parse",bw_source_param_identity(bytes,size,title,content));
    M_CALL("manual_param_restat",fstat(fd,file+1)||cp_fstatat(sys,"param.json",file+2,AT_SYMLINK_NOFOLLOW));
    M_TEST("manual_param_changed",!same(file,file+1)||!same(file+1,file+2));
    M_CALL("manual_sce_sys_restat",fstat(sys,syss+1));M_TEST("manual_sce_sys_changed",!same(syss,syss+1));
    M_CALL("manual_sce_sys_named_stat",cp_fstatat(root,"sce_sys",syss+1,AT_SYMLINK_NOFOLLOW));M_TEST("manual_sce_sys_name_changed",!same(syss,syss+1));
    M_CALL("manual_root_restat",fstat(root,roots+1));M_TEST("manual_root_changed",!same(roots,roots+1));
    M_CALL("manual_root_named_stat",lstat(path,roots+1));M_TEST("manual_root_name_changed",!same(roots,roots+1));
    M_CALL("manual_final_realpath",!realpath(path,canonical));M_TEST("manual_final_path_or_authority",strcmp(path,canonical)||current(r));
    result=0;
done:
    free(bytes);
    if(fd>=0&&close(fd)){if(!result){stage="manual_param_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(sys>=0&&close(sys)){if(!result){stage="manual_sce_sys_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(root>=0&&close(root)){if(!result){stage="manual_root_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(result){r->substage=stage;r->observation_error=error;}
#undef M_CALL
#undef M_TEST
    return result;
}

enum {G_PATH=1u,G_RUNTIME=2u,G_TYPE=4u,G_TITLE=8u,G_CONTENT=16u,G_INSTALLED=32u,
    G_PKG=64u,G_MANAGED=128u,G_AVAILABLE=256u,G_IMAGE=512u,G_MOUNTED=1024u};
struct game {
    char path[BW_NRC_PATH_BYTES],runtime[BW_NRC_PATH_BYTES],type[24],title[10],content[128];
    unsigned installed,pkg,managed,available,image,mounted,seen;
};
static int game_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct game *p=opaque;unsigned bit=0,*flag=NULL;char *text=NULL;size_t cap=0;
#define TEXT(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;text=p->m;cap=sizeof p->m;}
    TEXT("path",G_PATH,path) else TEXT("runtime_path",G_RUNTIME,runtime) else TEXT("source_type",G_TYPE,type)
    else TEXT("title_id",G_TITLE,title) else TEXT("content_id",G_CONTENT,content)
#undef TEXT
#define FLAG(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;flag=&p->m;}
    else FLAG("installed",G_INSTALLED,installed) else FLAG("installed_pkg",G_PKG,pkg) else FLAG("managed",G_MANAGED,managed)
    else FLAG("source_available",G_AVAILABLE,available) else FLAG("image_backed",G_IMAGE,image) else FLAG("mounted",G_MOUNTED,mounted) else return 0;
#undef FLAG
    if(p->seen&bit)return -1;p->seen|=bit;return text?bw_document_string(raw,size,text,cap):boolean(raw,size,flag);
}
static int game_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;struct game one={0};if(++c->count>NRC_REMOTE_ITEMS||current(c->remote)||
       bw_document_fields(raw,size,game_field,&one)||one.seen!=2047u||!one.title[0]||literal_path(one.path)||
       (one.runtime[0]&&literal_path(one.runtime)))return -1;
    const struct bw_nrc_subject *s=c->subject;
    if(!strcmp(one.title,s->title)){
        unsigned empty_absent=c->policy==NRC_ABSENT&&!one.content[0]&&!one.installed&&!one.managed&&!one.mounted;
        if(++c->owned_rows>1||strcmp(one.path,s->source)||strcmp(one.runtime,s->source)||strcmp(one.type,"folder")||
           (strcmp(one.content,s->content)&&!empty_absent)||one.pkg||one.image||(one.mounted&&c->policy!=NRC_REGISTRATION))return -1;
    }else if(!strcmp(one.path,s->source)||!strcmp(one.runtime,s->source))return -1;
    return 0;
}
static int path_contains(const char *,const char *);
static int manual_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;char path[BW_NRC_PATH_BYTES],title[10];unsigned image=0;struct stat identity;
    if(++c->count>NRC_REMOTE_ITEMS||bw_document_string(raw,size,path,sizeof path)||literal_path(path)||current(c->remote))return -1;
    if(!strcmp(path,c->subject->source))return ++c->owned_rows>1?-1:0;
    if(path_contains(path,c->subject->source)||path_contains(c->subject->source,path))return -1;
    if(manual_title(c->remote,path,title,&image,&identity)||!strcmp(title,c->subject->title))return -1;
    if(manual_status_proof(c->remote,c->subject,path,title,c->status))return -1;
    if(image){
        /* Reopen only to compare the original admitted identity after the
         * status observation; this never adopts a replacement generation. */
        int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0){c->remote->substage="manual_image_status_reopen";c->remote->observation_error=errno;return -1;}
        struct local_image held={.remote=c->remote,.path=path,.fd=fd,.admitted=identity};int bad=image_current(&held);
        if(close(fd)){c->remote->observation_release_unknown=1;c->remote->substage="manual_image_status_recheck_close";c->remote->observation_error=errno;return -1;}
        if(bad||current(c->remote)){c->remote->substage=held.stage?held.stage:"manual_image_status_recheck_authority";c->remote->observation_error=held.error;return -1;}
        uint64_t stamp[]={identity.st_dev,identity.st_ino,identity.st_mode,identity.st_nlink,identity.st_uid,identity.st_gid,
            (uint64_t)identity.st_size,(uint64_t)identity.st_mtim.tv_sec,(uint64_t)identity.st_mtim.tv_nsec,
            (uint64_t)identity.st_ctim.tv_sec,(uint64_t)identity.st_ctim.tv_nsec};
        cp_sha_update(&c->images,path,strlen(path)+1);cp_sha_update(&c->images,title,10);cp_sha_update(&c->images,stamp,sizeof stamp);c->image_count++;
    }else{
        /* Folder metadata is observed again after persisted status. Its root
         * title must still be the same foreign title; no stale row supplies it. */
        char again[10];if(manual_title(c->remote,path,again,NULL,NULL)||strcmp(title,again))return -1;
    }return 0;
}
/* Lexical normalization is used ONLY to detect overlap/aliases. It never
 * grants ownership or changes a source path. Resolve existing paths too, so a
 * disjoint-looking symlink into the selected tree cannot be ignored. */
static int prepare_normal(const char *path,char out[BW_NRC_PATH_BYTES]){
    if(!path||path[0]!='/'||strnlen(path,BW_NRC_PATH_BYTES)>=BW_NRC_PATH_BYTES)return -1;
    size_t used=1;out[0]='/';out[1]=0;
    for(const char *p=path+1;*p;){
        while(*p=='/')p++;if(!*p)break;const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return -1;
        if(n==1&&p[0]=='.'){}
        else if(n==2&&p[0]=='.'&&p[1]=='.'){
            if(used==1)return -1;while(used>1&&out[used-1]!='/')used--;if(used>1)used--;out[used]=0;
        }else{
            if(used>1)out[used++]='/';if(n>=BW_NRC_PATH_BYTES-used)return -1;
            memcpy(out+used,p,n);used+=n;out[used]=0;
        }
        if(!end)break;p=end+1;
    }return 0;
}
static int path_contains(const char *a,const char *b){size_t n=strlen(a);return !strcmp(a,"/")||(!strncmp(a,b,n)&&(!b[n]||b[n]=='/'));}
/* 0=disjoint literal, 1=exact selected path, 2=overlap/target alias,
 * 3=disjoint alias (unknown), -1=unclassifiable path. */
static int prepare_path(const char *path,const struct bw_nrc_subject *s){
    char normal[BW_NRC_PATH_BYTES],resolved[PATH_MAX];if(prepare_normal(path,normal))return -1;
    int exact=!strcmp(path,s->source),alias=strcmp(path,normal)!=0;
    if(path_contains(normal,s->source)||path_contains(s->source,normal)){
        if(!exact)return 2;
    }
    if(realpath(path,resolved)){
        if(path_contains(resolved,s->source)||path_contains(s->source,resolved)){
            if(!exact||strcmp(resolved,s->source))return 2;
        }
        if(strcmp(path,resolved))alias=1;
    }
    return exact?1:alias?3:0;
}
static const char *path_class(const char *path){
    if(!strncmp(path,"/data/",6))return "data";if(!strncmp(path,"/mnt/usb",8))return "usb";
    if(!strncmp(path,"/mnt/ext",8))return "external";if(!strncmp(path,"/user/",6))return "user";return "other";
}
static int collection_error(struct collection *c,const char *stage,unsigned missing,const char *class,int error){
    c->remote->substage=stage;c->remote->row_index=c->count;c->remote->missing_fields=missing;
    c->remote->path_class=class;c->remote->observation_error=error;return -1;
}
struct prepare_game {struct game one;unsigned invalid,duplicate;};
static int prepare_game_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct prepare_game *p=opaque;unsigned before=p->one.seen;
    if(game_field(&p->one,key,kn,raw,size)){
        unsigned added=p->one.seen^before;if(!added)p->duplicate=1;else p->invalid|=added;
    }return 0;
}
static int prepare_game_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;struct prepare_game parsed={0};struct game *one=&parsed.one;
    if(++c->count>NRC_REMOTE_ITEMS)return collection_error(c,"item_limit",0,NULL,0);
    if(current(c->remote))return collection_error(c,"authority_changed",0,NULL,0);
    if(bw_document_fields(raw,size,prepare_game_field,&parsed)||parsed.duplicate)return collection_error(c,"malformed_or_duplicate_game_fields",0,NULL,0);
    unsigned missing=2047u&~one->seen;
    if(!(one->seen&G_PATH)||(parsed.invalid&G_PATH)||!one->path[0])return collection_error(c,"game_path_unclassifiable",missing,NULL,0);
    int path=prepare_path(one->path,c->subject),runtime=0;
    if(path<0||path==2)return collection_error(c,"game_path_overlap_or_invalid",missing,path_class(one->path),0);
    if(parsed.invalid&G_RUNTIME)return collection_error(c,"game_runtime_unclassifiable",missing,path_class(one->path),0);
    if(one->runtime[0]){
        runtime=prepare_path(one->runtime,c->subject);
        if(runtime<0||runtime==2)return collection_error(c,"game_runtime_overlap_or_invalid",missing,path_class(one->runtime),0);
    }
    if(!strcmp(one->title,c->subject->title)){
        unsigned absent=c->policy==NRC_PREPARE_ABSENT&&!one->content[0]&&!one->installed&&!one->managed&&!one->mounted;
        if(missing||parsed.invalid||++c->owned_rows>1||path!=1||runtime!=1||strcmp(one->type,"folder")||
           (strcmp(one->content,c->subject->content)&&!absent)||one->pkg||one->image||
           (one->mounted&&c->policy!=NRC_PREPARE_REGISTERED))return collection_error(c,"selected_game_conflict_or_incomplete",missing,path_class(one->path),0);
    }else{
        if(path==1||runtime==1)return collection_error(c,"foreign_title_uses_selected_source",missing,path_class(one->path),0);
        if(!one->title[0]||(parsed.invalid&G_TITLE)){
            char title[10]={0};(void)manual_title(c->remote,one->path,title,NULL,NULL);
            if(c->remote->observation_release_unknown||current(c->remote))return collection_error(c,"game_metadata_release_or_authority_unknown",missing,path_class(one->path),errno);
            if(!strcmp(title,c->subject->title))return collection_error(c,"game_metadata_same_title_conflict",missing,path_class(one->path),0);
        }
        if(missing||parsed.invalid||!one->title[0]||path==3||runtime==3){
            c->remote->unknown_game_rows++;c->remote->substage="disjoint_game_schema_unknown";
            c->remote->row_index=c->count;c->remote->missing_fields=missing;c->remote->path_class=path_class(one->path);
        }
    }
    return current(c->remote)?collection_error(c,"authority_changed",missing,NULL,0):0;
}
static int prepare_manual_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;char path[BW_NRC_PATH_BYTES],title[10]={0};
    if(++c->count>NRC_REMOTE_ITEMS)return collection_error(c,"item_limit",0,NULL,0);
    if(bw_document_string(raw,size,path,sizeof path)||current(c->remote))return collection_error(c,"manual_path_malformed_or_authority_changed",0,NULL,0);
    int relation=prepare_path(path,c->subject);
    if(relation<0||relation==2)return collection_error(c,"manual_path_overlap_or_invalid",0,path_class(path),0);
    if(relation==1)return ++c->owned_rows>1?collection_error(c,"duplicate_selected_manual_path",0,path_class(path),0):0;
    errno=0;int result=manual_title(c->remote,path,title,NULL,NULL),error=result?c->remote->observation_error:0;
    if(c->remote->observation_release_unknown||current(c->remote))return collection_error(c,"manual_release_or_authority_unknown",0,path_class(path),error);
    if(!strcmp(title,c->subject->title))return collection_error(c,"manual_same_title_conflict",0,path_class(path),0);
    if(result){c->remote->unknown_manual_paths++;
        c->remote->row_index=c->count;c->remote->path_class=path_class(path);c->remote->observation_error=error;}
    return 0;
}

struct fields {struct collection *collection;unsigned manual;};
static int collection_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct fields *f=opaque;struct collection *c=f->collection;unsigned bit=0,n=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1u;
    else if(kn==5&&!memcmp(key,"count",5))bit=2u;
    else if(f->manual&&kn==5&&!memcmp(key,"paths",5))bit=4u;
    else if(!f->manual&&kn==5&&!memcmp(key,"games",5))bit=4u;else return 0;
    if(c->seen&bit)return -1;c->seen|=bit;
    if(bit==4u)return bw_document_array(raw,size,preparing(c->policy)?
        (f->manual?prepare_manual_item:prepare_game_item):(f->manual?manual_item:game_item),c);
    if(decimal(raw,size,NRC_REMOTE_ITEMS,&n)||(bit==1u&&n))return -1;if(bit==2u)c->declared=n;return 0;
}
static int documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,unsigned policy,struct status_snapshot *status){
    if(!r||!s)return -1;r->reason="cleanup_remote_unknown";r->substage=r->path_class=NULL;
    r->row_index=r->missing_fields=r->unknown_game_rows=r->unknown_manual_paths=r->observation_release_unknown=0;
    r->preparation_only=(unsigned)preparing(policy);r->observation_error=0;
    r->local_observation_bytes=0;
    if(current(r)||subject_shape(s))return fail(r,"cleanup_remote_subject_invalid");
    struct collection c={.remote=r,.subject=s,.policy=policy,.status=status};struct fields fields={.collection=&c};
    if(bw_document_fields(games,game_size,collection_field,&fields)||c.seen!=7u||c.count!=c.declared)return fail(r,"cleanup_game_snapshot_conflict_or_unknown");
    c=(struct collection){.remote=r,.subject=s,.policy=policy,.status=status};fields.manual=1;
    cp_sha_init(&c.images);
    if(bw_document_fields(manual,manual_size,collection_field,&fields)||c.seen!=7u||c.count!=c.declared||current(r))return fail(r,"cleanup_manual_source_conflict_or_unknown");
    /* Title-wide SMP Uninstall can reconcile an installed status association
     * even when its path is absent from manual/list. This complete global
     * proof is mandatory for EVERY strict observation, not just image rows. */
    if(!preparing(policy)&&manual_status_proof(r,s,NULL,NULL,status))return fail(r,"cleanup_manual_status_conflict_or_unknown");
    if(!preparing(policy)&&(c.image_count||status->images_seen)){
        unsigned char hash[32];cp_sha_final(&c.images,hash);
        if(status->images_seen&&(status->images!=c.image_count||memcmp(status->image_hash,hash,32))){r->substage="manual_image_snapshot_changed";return fail(r,"cleanup_manual_source_conflict_or_unknown");}
        if(!status->images_seen){status->images_seen=1;status->images=c.image_count;memcpy(status->image_hash,hash,32);}
    }
    r->reason="cleanup_remote_sources_checked";
    if(preparing(policy)&&(r->unknown_game_rows||r->unknown_manual_paths))
        bw_send_log("prepare-source-summary","policy=prepare-only unknown_rows=%u unknown_paths=%u last_row=%u missing=%u substage=%s class=%s errno=%d destructive_proof=0",
            r->unknown_game_rows,r->unknown_manual_paths,r->row_index,r->missing_fields,r->substage?r->substage:"",r->path_class?r->path_class:"",r->observation_error);
    return 0;
}
int bw_nrc_remote_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size){
    struct status_snapshot status={0};return documents(r,s,games,game_size,manual,manual_size,NRC_STRICT,&status);
}
int bw_nrc_remote_registration_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size){
    struct status_snapshot status={0};return documents(r,s,games,game_size,manual,manual_size,NRC_REGISTRATION,&status);
}
static int absence_twice(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_absent_fn absent,void *opaque){
    if(!r||subject_shape(s)||!absent)return fail(r,"cleanup_registration_absence_unconfirmed");
    for(unsigned i=0;i<2;i++)if(current(r)||absent(opaque,s)||current(r))return fail(r,"cleanup_registration_absence_unconfirmed");
    return 0;
}
int bw_nrc_remote_absent_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_absent_fn absent,void *opaque){
    struct status_snapshot status={0};return absence_twice(r,s,absent,opaque)||documents(r,s,games,game_size,manual,manual_size,NRC_ABSENT,&status)||
        absence_twice(r,s,absent,opaque)?-1:0;
}
static int sources(void *opaque,const struct bw_nrc_subject *subject,unsigned policy,bw_nrc_remote_absent_fn absent,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r)return -1;struct bw_sm_reply version={0};
    struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1;
    struct status_snapshot status={0};
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.uninstall_supported)goto done;
    for(unsigned i=0;i<2;i++){
        r->reason="cleanup_remote_observation_unknown";
        if((policy==NRC_ABSENT&&absence_twice(r,subject,absent,proof))||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,policy,&status)||
           (policy==NRC_ABSENT&&absence_twice(r,subject,absent,proof)))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||
       memcmp(games[0].bytes,games[1].bytes,games[0].size)||memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){
        fail(r,"cleanup_remote_snapshot_changed");goto done;
    }
    r->reason="cleanup_remote_sources_checked";result=0;
done:
    for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}
int bw_nrc_remote_sources(void *opaque,const struct bw_nrc_subject *subject){return sources(opaque,subject,NRC_STRICT,NULL,NULL);}
int bw_nrc_remote_registration_sources(void *opaque,const struct bw_nrc_subject *subject){return sources(opaque,subject,NRC_REGISTRATION,NULL,NULL);}
int bw_nrc_remote_absent_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_absent_fn absent,void *proof){
    if(!absent)return fail(opaque,"cleanup_registration_absence_unconfirmed");
    return sources(opaque,subject,NRC_ABSENT,absent,proof);
}
static int confirmed_twice(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_registered_fn registered,void *proof,int *expected){
    if(!r||subject_shape(s)||!registered)return fail(r,"cleanup_confirmed_registration_unqualified");
    for(unsigned i=0;i<2;i++){
        if(current(r))return fail(r,"cleanup_confirmed_registration_unqualified");
        int state=registered(proof,s);
        if((state!=0&&state!=1)||(*expected>=0&&state!=*expected)||current(r))return fail(r,"cleanup_confirmed_registration_changed_or_unknown");
        *expected=state;
    }return 0;
}
int bw_nrc_remote_confirmed_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_registered_fn registered,void *proof){
    struct status_snapshot status={0};int expected=-1;return confirmed_twice(r,s,registered,proof,&expected)||
        documents(r,s,games,game_size,manual,manual_size,expected?NRC_REGISTRATION:NRC_ABSENT,&status)||
        confirmed_twice(r,s,registered,proof,&expected)?-1:0;
}
int bw_nrc_remote_prepare_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_registered_fn witness,void *proof){
    struct status_snapshot status={0};int expected=-1;
    return confirmed_twice(r,s,witness,proof,&expected)||
        documents(r,s,games,game_size,manual,manual_size,expected?NRC_PREPARE_REGISTERED:NRC_PREPARE_ABSENT,&status)||
        confirmed_twice(r,s,witness,proof,&expected)?-1:0;
}
int bw_nrc_remote_prepare_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_registered_fn witness,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r||!witness)return fail(r,"cleanup_prepare_exact_witness_unconfirmed");
    struct bw_sm_reply version={0};struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1,expected=-1;
    struct status_snapshot status={0};
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version))goto done;
    for(unsigned i=0;i<2;i++){
        r->reason="cleanup_remote_observation_unknown";
        if(confirmed_twice(r,subject,witness,proof,&expected)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,expected?NRC_PREPARE_REGISTERED:NRC_PREPARE_ABSENT,&status)||
           confirmed_twice(r,subject,witness,proof,&expected))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||memcmp(games[0].bytes,games[1].bytes,games[0].size)||
       memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){fail(r,"cleanup_remote_snapshot_changed");goto done;}
    r->reason="cleanup_prepare_sources_observed_not_destructive_proof";result=0;
done:
    if(result)bw_send_log("prepare-source-failure","policy=prepare-only reason=%s row=%u missing=%u unknown_rows=%u unknown_paths=%u substage=%s class=%s errno=%d",
        r->reason?r->reason:"",r->row_index,r->missing_fields,r->unknown_game_rows,r->unknown_manual_paths,r->substage?r->substage:"",r->path_class?r->path_class:"",r->observation_error);
    for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}

int bw_nrc_remote_confirmed_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_registered_fn registered,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r||!registered)return fail(r,"cleanup_confirmed_registration_unqualified");
    struct bw_sm_reply version={0};struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1,expected=-1;
    struct status_snapshot status={0};
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.uninstall_supported)goto done;
    for(unsigned i=0;i<2;i++){
        if(confirmed_twice(r,subject,registered,proof,&expected)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,expected?NRC_REGISTRATION:NRC_ABSENT,&status)||
           confirmed_twice(r,subject,registered,proof,&expected))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||memcmp(games[0].bytes,games[1].bytes,games[0].size)||
       memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){fail(r,"cleanup_remote_snapshot_changed");goto done;}
    r->reason="cleanup_remote_sources_checked";result=0;
done:for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}
int bw_nrc_remote_uninstall(void *opaque,const char title[10]){
    struct bw_nrc_remote *r=opaque;if(!r||current(r))return -1;
    struct bw_sm_uninstall_reply reply={0};int result=bw_sm_uninstall(&r->connection,title,&reply);
    r->reason=result?(reply.refused_before_platform?
        (!strcmp(reply.error_reason,"title_unmount_busy")?"cleanup_uninstall_refused_title_unmount_busy_before_platform":"cleanup_uninstall_refused_before_platform"):
        "cleanup_uninstall_outcome_unknown"):"cleanup_uninstall_requested_not_removal_proof";
    return result?(reply.refused_before_platform?1:-1):0;
}
int bw_nrc_remote_release_runtime(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_registered_fn registered,void *proof){
    if(!r||!s||!registered)return fail(r,"cleanup_runtime_release_authority_unknown");
    struct bw_sm_reply version={0};int state=-1;
    if(confirmed_twice(r,s,registered,proof,&state)||state!=1||
       bw_nrc_remote_confirmed_sources(r,s,registered,proof)||
       bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.unmount_supported||
       confirmed_twice(r,s,registered,proof,&state)||state!=1)
        return fail(r,"cleanup_runtime_release_authority_or_capability_unknown");
    struct bw_sm_unmount_reply reply={0};
    if(bw_sm_unmount(&r->connection,s->title,&reply))
        return fail(r,reply.response_complete&&reply.api_status==16?"cleanup_runtime_release_busy_no_retry":"cleanup_runtime_release_outcome_unknown_no_retry");
    /* Release does not remove the registration or grant a new baseline. */
    if(confirmed_twice(r,s,registered,proof,&state)||state!=1||bw_nrc_remote_sources(r,s)||
       confirmed_twice(r,s,registered,proof,&state)||state!=1)
        return fail(r,"cleanup_runtime_release_fresh_unmounted_binding_unconfirmed");
    r->reason="cleanup_runtime_released_same_registration_reconfirmed";return 0;
}
const char *bw_nrc_remote_status_message(const struct bw_nrc_remote *r){
    if(!r||!r->reason)return "The game removal result is unknown. Refresh status without retrying.";
    if(!strcmp(r->reason,"cleanup_runtime_release_busy_no_retry"))return "Could not release the game mount because it is busy. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_authority_or_capability_unknown"))return "Could not confirm permission or support to release the game mount. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_fresh_unmounted_binding_unconfirmed"))return "Could not confirm the released game mount. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_outcome_unknown_no_retry"))return "Could not confirm release of the game mount. No uninstall was submitted; refresh status without retrying.";
    if(!strcmp(r->reason,"cleanup_uninstall_refused_title_unmount_busy_before_platform"))return "ShadowMount could not release the game mount and refused uninstall before removing the registration.";
    if(!strcmp(r->reason,"cleanup_uninstall_refused_before_platform"))return "ShadowMount refused uninstall before removing the registration. See the diagnostic reason.";
    return "The game removal result is unknown. Refresh status without retrying.";
}
