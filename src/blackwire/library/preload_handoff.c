#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_PRELOAD_HANDOFF_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
#include "preload_handoff.h"
#include "reader_projection.h"
#include "source_binding.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <time.h>

#define PH_INITIALIZED UINT32_C(0x42575048)
/* A process-private live capability, not a serialized FD receipt. The normal
 * game lease admits one retained consumer handoff at a time. */
static struct bw_preload_handoff *ph_live_consumer;
static int ph_consumer_instance(const struct bw_preload_handoff *o) {
    return !o->retained_descriptors||
        (__atomic_load_n(&ph_live_consumer,__ATOMIC_ACQUIRE)==o&&o->producer_pid==getpid());
}
static void ph_forget_closed(struct bw_preload_handoff *o) {
    if(o->retained_descriptors&&!o->share.descriptor_held&&!o->selection.descriptor_held&&!o->projection.descriptor_held) {
        struct bw_preload_handoff *expected=o;
        o->descriptors_closed=1;
        (void)__atomic_compare_exchange_n(&ph_live_consumer,&expected,NULL,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
}
struct ph_operation {
    struct bw_preload_handoff *owner;
    int root,folder;
    int (*current)(void *);
    void *opaque;
    double deadline;
};
static void ph_clear(void *data,size_t size) {
    volatile unsigned char *p=data;while(size--)*p++=0;
}
static int ph_zero(const void *data,size_t size) {
    const unsigned char *p=data;while(size--)if(*p++)return 0;return 1;
}
static int ph_guard(struct ph_operation *p) {
    struct timespec stamp;
    if(!ph_consumer_instance(p->owner)||!p->current||!__builtin_isfinite(p->deadline)||p->deadline<=0||clock_gettime(CLOCK_MONOTONIC,&stamp))return -1;
    double now=(double)stamp.tv_sec+(double)stamp.tv_nsec/1e9;
    if(!__builtin_isfinite(now)||now>=p->deadline||p->current(p->opaque)||clock_gettime(CLOCK_MONOTONIC,&stamp))return -1;
    now=(double)stamp.tv_sec+(double)stamp.tv_nsec/1e9;
    return !__builtin_isfinite(now)||now>=p->deadline?-1:0;
}
static int ph_node(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
static int ph_file_same(const struct stat *a,const struct stat *b) {
    return ph_node(a,b)&&S_ISREG(b->st_mode)&&b->st_nlink==1&&(b->st_mode&07777)==0600&&
        a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int ph_paths(struct ph_operation *p) {
    char path[BW_PRELOAD_HANDOFF_ROOT_BYTES+16],resolved[PATH_MAX];struct stat named,held;
    const struct bw_preload_handoff *o=p->owner;
    if(ph_guard(p)||!realpath(o->root,resolved)||strcmp(o->root,resolved)||lstat(o->root,&named)||
       !S_ISDIR(named.st_mode)||fstat(p->root,&held)||!ph_node(&o->root_identity,&held)||!ph_node(&held,&named))return -1;
    int n=snprintf(path,sizeof path,"%s/fakelib2",o->root);
    if(n<0||(size_t)n>=sizeof path||!realpath(path,resolved)||strcmp(path,resolved)||
       cp_fstatat(p->root,"fakelib2",&named,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(named.st_mode)||
       fstat(p->folder,&held)||!ph_node(&o->fakelib_identity,&held)||!ph_node(&held,&named)||ph_guard(p))return -1;
    return 0;
}
static int ph_open(struct ph_operation *p,int capture) {
    char resolved[PATH_MAX];struct stat named,held;
    if(ph_guard(p)||!realpath(p->owner->root,resolved)||strcmp(p->owner->root,resolved)||
       lstat(p->owner->root,&named)||!S_ISDIR(named.st_mode))return -1;
    p->root=open(p->owner->root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(p->root<0||fstat(p->root,&held)||!ph_node(&named,&held))return -1;
    if(capture)p->owner->root_identity=held;
    else if(!ph_node(&p->owner->root_identity,&held))return -1;
    p->folder=v2_openat(p->root,"fakelib2",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(p->folder<0||fstat(p->folder,&held)||!S_ISDIR(held.st_mode))return -1;
    if(capture)p->owner->fakelib_identity=held;
    return ph_paths(p);
}
static int ph_absent(struct ph_operation *p,const char *leaf) {
    struct stat named;
    return !ph_paths(p)&&cp_fstatat(p->folder,leaf,&named,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT&&!ph_paths(p)?0:-1;
}
static int ph_verify(struct ph_operation *p,const char *leaf,int fd,struct bw_preload_handoff_file *file,int capture) {
    struct stat before,named,after;struct cp_sha hash;uint8_t digest[32],bytes[32768];int result=-1;
    cp_sha_init(&hash);memset(digest,0,sizeof digest);memset(bytes,0,sizeof bytes);
    size_t maximum=!strcmp(leaf,BW_PRELOAD_HANDOFF_PROJECTION)?BW_READER_PROJECTION_LIMIT:BW_READER_SELECTION_BYTES;
    if(!file->created||!file->identity_known||file->bytes>maximum||ph_paths(p)||
       fstat(fd,&before)||!ph_node(&file->identity,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||
       (before.st_mode&07777)!=0600||before.st_size<0||(uint64_t)before.st_size!=file->bytes||
       (!capture&&!ph_file_same(&file->identity,&before))||
       cp_fstatat(p->folder,leaf,&named,AT_SYMLINK_NOFOLLOW)||!ph_file_same(&before,&named))goto done;
    for(size_t at=0;at<file->bytes;) {
        size_t chunk=!strcmp(leaf,BW_PRELOAD_HANDOFF_PROJECTION)?sizeof bytes:256u;
        size_t take=file->bytes-at;if(take>chunk)take=chunk;
        if(ph_paths(p))goto done;
        ssize_t got=pread(fd,bytes,take,(off_t)at);
        if(got<0&&errno==EINTR)continue;
        if(got<=0||(size_t)got>take)goto done;
        cp_sha_update(&hash,bytes,(size_t)got);at+=(size_t)got;
    }
    if(ph_paths(p)||pread(fd,bytes,1,(off_t)file->bytes)!=0||fstat(fd,&after)||!ph_file_same(&before,&after)||
       cp_fstatat(p->folder,leaf,&named,AT_SYMLINK_NOFOLLOW)||!ph_file_same(&before,&named)||ph_paths(p))goto done;
    cp_sha_final(&hash,digest);if(memcmp(digest,file->sha256,32))goto done;
    if(capture)file->identity=after;
    result=0;
done:
    ph_clear(bytes,sizeof bytes);ph_clear(digest,sizeof digest);ph_clear(&hash,sizeof hash);return result;
}
static int ph_create(struct ph_operation *p,const char *leaf,const char *bytes,size_t size,struct bw_preload_handoff_file *file) {
    int fd=-1,result=-1;struct stat named;
    if(ph_absent(p,leaf))return -1;
    fd=v2_openat(p->folder,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
    if(fd<0)return -1;
    file->created=1;file->bytes=size;cp_sha_bytes(bytes,size,file->sha256);
    if(fstat(fd,&file->identity)||!S_ISREG(file->identity.st_mode)||file->identity.st_nlink!=1||
       cp_fstatat(p->folder,leaf,&named,AT_SYMLINK_NOFOLLOW)||!ph_node(&file->identity,&named))goto done;
    file->identity_known=1;
    if(ph_paths(p)||fchmod(fd,0600)||fstat(fd,&named)||named.st_dev!=file->identity.st_dev||named.st_ino!=file->identity.st_ino)goto done;
    file->identity=named;
    for(size_t at=0;at<size;) {
        if(ph_paths(p))goto done;
        ssize_t put=write(fd,bytes+at,size-at);
        if(put<0&&errno==EINTR)continue;
        if(put<=0||(size_t)put>size-at)goto done;
        at+=(size_t)put;
    }
    if(ph_paths(p)||fsync(fd)||ph_verify(p,leaf,fd,file,1)||fsync(p->folder)||ph_paths(p))goto done;
    file->verified=1;result=0;
done:
    if(p->owner->retained_descriptors&&result==0) {
        file->producer_identity=file->identity;file->descriptor=fd;file->descriptor_held=1;
        return 0;
    }
    if(p->owner->retained_descriptors) {
        file->close_attempted=1;file->close_result=close(fd);file->close_error=errno;
        if(file->close_result){file->descriptor=fd;file->descriptor_held=1;result=-1;}
    } else if(close(fd))result=-1;
    return result;
}
static int ph_encode_share(const struct bw_share_config *c,char bytes[1024],size_t *size) {
    struct bw_share_config checked={0};int result=-1;
    if(!c||!memchr(c->server,0,sizeof c->server)||!memchr(c->share,0,sizeof c->share)||
       !memchr(c->user,0,sizeof c->user)||!memchr(c->password,0,sizeof c->password))goto done;
    int n=snprintf(bytes,1024,"server=%s\nshare=%s\nuser=%s\npassword=%s\npath=\n",c->server,c->share,c->user,c->password);
    if(n<=0||n>=1024||bw_share_parse(&checked,bytes,(size_t)n)||strcmp(c->server,checked.server)||
       strcmp(c->share,checked.share)||strcmp(c->user,checked.user)||strcmp(c->password,checked.password))goto done;
    *size=(size_t)n;result=0;
done:
    bw_share_clear(&checked);return result;
}
static int ph_fresh_verify(struct ph_operation *p,const char *leaf,struct bw_preload_handoff_file *file) {
    if(p->owner->retained_descriptors)
        return !file->descriptor_held||file->close_attempted?-1:ph_verify(p,leaf,file->descriptor,file,0);
    int fd=v2_openat(p->folder,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;
    int result=ph_verify(p,leaf,fd,file,0);if(close(fd))result=-1;return result;
}
static int ph_publish(const struct bw_preload_handoff_request *r,struct bw_preload_handoff *o,int retained,const void *projection,size_t projection_size) {
    char selection[BW_READER_SELECTION_BYTES+1]={0},share[1024]={0};size_t selection_size=0,share_size=0;
    struct bw_provider_binding binding={0};struct ph_operation p={o,-1,-1,NULL,NULL,0};int entered=0,result=BW_PRELOAD_HANDOFF_REFUSED;
    if(!o||!ph_zero(o,sizeof *o)||!r||!r->root||!r->selection||!r->connection||!r->current||
       r->root[0]!='/'||strnlen(r->root,sizeof o->root)>=sizeof o->root||
       (r->selection->source_kind!=BW_PROVIDER_FOLDER&&r->selection->source_kind!=BW_PROVIDER_CONTAINER)||r->selection->source_policy!=BW_PROVIDER_POLICY_V1||
       (bw_reader_selection_is_projection(r->selection)?(!retained||!projection||projection_size<BW_READER_PROJECTION_HEADER||projection_size>BW_READER_PROJECTION_LIMIT||memcmp(projection,"BWDAT001",8)):(projection||projection_size))||
       bw_reader_selection_serialize(r->selection,selection,sizeof selection,&selection_size)||
       ph_encode_share(r->connection,share,&share_size)||
       bw_provider_binding_init(&binding,r->connection,r->selection->source,r->selection->title,(enum bw_provider_kind)r->selection->source_kind)||
       strcmp(binding.binding.game,r->selection->game))goto done;
    const char *leaf=strrchr(r->root,'/');
    if(!leaf||strcmp(leaf+1,r->selection->prepared_policy?r->selection->prepared_game:r->selection->game))goto done;
    pid_t producer=0;
    if(retained) {
        struct bw_preload_handoff *expected=NULL;producer=getpid();
        if(producer<=0||!__atomic_compare_exchange_n(&ph_live_consumer,&expected,o,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))goto done;
    }
    memcpy(o->root,r->root,strlen(r->root)+1);o->initialized=PH_INITIALIZED;o->busy=1;entered=1;
    o->retained_descriptors=retained;o->producer_pid=producer;o->projected=bw_reader_selection_is_projection(r->selection);
    p.current=r->current;p.opaque=r->opaque;p.deadline=r->deadline;
    if(ph_open(&p,1)||ph_absent(&p,BW_PRELOAD_HANDOFF_SHARE)||ph_absent(&p,BW_PRELOAD_HANDOFF_SELECTION)||ph_absent(&p,BW_PRELOAD_HANDOFF_PROJECTION)||
       ph_create(&p,BW_PRELOAD_HANDOFF_SHARE,share,share_size,&o->share)||
       (o->projected&&ph_create(&p,BW_PRELOAD_HANDOFF_PROJECTION,projection,projection_size,&o->projection))||
       ph_absent(&p,BW_PRELOAD_HANDOFF_SELECTION)||
       ph_create(&p,BW_PRELOAD_HANDOFF_SELECTION,selection,selection_size,&o->selection)||
       ph_fresh_verify(&p,BW_PRELOAD_HANDOFF_SHARE,&o->share)||
       (o->projected&&ph_fresh_verify(&p,BW_PRELOAD_HANDOFF_PROJECTION,&o->projection))||
       ph_fresh_verify(&p,BW_PRELOAD_HANDOFF_SELECTION,&o->selection)||ph_paths(&p))goto done;
    result=BW_PRELOAD_HANDOFF_OK;
done:
    ph_clear(selection,sizeof selection);ph_clear(share,sizeof share);ph_clear(&binding,sizeof binding);
    if(p.folder>=0&&close(p.folder))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(p.root>=0&&close(p.root))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(entered) {
        if(result&& (o->share.created||o->selection.created||o->projection.created))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
        o->published=result==BW_PRELOAD_HANDOFF_OK;o->last_status=result;o->busy=0;
        if(retained&&!o->share.created&&!o->selection.created&&!o->projection.created)ph_forget_closed(o);
    }
    return result;
}
int bw_preload_handoff_publish(const struct bw_preload_handoff_request *r,struct bw_preload_handoff *o) {
    return ph_publish(r,o,0,NULL,0);
}
int bw_preload_handoff_publish_retained(const struct bw_preload_handoff_request *r,struct bw_preload_handoff *o) {
    return ph_publish(r,o,1,NULL,0);
}
int bw_preload_handoff_publish_projection_retained(const struct bw_preload_handoff_request *r,const void *bytes,size_t size,struct bw_preload_handoff *o){
    return ph_publish(r,o,1,bytes,size);
}
static int ph_owned_open(struct ph_operation *p,const char *leaf,struct bw_preload_handoff_file *file,int *out) {
    if(!file->created)return ph_absent(p,leaf);
    if(!file->verified||file->removed)return -1;
    if(p->owner->retained_descriptors) {
        if(!file->descriptor_held||file->close_attempted||(file->ownership_attempted&&!file->ownership_confirmed))return -1;
        *out=file->descriptor;return ph_verify(p,leaf,*out,file,0);
    }
    *out=v2_openat(p->folder,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    return *out<0?-1:ph_verify(p,leaf,*out,file,0);
}
static int ph_assign_file(struct ph_operation *p,const char *leaf,struct bw_preload_handoff_file *file,uid_t uid) {
    int fd=file->descriptor;
    if(ph_verify(p,leaf,fd,file,0)||ph_paths(p))return -1;
    if(file->identity.st_uid==uid) {
        file->ownership_skipped=1;
        if(ph_verify(p,leaf,fd,file,0))return -1;
        file->ownership_confirmed=1;return 0;
    }
    file->ownership_attempted=1;
    file->ownership_result=fchown(fd,uid,(gid_t)-1);file->ownership_error=errno;
    if(file->ownership_result)return -1;
    struct stat after;
    if(fsync(fd)||fstat(fd,&after)||ph_paths(p)||!S_ISREG(after.st_mode)||after.st_nlink!=1||
       after.st_uid!=uid||after.st_gid!=file->identity.st_gid||after.st_mode!=file->identity.st_mode||
       after.st_dev!=file->identity.st_dev||after.st_ino!=file->identity.st_ino||after.st_size!=file->identity.st_size||
       after.st_mtim.tv_sec!=file->identity.st_mtim.tv_sec||after.st_mtim.tv_nsec!=file->identity.st_mtim.tv_nsec)return -1;
    /* Ownership may change ctime, never identity/body/mode/GID/mtime. Verify
     * the actual post-change snapshot fully before committing it to owner. */
    struct bw_preload_handoff_file candidate=*file;candidate.identity=after;
    if(ph_verify(p,leaf,fd,&candidate,0))return -1;
    file->identity=after;file->ownership_confirmed=1;return 0;
}
int bw_preload_handoff_assign_consumer(struct bw_preload_handoff *o,uid_t uid,int (*current)(void *),void *opaque,double deadline) {
    if(!o||o->initialized!=PH_INITIALIZED||!o->retained_descriptors||!ph_consumer_instance(o)||
       o->busy||!o->published||o->consumer_assignment_started||o->release_started||o->descriptor_close_started||
       o->released||uid==(uid_t)-1||!current)return BW_PRELOAD_HANDOFF_REFUSED;
    struct ph_operation p={o,-1,-1,current,opaque,deadline};int share=-1,selection=-1,projection=-1,result=BW_PRELOAD_HANDOFF_REFUSED;
    o->busy=1;
    if(ph_open(&p,0)||ph_owned_open(&p,BW_PRELOAD_HANDOFF_SHARE,&o->share,&share)||
       ph_owned_open(&p,BW_PRELOAD_HANDOFF_SELECTION,&o->selection,&selection)||
       ph_owned_open(&p,BW_PRELOAD_HANDOFF_PROJECTION,&o->projection,&projection)||ph_paths(&p))goto done;
    o->consumer_uid=uid;o->consumer_assignment_started=1;result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(ph_assign_file(&p,BW_PRELOAD_HANDOFF_SHARE,&o->share,uid)||
       (o->projected&&ph_assign_file(&p,BW_PRELOAD_HANDOFF_PROJECTION,&o->projection,uid))||
       ph_assign_file(&p,BW_PRELOAD_HANDOFF_SELECTION,&o->selection,uid)||
       ph_verify(&p,BW_PRELOAD_HANDOFF_SHARE,share,&o->share,0)||
       (o->projected&&ph_verify(&p,BW_PRELOAD_HANDOFF_PROJECTION,projection,&o->projection,0))||
       ph_verify(&p,BW_PRELOAD_HANDOFF_SELECTION,selection,&o->selection,0)||fsync(p.folder)||ph_paths(&p))goto done;
    o->consumer_assigned=1;result=BW_PRELOAD_HANDOFF_OK;
done:
    if(p.folder>=0&&close(p.folder))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(p.root>=0&&close(p.root))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(result)o->consumer_assigned=0;
    o->last_status=result;o->busy=0;return result;
}
int bw_preload_handoff_publish_for_consumer(const struct bw_preload_handoff_request *r,uid_t uid,struct bw_preload_handoff *o) {
    if(uid==(uid_t)-1)return BW_PRELOAD_HANDOFF_REFUSED;
    int result=bw_preload_handoff_publish_retained(r,o);
    return result?result:bw_preload_handoff_assign_consumer(o,uid,r->current,r->opaque,r->deadline);
}
static int ph_close_retained(struct ph_operation *p,struct bw_preload_handoff_file *file) {
    if(!file->descriptor_held)return 0;
    if(file->close_attempted||ph_guard(p))return -1;
    struct stat held;
    if(fstat(file->descriptor,&held)||!S_ISREG(held.st_mode)||held.st_dev!=file->producer_identity.st_dev||
       held.st_ino!=file->producer_identity.st_ino||ph_guard(p))return -1;
    file->close_attempted=1;file->close_result=close(file->descriptor);file->close_error=errno;
    if(file->close_result)return -1;
    file->descriptor_held=0;file->descriptor=-1;return 0;
}
static int ph_discharge(struct ph_operation *p) {
    struct bw_preload_handoff *o=p->owner;
    if(ph_guard(p))return -1;
    o->descriptor_close_started=1;
    int selection=ph_close_retained(p,&o->selection),projection=ph_close_retained(p,&o->projection),share=ph_close_retained(p,&o->share);
    ph_forget_closed(o);return selection||projection||share?-1:0;
}
static int ph_remove(struct ph_operation *p,const char *leaf,int fd,struct bw_preload_handoff_file *file) {
    if(!file->created)return ph_absent(p,leaf);
    if(ph_verify(p,leaf,fd,file,0)||ph_paths(p)||cp_unlinkat(p->folder,leaf))return -1;
    file->removed=1;
    return ph_absent(p,leaf)||fsync(p->folder)||ph_paths(p)?-1:0;
}
int bw_preload_handoff_release(struct bw_preload_handoff *o,int (*exited)(void *),void *opaque,double deadline) {
    if(!o||o->initialized!=PH_INITIALIZED||!ph_consumer_instance(o)||o->busy||o->release_started||
       o->descriptor_close_started||o->released||!exited)return BW_PRELOAD_HANDOFF_REFUSED;
    struct ph_operation p={o,-1,-1,exited,opaque,deadline};int share=-1,selection=-1,projection=-1,result=BW_PRELOAD_HANDOFF_REFUSED;
    o->busy=1;
    if(ph_open(&p,0)||ph_owned_open(&p,BW_PRELOAD_HANDOFF_SHARE,&o->share,&share)||
       ph_owned_open(&p,BW_PRELOAD_HANDOFF_SELECTION,&o->selection,&selection)||
       ph_owned_open(&p,BW_PRELOAD_HANDOFF_PROJECTION,&o->projection,&projection)||ph_paths(&p))goto done;
    o->release_started=1;result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(ph_remove(&p,BW_PRELOAD_HANDOFF_SELECTION,selection,&o->selection)||
       ph_remove(&p,BW_PRELOAD_HANDOFF_PROJECTION,projection,&o->projection)||
       ph_remove(&p,BW_PRELOAD_HANDOFF_SHARE,share,&o->share)||ph_paths(&p))goto done;
    result=BW_PRELOAD_HANDOFF_OK;
done:
    if(o->retained_descriptors) {
        if(o->release_started&&ph_discharge(&p))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    } else {
        if(selection>=0&&close(selection))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
        if(projection>=0&&close(projection))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
        if(share>=0&&close(share))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    }
    if(p.folder>=0&&close(p.folder))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    if(p.root>=0&&close(p.root))result=BW_PRELOAD_HANDOFF_UNCONFIRMED;
    o->released=result==BW_PRELOAD_HANDOFF_OK;o->last_status=result;o->busy=0;return result;
}
int bw_preload_handoff_close_descriptors(struct bw_preload_handoff *o,int (*exited)(void *),void *opaque,double deadline) {
    if(!o||o->initialized!=PH_INITIALIZED||!o->retained_descriptors||!ph_consumer_instance(o)||
       o->busy||o->descriptor_close_started||o->descriptors_closed||!exited)return BW_PRELOAD_HANDOFF_REFUSED;
    struct ph_operation p={o,-1,-1,exited,opaque,deadline};o->busy=1;
    int result=ph_guard(&p)?BW_PRELOAD_HANDOFF_REFUSED:ph_discharge(&p)?BW_PRELOAD_HANDOFF_UNCONFIRMED:BW_PRELOAD_HANDOFF_OK;
    o->last_status=result;o->busy=0;return result;
}
