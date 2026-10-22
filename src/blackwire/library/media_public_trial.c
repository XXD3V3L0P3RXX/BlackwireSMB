/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "media_public_trial.h"
#include "native_document.h"
#include "copy_sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MP_PARAM_LIMIT 65536u
struct mp_entry {char title[10];struct stat app,sys,param;uint8_t param_sha[32];};
struct mp_inventory {struct stat root;size_t count;struct mp_entry entries[BW_MEDIA_PUBLIC_APP_LIMIT];};
static int mp_title(const char *s){
    if(!s||strlen(s)!=9)return 0;
    for(unsigned i=0;i<9;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')))return 0;
    return 1;
}
static int mp_same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
        a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int mp_child(int parent,const char *name,int fd,const struct stat *expected){
    struct stat held,named;return fstat(fd,&held)||fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||
        !mp_same(expected,&held)||!mp_same(&held,&named)?-1:0;
}
struct mp_parameter {char title[10];unsigned seen;};
static int mp_field(void *opaque,const char *key,size_t size,const void *bytes,size_t n){
    struct mp_parameter *p=opaque;if(size!=7||memcmp(key,"titleId",7))return 0;
    return p->seen++||bw_document_string(bytes,n,p->title,sizeof p->title)||!mp_title(p->title)?-1:0;
}
static int mp_parameter(int sys,struct mp_entry *entry){
    int fd=openat(sys,"param.json",O_RDONLY|O_NONBLOCK|O_NOFOLLOW),result=-1;void *body=NULL;
    if(fd<0)return -1;
    if(fstat(fd,&entry->param)||!S_ISREG(entry->param.st_mode)||entry->param.st_nlink!=1||
       entry->param.st_size<=0||entry->param.st_size>MP_PARAM_LIMIT)goto done;
    size_t size=(size_t)entry->param.st_size,at=0;body=malloc(size);if(!body)goto done;
    while(at<size){ssize_t n=pread(fd,(char *)body+at,size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;at+=(size_t)n;}
    struct mp_parameter parameter={0};
    if(bw_document_fields(body,size,mp_field,&parameter)||parameter.seen!=1||strcmp(parameter.title,entry->title)||
       mp_child(sys,"param.json",fd,&entry->param))goto done;
    cp_sha_bytes(body,size,entry->param_sha);result=0;
done:
    free(body);if(close(fd))result=-1;return result;
}
static int mp_order(const void *a,const void *b){return strcmp(((const struct mp_entry *)a)->title,((const struct mp_entry *)b)->title);}
static int mp_inventory(const struct bw_media_public_request *q,struct mp_inventory *out){
    memset(out,0,sizeof *out);char canonical[PATH_MAX];struct stat named;int root=-1,result=-1;DIR *dir=NULL;
    if(q->current(q->opaque)||!realpath(q->apps,canonical)||strcmp(q->apps,canonical)||lstat(q->apps,&named)||!S_ISDIR(named.st_mode))goto done;
    root=open(q->apps,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(root<0||fstat(root,&out->root)||!mp_same(&named,&out->root))goto done;
    int scan=openat(root,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(scan<0)goto done;
    dir=fdopendir(scan);if(!dir){close(scan);goto done;}
    for(;;){
        errno=0;struct dirent *item=readdir(dir);if(!item){if(errno)goto done;break;}
        if(!strcmp(item->d_name,".")||!strcmp(item->d_name,".."))continue;
        if(q->current(q->opaque)||out->count==BW_MEDIA_PUBLIC_APP_LIMIT||!mp_title(item->d_name))goto done;
        struct mp_entry *entry=&out->entries[out->count];strcpy(entry->title,item->d_name);
        int app=-1,sys=-1,bad=1;
        app=openat(root,item->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(app>=0&&!fstat(app,&entry->app)&&S_ISDIR(entry->app.st_mode)&&!mp_child(root,item->d_name,app,&entry->app)){
            sys=openat(app,"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
            if(sys>=0&&!fstat(sys,&entry->sys)&&S_ISDIR(entry->sys.st_mode)&&!mp_child(app,"sce_sys",sys,&entry->sys)&&
               !mp_parameter(sys,entry)&&!mp_child(app,"sce_sys",sys,&entry->sys)&&!mp_child(root,item->d_name,app,&entry->app)&&
               !q->title_idle(q->opaque,entry->title))bad=0;
        }
        if(sys>=0&&close(sys))bad=1;if(app>=0&&close(app))bad=1;if(bad)goto done;out->count++;
    }
    struct stat held;
    if(q->current(q->opaque)||fstat(root,&held)||lstat(q->apps,&named)||!mp_same(&out->root,&held)||!mp_same(&held,&named)||
       !realpath(q->apps,canonical)||strcmp(q->apps,canonical))goto done;
    qsort(out->entries,out->count,sizeof *out->entries,mp_order);result=0;
done:
    if(dir&&closedir(dir))result=-1;if(root>=0&&close(root))result=-1;return result;
}
static int mp_inventory_equal(const struct mp_inventory *a,const struct mp_inventory *b){
    if(a->count!=b->count||!mp_same(&a->root,&b->root))return 0;
    for(size_t i=0;i<a->count;i++){const struct mp_entry *x=a->entries+i,*y=b->entries+i;
        if(strcmp(x->title,y->title)||!mp_same(&x->app,&y->app)||!mp_same(&x->sys,&y->sys)||!mp_same(&x->param,&y->param)||
           memcmp(x->param_sha,y->param_sha,32))return 0;}
    return 1;
}
static int mp_contains(const struct bw_registration_titles *titles,const char *title){
    for(size_t i=0;i<titles->count;i++)if(!strcmp(titles->title[i],title))return 1;return 0;
}
static int mp_titles_equal(const struct bw_registration_titles *a,const struct bw_registration_titles *b,int allow_own){
    size_t i=0,j=0;
    for(;;){
        if(allow_own){while(i<a->count&&!strcmp(a->title[i],BW_MEDIA_PUBLIC_TITLE))i++;while(j<b->count&&!strcmp(b->title[j],BW_MEDIA_PUBLIC_TITLE))j++;}
        if(i==a->count||j==b->count)return i==a->count&&j==b->count;
        if(strcmp(a->title[i++],b->title[j++]))return 0;
    }
}
static int mp_snapshot(const struct bw_media_public_request *q,struct mp_inventory *inventory,struct bw_registration_titles *titles){
    if(mp_inventory(q,inventory)||bw_registration_db_titles(q->database,titles)||q->current(q->opaque))return -1;
    int own=0;
    for(size_t i=0;i<inventory->count;i++){
        const char *title=inventory->entries[i].title;if(!strcmp(title,BW_MEDIA_PUBLIC_TITLE)){own++;continue;}
        if(!mp_contains(titles,title))return -1;
    }
    return own==1?0:-1;
}
static int mp_refuse(struct bw_media_public_record *r,const char *reason){
    r->phase=r->attempted?BW_MEDIA_PUBLIC_UNCONFIRMED:BW_MEDIA_PUBLIC_REFUSED;r->reason=reason;return -1;
}
static int mp_observe(const struct bw_media_public_request *q,struct bw_media_public_record *r,
    const struct mp_inventory *before,const struct bw_registration_titles *before_titles){
    struct mp_inventory *after=calloc(1,sizeof *after);struct bw_registration_titles *titles=calloc(1,sizeof *titles);
    double began=q->now(q->opaque),positive=-1,last=began;int result=-1;
    r->phase=BW_MEDIA_PUBLIC_OBSERVING;r->registered=0;r->reason="own_row_not_confirmed";
    if(!after||!titles||began<0)goto done;
    for(unsigned polls=0;polls<121;polls++){
        double at=q->now(q->opaque);if(at<last||at<0||at-began>30.0)break;last=at;
        if(q->current(q->opaque)||q->assets(q->opaque,0)||mp_snapshot(q,after,titles))break;
        at=q->now(q->opaque);if(at<last||at<0||at-began>30.0)break;last=at;
        if(!mp_inventory_equal(before,after)||!mp_titles_equal(before_titles,titles,1)){r->reason="application_inventory_changed_after_call";break;}
        if(mp_contains(titles,BW_MEDIA_PUBLIC_TITLE)){
            if(positive<0)positive=at;
            if(at-positive>=2.0){r->registered=1;r->phase=BW_MEDIA_PUBLIC_VERIFIED;r->reason="own_row_observed";result=0;break;}
        }else positive=-1;
        if(at-began>=30.0||q->wait(q->opaque,250))break;
    }
done:
    free(after);free(titles);if(result)r->phase=BW_MEDIA_PUBLIC_UNCONFIRMED;return result;
}
int bw_media_public_run(const struct bw_media_public_request *q,struct bw_media_public_record *r){
    if(!q||!r||!q->apps||!q->database||!q->current||!q->title_idle||!q->assets||!q->sdk_open||!q->now||!q->wait)return -1;
    struct mp_inventory *first=calloc(1,sizeof *first),*last=calloc(1,sizeof *last);
    struct bw_registration_titles *titles=calloc(1,sizeof *titles),*checked=calloc(1,sizeof *checked);
    struct bw_media_public_sdk sdk={0};int initialized=0,result=-1;
    r->phase=BW_MEDIA_PUBLIC_CHECKING;r->registered=0;
    if(!first||!last||!titles||!checked){mp_refuse(r,"observation_allocation_failed");goto done;}
    if(q->current(q->opaque)||q->assets(q->opaque,r->attempted?0:1)||mp_snapshot(q,first,titles)){
        mp_refuse(r,"idle_assets_inventory_or_other_title_coverage_unconfirmed");goto done;}
    if(mp_contains(titles,BW_MEDIA_PUBLIC_TITLE)){
        /* Existing own row needs no SDK load, initialization or install call. */
        result=mp_observe(q,r,first,titles);goto done;
    }
    if(r->attempted){result=mp_observe(q,r,first,titles);goto done;}
    if(q->wait(q->opaque,250)||mp_snapshot(q,last,checked)||!mp_inventory_equal(first,last)||!mp_titles_equal(titles,checked,0)||
       q->assets(q->opaque,0)||q->current(q->opaque)){mp_refuse(r,"application_inventory_not_stable_before_sdk");goto done;}
    if(q->sdk_open(q->opaque,&sdk)||!sdk.initialize||!sdk.terminate||(!sdk.title_dir&&!sdk.install_all)){
        mp_refuse(r,"normal_public_sdk_unavailable");goto done;}
    if(q->current(q->opaque)){mp_refuse(r,"idle_or_owner_changed_before_initialize");goto done;}
    r->initialize_status=sdk.initialize();if(r->initialize_status){mp_refuse(r,"normal_public_sdk_initialize_failed");goto done;}initialized=1;
    /* SDK loading/initialization grants no permission to skip final fences. */
    if(mp_snapshot(q,last,checked)||!mp_inventory_equal(first,last)||!mp_titles_equal(titles,checked,1)||
       q->assets(q->opaque,0)||q->current(q->opaque)){mp_refuse(r,"application_inventory_changed_before_call");goto done;}
    if(mp_contains(checked,BW_MEDIA_PUBLIC_TITLE)){result=mp_observe(q,r,first,titles);goto done;}
    char parent[PATH_MAX];int n=snprintf(parent,sizeof parent,"%s/",q->apps);
    if(n<0||(size_t)n>=sizeof parent){mp_refuse(r,"application_parent_too_long");goto done;}
    r->mode=sdk.title_dir?BW_MEDIA_PUBLIC_TITLE_DIR:BW_MEDIA_PUBLIC_BATCH;
    r->attempted=1;r->returned=0;r->phase=BW_MEDIA_PUBLIC_OBSERVING;r->reason="platform_call_in_progress";
    if(q->report)q->report(q->opaque,r);
    r->platform_status=sdk.title_dir?sdk.title_dir(BW_MEDIA_PUBLIC_TITLE,parent,NULL):sdk.install_all(NULL);
    r->returned=1; /* A returned failure is independent of game ownership. */
    if(q->report)q->report(q->opaque,r);
    result=mp_observe(q,r,first,titles);
done:
    if(initialized&&sdk.terminate)r->terminate_status=sdk.terminate();
    if(q->report)q->report(q->opaque,r);
    free(first);free(last);free(titles);free(checked);return result;
}
const char *bw_media_public_phase_name(enum bw_media_public_phase phase){
    static const char *const names[]={"idle","checking","observing","verified","refused","unconfirmed"};
    return (unsigned)phase<sizeof names/sizeof *names?names[phase]:"unconfirmed";
}
const char *bw_media_public_mode_name(enum bw_media_public_mode mode){
    return mode==BW_MEDIA_PUBLIC_TITLE_DIR?"public_title_dir":mode==BW_MEDIA_PUBLIC_BATCH?"public_batch":"none";
}
