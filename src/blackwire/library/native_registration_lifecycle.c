/* SPDX-License-Identifier: GPL-3.0-or-later
 * Normal raw-FOLDER registration ownership and joined-exit retirement.
 * All remote requests use the existing fixed-route SMP transport. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_registration_lifecycle.h"
#include "native_cleanup_remote.h"
#include "native_document.h"
#include "source_param.h"
#include "registration_param_compat.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>

#define NRL_ROOT_LIMIT 256u
#define NRL_DEPTH_LIMIT 64u
struct nrl_witness;
struct bw_nrl_owner {
    struct bw_nrl_request request;
    struct bw_reader_selection selection;
    char roots[3][BW_NRC_PATH_BYTES],registration_run[33];
    struct bw_nrc_ticket *ticket;
    struct bw_nrc_remote remote;
    struct bw_native_reader_release release;
    enum bw_nrl_control phase;
    unsigned registered,owned,finished,release_unknown;
    unsigned initial_absence;
    unsigned manual_requested,runtime_release_done;
    struct nrl_witness *witness;
    const char *failure;
};
static int nrl_current(void *opaque){
    struct bw_nrl_owner *p=opaque;
    int bad=!p||!p->request.current||p->request.current(p->request.opaque,p->phase);
    if(bad&&p&&!p->failure)p->failure="registration_control_guard_refused";return bad?-1:0;
}
static int nrl_path(const char *path){
    if(!path||path[0]!='/'||strnlen(path,BW_NRC_PATH_BYTES)>=BW_NRC_PATH_BYTES)return -1;
    if(!path[1])return 0;
    for(const char *p=path+1;*p;){const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        if(!n||n>255||(n==1&&*p=='.')||(n==2&&!memcmp(p,"..",2)))return -1;
        for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return -1;
        if(!end)break;p=end+1;if(!*p)return -1;
    }return 0;
}
static int nrl_below(const char *root,const char *path){
    size_t n=strlen(root);return !strcmp(root,"/")||(!strncmp(root,path,n)&&(!path[n]||path[n]=='/'));
}
static int nrl_node(const struct stat *a,const struct stat *b){
    return S_ISDIR(b->st_mode)&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
struct nrl_directory {int fd;char path[BW_NRC_PATH_BYTES];struct stat st;};
static int nrl_directory_current(const struct nrl_directory *d){
    struct stat held,named;char real[PATH_MAX];
    return d->fd<0||fstat(d->fd,&held)||lstat(d->path,&named)||!nrl_node(&d->st,&held)||!nrl_node(&held,&named)||
        !realpath(d->path,real)||strcmp(d->path,real)?-1:0;
}
static int nrl_directory_open(struct nrl_directory *d,const char *path){
    char real[PATH_MAX];struct stat named;d->fd=-1;
    if(nrl_path(path)||!realpath(path,real)||strcmp(path,real)||lstat(path,&named)||!S_ISDIR(named.st_mode))return -1;
    strcpy(d->path,path);d->fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);
    return d->fd<0||fstat(d->fd,&d->st)||!nrl_node(&named,&d->st)||nrl_directory_current(d)?-1:0;
}
struct nrl_blob {int fd;char path[BW_NRC_PATH_BYTES];struct stat st;size_t size;unsigned char hash[32];};
struct nrl_witness {
    struct nrl_directory apps,app,source;
    struct nrl_blob source_param,app_param,mount,receipt;
    struct bw_nrc_subject subject;
};
static int nrl_same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&
        a->st_size==b->st_size&&a->st_nlink==b->st_nlink&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int nrl_blob_bytes(struct bw_nrl_owner *p,struct nrl_blob *b,void **body){
    struct stat held,named;char real[PATH_MAX];unsigned char *bytes=NULL;int rc=-1;
    if(body)*body=NULL;
    if(nrl_current(p)||b->fd<0||fstat(b->fd,&held)||!nrl_same(&b->st,&held)||!S_ISREG(held.st_mode)||held.st_nlink!=1||
       lstat(b->path,&named)||!nrl_same(&held,&named)||!realpath(b->path,real)||strcmp(b->path,real))return -1;
    bytes=malloc(b->size);if(!bytes)return -1;
    for(size_t at=0;at<b->size;){
        if(nrl_current(p))goto done;ssize_t n=pread(b->fd,bytes+at,b->size-at,(off_t)at);
        if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>b->size-at)goto done;at+=(size_t)n;
    }
    if(fstat(b->fd,&held)||lstat(b->path,&named)||!nrl_same(&b->st,&held)||!nrl_same(&held,&named)||
       !realpath(b->path,real)||strcmp(b->path,real)||nrl_current(p))goto done;
    if(body){*body=bytes;bytes=NULL;}else{unsigned char hash[32];cp_sha_bytes(bytes,b->size,hash);if(memcmp(hash,b->hash,32))goto done;}
    rc=0;
done:free(bytes);return rc;
}
static int nrl_blob_open(struct bw_nrl_owner *p,struct nrl_blob *b,const char *path,size_t limit,void **body){
    b->fd=-1;if(nrl_path(path))return -1;strcpy(b->path,path);b->fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(b->fd<0||fstat(b->fd,&b->st)||!S_ISREG(b->st.st_mode)||b->st.st_nlink!=1||b->st.st_size<=0||(uint64_t)b->st.st_size>limit)return -1;
    b->size=(size_t)b->st.st_size;void *bytes=NULL;if(nrl_blob_bytes(p,b,&bytes))return -1;cp_sha_bytes(bytes,b->size,b->hash);
    if(body)*body=bytes;else free(bytes);return 0;
}
static int nrl_witness_close(struct bw_nrl_owner *p){
    struct nrl_witness *w=p->witness;if(!w)return 0;int bad=0;
    int fds[]={w->receipt.fd,w->mount.fd,w->app_param.fd,w->source_param.fd,w->source.fd,w->app.fd,w->apps.fd};
    for(unsigned i=0;i<sizeof fds/sizeof *fds;i++)if(fds[i]>=0&&close(fds[i]))bad=1;
    memset(w,0,sizeof *w);free(w);p->witness=NULL;return bad?-1:0;
}
static int nrl_witness_subject(const struct nrl_witness *w,const struct bw_nrc_subject *s){
    const struct bw_nrc_subject *a=&w->subject;
    return !s||strcmp(a->title,s->title)||strcmp(a->run,s->run)||strcmp(a->source,s->source)||strcmp(a->content,s->content)||
        strcmp(a->game,s->game)||memcmp(a->prepared_record_sha256,s->prepared_record_sha256,32)?-1:0;
}
static int nrl_witness_current(struct bw_nrl_owner *p){
    struct nrl_witness *w=p->witness;struct stat named;
    if(!w||nrl_current(p)||nrl_directory_current(&w->apps)||nrl_directory_current(&w->app)||nrl_directory_current(&w->source)||
       nrl_blob_bytes(p,&w->source_param,NULL)||nrl_blob_bytes(p,&w->app_param,NULL)||nrl_blob_bytes(p,&w->receipt,NULL))return -1;
    const char *forbidden[]={"app.pkg","mount_img.lnk","mount_image.lnk"};
    for(unsigned i=0;i<3;i++)if(!cp_fstatat(w->app.fd,forbidden[i],&named,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
    if(!cp_fstatat(w->app.fd,"mount.lnk",&named,AT_SYMLINK_NOFOLLOW)){
        if(nrl_blob_bytes(p,&w->mount,NULL))return -1;
    }else if(errno!=ENOENT||!p->manual_requested)return -1;
    return bw_registration_db_match(p->request.environment.database,w->subject.title,w->subject.content)!=1||nrl_current(p)?-1:0;
}
static int nrl_witness_capture(struct bw_nrl_owner *p){
    if(p->witness)return -1;struct nrl_witness *w=calloc(1,sizeof *w);if(!w)return -1;p->witness=w;
    w->apps.fd=w->app.fd=w->source.fd=w->source_param.fd=w->app_param.fd=w->mount.fd=w->receipt.fd=-1;
    char app[BW_NRC_PATH_BYTES],source[BW_NRC_PATH_BYTES],path[BW_NRC_PATH_BYTES],title[10];void *a=NULL,*b=NULL,*link=NULL;int rc=-1;
    int n=snprintf(source,sizeof source,"%s/games/%s",p->roots[0],p->selection.game);
    int m=snprintf(app,sizeof app,"%s/%s",p->roots[1],p->selection.title);
    if(n<0||(size_t)n>=sizeof source||m<0||(size_t)m>=sizeof app||nrl_directory_open(&w->apps,p->roots[1])||
       nrl_directory_open(&w->app,app)||nrl_directory_open(&w->source,source))goto done;
    n=snprintf(path,sizeof path,"%s/sce_sys/param.json",source);
    if(n<0||(size_t)n>=sizeof path||nrl_blob_open(p,&w->source_param,path,BW_SOURCE_PARAM_LIMIT,&a)||
       bw_source_param_identity(a,w->source_param.size,title,w->subject.content)||strcmp(title,p->selection.title))goto done;
    n=snprintf(path,sizeof path,"%s/sce_sys/param.json",app);
    if(n<0||(size_t)n>=sizeof path||nrl_blob_open(p,&w->app_param,path,BW_SOURCE_PARAM_LIMIT,&b)||
       bw_registration_param_pair(a,w->source_param.size,b,w->app_param.size)<0)goto done;
    n=snprintf(path,sizeof path,"%s/mount.lnk",app);
    if(n<0||(size_t)n>=sizeof path||nrl_blob_open(p,&w->mount,path,BW_NRC_PATH_BYTES+2u,&link))goto done;
    size_t z=w->mount.size;if(z&&((char *)link)[z-1]=='\n')z--;if(z&&((char *)link)[z-1]=='\r')z--;
    if(z!=strlen(source)||memcmp(link,source,z))goto done;
    n=snprintf(path,sizeof path,"%s/native-prepared/%s/prepared.bin",p->roots[0],p->selection.game);
    if(n<0||(size_t)n>=sizeof path||nrl_blob_open(p,&w->receipt,path,384,NULL)||w->receipt.size!=384)goto done;
    static const char digits[]="0123456789abcdef";
    if(strnlen(p->selection.prepared_record_hex,65)!=64)goto done;
    for(unsigned i=0;i<32;i++){const char *x=strchr(digits,p->selection.prepared_record_hex[2*i]),*y=strchr(digits,p->selection.prepared_record_hex[2*i+1]);if(!x||!y)goto done;w->subject.prepared_record_sha256[i]=(unsigned char)(((x-digits)<<4)|(y-digits));}
    if(memcmp(w->receipt.hash,w->subject.prepared_record_sha256,32))goto done;
    strcpy(w->subject.title,title);strcpy(w->subject.game,p->selection.game);strcpy(w->subject.source,source);strcpy(w->subject.run,p->registration_run);
    if(nrl_witness_current(p))goto done;rc=0;
done:free(a);free(b);free(link);if(rc)nrl_witness_close(p);return rc;
}
struct nrl_absence {const struct bw_nrc_environment *env;struct bw_nrc_remote *remote;struct bw_nrc_subject subject;struct nrl_directory apps;};
static int nrl_absence_current(void *opaque,const struct bw_nrc_subject *s){
    struct nrl_absence *p=opaque;struct stat st;
    return !s||memcmp(s,&p->subject,sizeof *s)||!p->remote->connection.current||p->remote->connection.current(p->remote->connection.opaque)||
        nrl_directory_current(&p->apps)||!cp_fstatat(p->apps.fd,s->title,&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||
        bw_registration_db_match(p->env->database,s->title,s->content)!=0||nrl_directory_current(&p->apps)||
        p->remote->connection.current(p->remote->connection.opaque)?-1:0;
}
int bw_nrl_absent_sources(const struct bw_nrc_environment *env,struct bw_nrc_remote *remote,const struct bw_nrc_subject *subject){
    if(!env||!env->apps_root||!env->database||!remote||!remote->connection.current||!subject)return -1;
    struct nrl_absence proof={.env=env,.remote=remote,.subject=*subject,.apps={.fd=-1}};int rc=-1;
    if(remote->connection.current(remote->connection.opaque)||nrl_directory_open(&proof.apps,env->apps_root))goto done;
    rc=bw_nrc_remote_absent_sources(remote,subject,nrl_absence_current,&proof);
done:if(proof.apps.fd>=0&&close(proof.apps.fd))rc=-1;return rc;
}
/* Keep every opened ancestor until the root observation has been checked.
 * ENOENT is accepted only under those still-named canonical held parents.
 * An unreadable, linked, non-directory or renamed root is unknown. */
static int nrl_retained_current(const char *source,const struct bw_nrl_retained_source *w,char parent[BW_NRC_PATH_BYTES]){
    if(!w||w->parent<0||w->root<0||nrl_path(source))return -1;strcpy(parent,source);char *leaf=strrchr(parent,'/');
    if(!leaf||leaf==parent||!leaf[1])return -1;*leaf++=0;
    struct stat p,named,r;char real[PATH_MAX];
    if(fstat(w->parent,&p)||lstat(parent,&named)||!nrl_node(&w->parent_identity,&p)||!nrl_node(&p,&named)||
       !realpath(parent,real)||strcmp(parent,real)||fstat(w->root,&r)||!nrl_node(&w->root_identity,&r))return -1;
    if(!cp_fstatat(w->parent,leaf,&named,AT_SYMLINK_NOFOLLOW))return nrl_node(&r,&named)?0:-1;
    return errno==ENOENT&&w->root_identity.st_nlink>0&&r.st_nlink==0?0:-1;
}
static int nrl_root_excludes(const struct bw_sm_connection *c,const char *source,const char *root,const struct bw_nrl_retained_source *w){
    if(!c||!c->current||nrl_path(source)||nrl_path(root)||nrl_below(root,source)||c->current(c->opaque))return -1;
    struct nrl_directory *dirs=calloc(NRL_DEPTH_LIMIT,sizeof *dirs);if(!dirs)return -1;
    for(unsigned i=0;i<NRL_DEPTH_LIMIT;i++)dirs[i].fd=-1;
    unsigned count=1,complete=0;int result=-1;strcpy(dirs[0].path,"/");
    dirs[0].fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);
    if(dirs[0].fd<0||fstat(dirs[0].fd,&dirs[0].st)||nrl_directory_current(dirs))goto done;
    const char *at=root+1;
    while(*at){
        const char *end=strchr(at,'/');size_t n=end?(size_t)(end-at):strlen(at);char component[256];
        if(!n||n>=sizeof component||count==NRL_DEPTH_LIMIT||c->current(c->opaque))goto done;
        memcpy(component,at,n);component[n]=0;
        struct nrl_directory *d=dirs+count;int length=snprintf(d->path,sizeof d->path,"%s%s%s",dirs[count-1].path,count==1?"":"/",component);
        if(length<0||(size_t)length>=sizeof d->path)goto done;
        d->fd=v2_openat(dirs[count-1].fd,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);
        if(d->fd<0){if(errno!=ENOENT)goto done;break;}
        count++;if(fstat(d->fd,&d->st)||nrl_directory_current(d))goto done;
        if(!end)complete=1;
        at=end?end+1:at+n;
    }
    /* Resolve source ancestry too, to reject an existing root which is a
     * differently named mount of one of the source's ancestor directories. */
    char prefix[BW_NRC_PATH_BYTES],real[PATH_MAX];size_t sn=strlen(source);
    if(w){
        if(nrl_retained_current(source,w,prefix))goto done;
        if(complete&&dirs[count-1].st.st_dev==w->root_identity.st_dev&&dirs[count-1].st.st_ino==w->root_identity.st_ino)goto done;
    }else{
        if(!realpath(source,real)||strcmp(source,real))goto done;
        memcpy(prefix,source,sn+1);
    }
    for(;;){
        struct stat st;if(lstat(prefix,&st)||!S_ISDIR(st.st_mode))goto done;
        /* '/' is necessarily shared by every absolute path; it is not an
         * effective root unless the requested scan root actually was '/'. */
        if(complete&&st.st_dev==dirs[count-1].st.st_dev&&st.st_ino==dirs[count-1].st.st_ino)goto done;
        char *slash=strrchr(prefix,'/');if(!slash||slash==prefix)break;*slash=0;
    }
    for(unsigned i=0;i<count;i++)if(nrl_directory_current(dirs+i))goto done;
    if(w&&nrl_retained_current(source,w,prefix))goto done;
    if(c->current(c->opaque))goto done;result=0;
done:
    for(unsigned i=count;i>0;i--)if(dirs[i-1].fd>=0&&close(dirs[i-1].fd))result=-1;
    free(dirs);return result;
}
struct nrl_roots {
    const struct bw_sm_connection *connection;
    const struct bw_nrc_subject *subject;
    const struct bw_nrl_retained_source *retained;
    unsigned seen,count,declared;
};
static int nrl_number(const void *raw,size_t size,unsigned max,unsigned *out){
    const unsigned char *p=raw;unsigned value=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>max||value>(max-d)/10)return -1;value=value*10+d;}*out=value;return 0;
}
static int nrl_root_item(void *opaque,const void *raw,size_t size){
    struct nrl_roots *p=opaque;char path[BW_NRC_PATH_BYTES];
    if(++p->count>NRL_ROOT_LIMIT||bw_document_string(raw,size,path,sizeof path))return -1;
    return nrl_root_excludes(p->connection,p->subject->source,path,p->retained);
}
static int nrl_roots_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct nrl_roots *p=opaque;unsigned bit=0,n=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1;
    else if(kn==15&&!memcmp(key,"scan_path_count",15))bit=2;
    else if(kn==10&&!memcmp(key,"scan_paths",10))bit=4;else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==4)return bw_document_array(raw,size,nrl_root_item,p);
    if(nrl_number(raw,size,NRL_ROOT_LIMIT,&n)||(bit==1&&n))return -1;
    if(bit==2)p->declared=n;return 0;
}
static int nrl_settings_exclude(const struct bw_sm_connection *c,const struct bw_nrc_subject *s,const void *raw,size_t size,const struct bw_nrl_retained_source *w){
    if(!c||!s||nrl_path(s->source)||!raw||!size||size>BW_NATIVE_DOCUMENT_LIMIT)return -1;
    struct nrl_roots p={.connection=c,.subject=s,.retained=w};
    if(bw_document_fields(raw,size,nrl_roots_field,&p)||p.seen!=7||p.count!=p.declared)return -1;
    /* Exact defaults/managed roots from the supported SMP API-v1 source:
     * include/sm_paths.h and sm_config_mount.c init_runtime_scan_paths_defaults.
     * An empty custom array means defaults, never an empty effective list. */
    if(!p.count){
        static const char *const defaults[]={"/data/homebrew","/data/etaHEN/games",
            "/mnt/ext0/homebrew","/mnt/ext0/etaHEN/games","/mnt/ext1/homebrew","/mnt/ext1/etaHEN/games",
            "/mnt/usb0/homebrew","/mnt/usb1/homebrew","/mnt/usb2/homebrew","/mnt/usb3/homebrew",
            "/mnt/usb4/homebrew","/mnt/usb5/homebrew","/mnt/usb6/homebrew","/mnt/usb7/homebrew",
            "/mnt/usb0/etaHEN/games","/mnt/usb1/etaHEN/games","/mnt/usb2/etaHEN/games","/mnt/usb3/etaHEN/games",
            "/mnt/usb4/etaHEN/games","/mnt/usb5/etaHEN/games","/mnt/usb6/etaHEN/games","/mnt/usb7/etaHEN/games",
            "/mnt/usb0","/mnt/usb1","/mnt/usb2","/mnt/usb3","/mnt/usb4","/mnt/usb5","/mnt/usb6","/mnt/usb7","/mnt/ext0","/mnt/ext1"};
        for(unsigned i=0;i<sizeof defaults/sizeof *defaults;i++)if(nrl_root_excludes(c,s->source,defaults[i],w))return -1;
    }
    return nrl_root_excludes(c,s->source,"/mnt/shadowmnt",w)||nrl_root_excludes(c,s->source,"/mnt/shadowmnt/pfsc",w)?-1:0;
}
int bw_nrl_settings_exclude(const struct bw_sm_connection *c,const struct bw_nrc_subject *s,const void *raw,size_t size){return nrl_settings_exclude(c,s,raw,size,NULL);}
int bw_nrl_settings_exclude_retained(const struct bw_sm_connection *c,const struct bw_nrc_subject *s,const void *raw,size_t size,const struct bw_nrl_retained_source *w){
    char parent[BW_NRC_PATH_BYTES];return !c||!c->current||c->current(c->opaque)||!w||!s||nrl_retained_current(s->source,w,parent)?-1:nrl_settings_exclude(c,s,raw,size,w);
}
static int nrl_released(void *,const struct bw_nrc_subject *);
/* This callback is meaningful only inside the original live joined owner.
 * Its registered witness is retained before handoff; a cache mounted flag is
 * never the source of release, registration, or process-idle authority. */
static int nrl_released_registration(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;struct stat st;struct nrl_witness *w=p->witness;
    if(!w||nrl_released(p,s)||nrl_witness_subject(w,s)||nrl_current(p)||nrl_directory_current(&w->apps))return -1;
    if(cp_fstatat(w->apps.fd,s->title,&st,AT_SYMLINK_NOFOLLOW)){
        return errno==ENOENT&&bw_registration_db_match(p->roots[2],s->title,s->content)==0&&
            !nrl_directory_current(&w->apps)&&!nrl_current(p)?0:-1;
    }
    return nrl_witness_current(p)?-1:1;
}
static int nrl_sources(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;if(nrl_current(p))return -1;
    /* Only the initial intent is collected while the real app and DB are
     * independently absent. A stale uninstalled cache row is not ownership.
     * Commit/post-registration keeps its exact registration policy. */
    int checked=p->phase==BW_NRL_REGISTER?(p->initial_absence?
        bw_nrl_absent_sources(&p->request.environment,&p->remote,s):
        bw_nrc_remote_registration_sources(&p->remote,s)):
        p->owned?bw_nrc_remote_confirmed_sources(&p->remote,s,nrl_released_registration,p):
        bw_nrl_absent_sources(&p->request.environment,&p->remote,s);
    if(checked){
        if(!p->failure)p->failure=p->remote.reason?p->remote.reason:"registration_source_collector_refused";return -1;
    }
    return nrl_current(p);
}
static int nrl_uninstall(void *opaque,const char title[10]){
    struct bw_nrl_owner *p=opaque;if(nrl_current(p)||!p->witness||strcmp(title,p->selection.title))return -1;
    /* If manual bookkeeping was already absent, no manual callback ran.
     * Preserve its missing-link exception only when strict snapshots already
     * say unmounted and the retained app/DB witness remains exact. */
    if(!p->runtime_release_done){
        if(nrl_released_registration(p,&p->witness->subject)!=1)return -1;
        if(bw_nrc_remote_sources(&p->remote,&p->witness->subject)&&
           bw_nrc_remote_release_runtime(&p->remote,&p->witness->subject,nrl_released_registration,p)){
            p->failure=p->remote.reason;return -1;
        }
    }
    /* CONTROL and release observations may block after the core's witness,
     * including when runtime release completed in the manual-remove phase. */
    if(nrl_released_registration(p,&p->witness->subject)!=1)return -1;
    p->runtime_release_done=1;
    int rc=bw_nrc_remote_uninstall(&p->remote,title);if(rc)p->failure=p->remote.reason;return rc;
}
static int nrl_outside(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;struct bw_sm_reply v={0};struct bw_sm_document docs[2]={{0}};int rc=-1;
    if(nrl_current(p)||bw_sm_post(&p->remote.connection,BW_SM_VERSION,"/",&v)||!v.settings_supported||!v.remove_supported||!v.uninstall_supported)goto done;
    for(unsigned i=0;i<2;i++)if(bw_sm_observe(&p->remote.connection,BW_SM_OBSERVE_SETTINGS,docs+i)||
        bw_nrl_settings_exclude(&p->remote.connection,s,docs[i].bytes,docs[i].size))goto done;
    if(docs[0].size!=docs[1].size||memcmp(docs[0].bytes,docs[1].bytes,docs[0].size)||nrl_current(p))goto done;rc=0;
done:for(unsigned i=0;i<2;i++)bw_sm_document_clear(docs+i);return rc;
}
static int nrl_manual(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;struct bw_sm_reply a={0},b={0};
    if(nrl_current(p)||bw_sm_post(&p->remote.connection,BW_SM_MANUAL_LIST,s->source,&a)||
       bw_sm_post(&p->remote.connection,BW_SM_MANUAL_LIST,s->source,&b)||a.present!=b.present||a.count!=b.count||nrl_current(p))return -1;
    return a.present?1:0;
}
static int nrl_remove(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;struct bw_sm_reply r={0};
    if(nrl_current(p)||!p->witness||nrl_released_registration(p,s)!=1)return -1;
    /* The core already persisted this owner's .retire unit. Release while
     * the exact witnessed mount.lnk still exists, before manual/remove may
     * remove it. Failure remains consumed and does not reach unregister. */
    if(!p->runtime_release_done&&bw_nrc_remote_release_runtime(&p->remote,s,nrl_released_registration,p)){
        p->failure=p->remote.reason;return -1;
    }
    p->runtime_release_done=1;
    if(nrl_released_registration(p,s)!=1||nrl_current(p))return -1;p->manual_requested=1;
    return bw_sm_post(&p->remote.connection,BW_SM_MANUAL_REMOVE,s->source,&r)||r.present?-1:0;
}
static int nrl_released(void *opaque,const struct bw_nrc_subject *s){
    struct bw_nrl_owner *p=opaque;char source[BW_NRC_PATH_BYTES];
    int n=snprintf(source,sizeof source,"%s/games/%s",p->request.environment.data_root,p->selection.game);
    return !p->owned||p->phase!=BW_NRL_RETIRE||n<0||(size_t)n>=sizeof source||strcmp(s->source,source)||
        strcmp(s->title,p->selection.title)||strcmp(s->run,p->registration_run)||strcmp(p->release.run,p->selection.run)||
        !p->release.runtime_released||!p->release.io_released||!p->release.private_released?-1:0;
}
static struct bw_nrc_ops nrl_ops(struct bw_nrl_owner *p){
    return (struct bw_nrc_ops){p,nrl_current,nrl_sources,nrl_uninstall};
}
static int nrl_result(struct bw_nrc_result *r,enum bw_nrc_state state,const char *reason,int rc){
    if(r)*r=(struct bw_nrc_result){.state=state,.reason=reason};return rc;
}
struct nrl_generation_observation {const struct bw_nrc_environment *env;int (*current)(void *);void *opaque;struct bw_nrc_remote remote;};
static int nrl_generation_current(void *opaque){struct nrl_generation_observation *o=opaque;return !o||!o->current||o->current(o->opaque)?-1:0;}
static int nrl_generation_sources(void *opaque,const struct bw_nrc_subject *s){
    struct nrl_generation_observation *o=opaque;return nrl_generation_current(o)||bw_nrl_absent_sources(o->env,&o->remote,s)?-1:0;
}
static int nrl_generation_manual(void *opaque,const struct bw_nrc_subject *s){
    struct nrl_generation_observation *o=opaque;struct bw_sm_reply a={0},b={0};
    if(nrl_generation_current(o)||bw_sm_post(&o->remote.connection,BW_SM_MANUAL_LIST,s->source,&a)||
       bw_sm_post(&o->remote.connection,BW_SM_MANUAL_LIST,s->source,&b)||a.present!=b.present||a.count!=b.count||nrl_generation_current(o))return -1;
    return a.present?1:0;
}
int bw_nrl_reconcile_prepared_generation(const struct bw_nrc_environment *env,const struct bw_reader_selection *selected,
    uint16_t port,int (*current)(void *),void *opaque,struct bw_nrc_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!env||!selected||!current||!out)return -1;
    struct bw_nrc_candidate *list=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *list);size_t count=0,at=0,matches=0;int result=-1;
    if(!list||current(opaque)||bw_nrc_candidates(env,list,BW_NRC_CANDIDATE_LIMIT,&count))goto done;
    for(size_t i=0;i<count;i++)if(!strcmp(list[i].subject.title,selected->title)){at=i;matches++;}
    if(!matches){result=nrl_result(out,BW_NRC_UNOWNED,"no_previous_committed_generation",0);goto done;}
    if(matches!=1){nrl_result(out,BW_NRC_UNKNOWN,"multiple_previous_generations_require_observation",-1);goto done;}
    struct bw_nrc_candidate *prior=list+at;
    if(prior->pending||prior->retiring){nrl_result(out,BW_NRC_PENDING,"previous_generation_has_pending_control_history",-1);goto done;}
    unsigned char expected[32];static const char digits[]="0123456789abcdef";
    if(strnlen(selected->prepared_record_hex,65)!=64)goto done;
    for(unsigned i=0;i<32;i++){const char *a=strchr(digits,selected->prepared_record_hex[2*i]),*b=strchr(digits,selected->prepared_record_hex[2*i+1]);if(!a||!b)goto done;expected[i]=(unsigned char)(((a-digits)<<4)|(b-digits));}
    if(!strcmp(prior->subject.game,selected->game)&&!memcmp(prior->subject.prepared_record_sha256,expected,32)){
        result=nrl_result(out,BW_NRC_OWNED,"current_prepared_generation_retained",0);goto done;
    }
    struct nrl_generation_observation observation={.env=env,.current=current,.opaque=opaque};
    observation.remote.connection=(struct bw_sm_connection){port,nrl_generation_current,&observation};
    struct bw_nrc_generation_observer ops={&observation,nrl_generation_current,nrl_generation_sources,nrl_generation_manual};
    result=bw_nrc_observe_superseded(env,&prior->subject,selected,&ops,out);
    if(result&&observation.remote.reason)out->detail=observation.remote.reason;
done:free(list);if(result&&out&&!out->reason)nrl_result(out,BW_NRC_UNKNOWN,"previous_generation_observation_unconfirmed",result);return result;
}
int bw_nrl_begin(const struct bw_nrl_request *q,struct bw_nrl_owner **out,struct bw_nrc_result *r){
    if(out)*out=NULL;
    if(!out||!r||!q||!q->selection||!q->current||!strcmp(q->selection->title,"PPSA99171")||!strcmp(q->selection->title,"PPSA99170")||q->selection->source_kind!=BW_PROVIDER_FOLDER||
       q->selection->source_policy!=BW_PROVIDER_POLICY_V1||q->selection->prepared_policy)return -1;
    struct bw_nrl_owner *p=calloc(1,sizeof *p);struct bw_nrc_candidate *list=NULL;int rc=-1;if(!p)return -1;
    p->request=*q;p->selection=*q->selection;p->request.selection=&p->selection;p->phase=BW_NRL_REGISTER;
    const char *paths[]={q->environment.data_root,q->environment.apps_root,q->environment.database};
    for(unsigned i=0;i<3;i++)if(!paths[i]||strnlen(paths[i],sizeof p->roots[i])>=sizeof p->roots[i])goto done;else strcpy(p->roots[i],paths[i]);
    p->request.environment=(struct bw_nrc_environment){p->roots[0],p->roots[1],p->roots[2]};
    p->remote.connection=(struct bw_sm_connection){q->port,nrl_current,p};
    list=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *list);size_t count=0;
    if(!list||nrl_current(p)||bw_nrc_candidates(&p->request.environment,list,BW_NRC_CANDIDATE_LIMIT,&count))goto done;
    for(size_t i=0;i<count;i++)if(!strcmp(list[i].subject.title,p->selection.title)){
        if(list[i].pending||list[i].retiring){nrl_result(r,BW_NRC_PENDING,"This title has pending registration cleanup. Wait for independent removal before another Play.",-1);goto explicit_failure;}
        /* An existing committed association can be retained, never adopted
         * from path/title alone. Do not merge ambiguous old generations. */
        static const char digits[]="0123456789abcdef";unsigned char expected[32];if(strnlen(p->selection.prepared_record_hex,65)!=64)goto done;
        for(unsigned j=0;j<32;j++){const char *a=strchr(digits,p->selection.prepared_record_hex[j*2]);const char *b=strchr(digits,p->selection.prepared_record_hex[j*2+1]);if(!a||!b)goto done;expected[j]=(unsigned char)(((a-digits)<<4)|(b-digits));}
        if(p->owned||strcmp(list[i].subject.game,p->selection.game))goto done;
        if(memcmp(list[i].subject.prepared_record_sha256,expected,32)){
            int observed=bw_nrl_reconcile_prepared_generation(&p->request.environment,&p->selection,q->port,nrl_current,p,r);
            if(observed){if(observed==BW_NRC_RELEASE_UNCONFIRMED)p->release_unknown=1;goto explicit_failure;}
            if(r->state!=BW_NRC_ABSENT)goto done;
            continue;
        }
        p->owned=1;memcpy(p->registration_run,list[i].subject.run,33);
    }
    enum bw_registration_state before=bw_native_registration_state(p->roots[0],p->roots[1],p->roots[2],&p->selection);
    if(nrl_current(p))goto done;
    enum bw_registration_state after=bw_native_registration_state(p->roots[0],p->roots[1],p->roots[2],&p->selection);
    if(before!=after||nrl_current(p))goto done;
    if(before==BW_REG_MATCH||before==BW_REG_PENDING){
        if(p->owned&&before!=BW_REG_MATCH)goto done;
        nrl_result(r,p->owned?BW_NRC_OWNED:BW_NRC_UNOWNED,p->owned?"Previously committed exact registration retained.":"Preexisting registration remains unowned.",0);
    }else if(before==BW_REG_ABSENT&&!p->owned){
        struct bw_nrc_ops ops=nrl_ops(p);p->initial_absence=1;
        int begun=bw_nrc_begin(&p->request.environment,&p->selection,&ops,&p->ticket,r);
        p->initial_absence=0;if(begun){if(begun==BW_NRC_RELEASE_UNCONFIRMED)p->release_unknown=1;goto explicit_failure;}
        memcpy(p->registration_run,p->selection.run,33);
    }else goto done;
    *out=p;p=NULL;rc=0;goto done;
explicit_failure:rc=-2;
done:
    free(list);const char *detail=p?p->failure:NULL;unsigned release_unknown=0;
    if(p){if(p->ticket&&bw_nrc_ticket_destroy(p->ticket))p->release_unknown=1;release_unknown=p->release_unknown;free(p);}
    int result=rc==-2?-1:rc<0?nrl_result(r,BW_NRC_UNKNOWN,"Registration ownership is uncertain; no new add or scan was requested.",-1):0;
    if(result&&release_unknown)result=BW_NRC_RELEASE_UNCONFIRMED;
    if(result&&r)r->detail=detail;return result;
}
int bw_nrl_registered(struct bw_nrl_owner *p,int status,const struct bw_register_result *registered,struct bw_nrc_result *r){
    if(!p||!r||p->finished)return -1;p->finished=1;p->failure=NULL;int rc=0;
    if(p->ticket){rc=bw_nrc_finish(p->ticket,status,registered,r);p->owned=!rc;int closed=bw_nrc_ticket_destroy(p->ticket);p->ticket=NULL;
        if(closed){p->release_unknown=1;rc=BW_NRC_RELEASE_UNCONFIRMED;r->state=BW_NRC_UNKNOWN;if(!r->stage)r->reason="registration_ticket_release_unconfirmed";}}
    else if(status||!registered||registered->registered!=1||nrl_current(p))rc=-1;
    else nrl_result(r,p->owned?BW_NRC_OWNED:BW_NRC_UNOWNED,p->owned?"Exact prior ownership retained.":"Preexisting registration is not owned by this Play.",0);
    if(!rc&&p->owned&&nrl_witness_capture(p)){rc=-1;nrl_result(r,BW_NRC_UNKNOWN,"Exact retained registration witnesses could not be confirmed; no game was started.",-1);}
    if(rc)r->detail=p->failure;p->registered=!rc;return rc;
}
int bw_nrl_retire(struct bw_nrl_owner *p,const struct bw_native_reader_release *release,struct bw_nrc_retirement_result *out){
    if(!p||!out||!p->finished||!p->registered)return -1;
    memset(out,0,sizeof *out);if(!p->owned)return nrl_result(&out->registration,BW_NRC_UNOWNED,"Preexisting registration was preserved.",0);
    if(!release||strcmp(release->run,p->selection.run)||!release->runtime_released||!release->io_released||!release->private_released)
        return nrl_result(&out->registration,BW_NRC_UNKNOWN,"Exact reader and private-input release is unconfirmed; registration was retained.",-1);
    p->release=*release;p->phase=BW_NRL_RETIRE;
    struct bw_nrc_retirement_ops ops={.registration=nrl_ops(p),.released=nrl_released,.outside_scan_roots=nrl_outside,.manual_present=nrl_manual,.manual_remove=nrl_remove};
    int rc=bw_nrc_retire_present(&p->request.environment,p->selection.title,p->registration_run,&ops,out);
    if(p->failure){out->registration.detail=p->failure;out->registration.reason=bw_nrc_remote_status_message(&p->remote);}return rc;
}
int bw_nrl_destroy(struct bw_nrl_owner **owner){
    if(!owner)return -1;if(!*owner)return 0;struct bw_nrl_owner *p=*owner;int rc=p->release_unknown?-1:0;if(bw_nrc_ticket_destroy(p->ticket))rc=-1;if(nrl_witness_close(p))rc=-1;memset(p,0,sizeof *p);free(p);*owner=NULL;return rc;
}
int bw_nrl_observe(const struct bw_nrc_environment *env,const char title[10],const char run[33],uint16_t port,
    int (*guard)(void *,enum bw_nrl_control),void *opaque,struct bw_nrc_retirement_result *out){
    if(!env||!guard||!out)return -1;
    struct bw_nrl_owner p={.request={.environment=*env,.current=guard,.opaque=opaque},.phase=BW_NRL_RETIRE};
    p.remote.connection=(struct bw_sm_connection){port,nrl_current,&p};
    struct bw_nrc_retirement_observer ops={.opaque=&p,.current=nrl_current,.sources=nrl_sources,
        .outside_scan_roots=nrl_outside,.manual_present=nrl_manual};
    return bw_nrc_retirement_observe(env,title,run,&ops,out);
}
