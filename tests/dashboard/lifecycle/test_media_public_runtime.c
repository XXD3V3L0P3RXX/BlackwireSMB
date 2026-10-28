/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real POSIX inventory and document parser, modeled SDK/read-only title rows.
 * This does not open SQLite, call Sony APIs, or contact a console.
 */
#define _XOPEN_SOURCE 700
#include "media_public_trial.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned cases,checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"media public case%u line%d: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
struct fixture {
    char root[PATH_MAX],apps[PATH_MAX];double clock;
    unsigned own_row,other_row,db_reads,stages,asset_reads,sdk_opens,initializes,terminates,title_calls,batch_calls,waits,reports;
    int current_bad,assets_bad,sdk_bad,initialize_bad,title_busy,db_bad,no_title,create_row,install_result,change_wait,change_init,change_install;
};
static struct fixture *active;
static void path(char out[PATH_MAX],const char *parent,const char *leaf){
    int n=snprintf(out,PATH_MAX,"%s/%s",parent,leaf);CHECK(n>0&&n<PATH_MAX);
}
static void write_bytes(const char *file,const char *bytes){
    int fd=open(file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644);CHECK(fd>=0);
    size_t size=strlen(bytes);CHECK(write(fd,bytes,size)==(ssize_t)size);CHECK(!close(fd));
}
static void make_app(struct fixture *f,const char *title){
    char app[PATH_MAX],sys[PATH_MAX],file[PATH_MAX],json[80];struct stat st;path(app,f->apps,title);
    if(!lstat(app,&st))return;
    CHECK(!mkdir(app,0755));path(sys,app,"sce_sys");CHECK(!mkdir(sys,0755));path(file,sys,"param.json");
    CHECK(snprintf(json,sizeof json,"{\"titleId\":\"%s\"}",title)>0);write_bytes(file,json);
    path(file,sys,"icon0.png");write_bytes(file,"fixture image");
}
static void change_other(struct fixture *f){
    char app[PATH_MAX],sys[PATH_MAX],file[PATH_MAX];path(app,f->apps,"KEEP00001");path(sys,app,"sce_sys");path(file,sys,"param.json");
    write_bytes(file,"{\"titleId\":\"KEEP00001\",\"changed\":true}");
}
static void remove_app(struct fixture *f,const char *title){
    char app[PATH_MAX],sys[PATH_MAX],file[PATH_MAX];struct stat st;path(app,f->apps,title);
    if(lstat(app,&st))return;
    if(S_ISLNK(st.st_mode)){CHECK(!unlink(app));return;}
    CHECK(S_ISDIR(st.st_mode));path(sys,app,"sce_sys");path(file,sys,"param.json");CHECK(!unlink(file));
    path(file,sys,"icon0.png");CHECK(!unlink(file));CHECK(!rmdir(sys));CHECK(!rmdir(app));
}
static void begin(struct fixture *f){
    memset(f,0,sizeof *f);char temporary[]="/tmp/blackwire-media-public-XXXXXX";char *root=mkdtemp(temporary);CHECK(root);
    CHECK(strlen(root)<sizeof f->root);strcpy(f->root,root);path(f->apps,f->root,"apps");CHECK(!mkdir(f->apps,0755));
    f->clock=10;f->other_row=1;f->create_row=1;active=f;make_app(f,"KEEP00001");cases++;
}
static void finish(struct fixture *f){
    CHECK(!strncmp(f->root,"/tmp/blackwire-media-public-",28));remove_app(f,"LINK00001");remove_app(f,BW_MEDIA_PUBLIC_TITLE);remove_app(f,"KEEP00001");
    CHECK(!rmdir(f->apps));CHECK(!rmdir(f->root));active=NULL;
}
int bw_registration_db_titles(const char *database,struct bw_registration_titles *out){
    struct fixture *f=active;CHECK(f&&database&&!strcmp(database,"modeled-read-only-title-provider"));f->db_reads++;memset(out,0,sizeof *out);
    if(f->db_bad)return -1;
    if(f->other_row)strcpy(out->title[out->count++],"KEEP00001");
    if(f->own_row)strcpy(out->title[out->count++],BW_MEDIA_PUBLIC_TITLE);
    return 0;
}
static int current(void *opaque){return ((struct fixture *)opaque)->current_bad?-1:0;}
static int title_idle(void *opaque,const char title[10]){struct fixture *f=opaque;CHECK(strlen(title)==9);return f->title_busy?-1:0;}
static int assets(void *opaque,int stage){
    struct fixture *f=opaque;if(stage)f->stages++;else f->asset_reads++;if(f->assets_bad)return -1;
    if(stage)make_app(f,BW_MEDIA_PUBLIC_TITLE);
    char app[PATH_MAX],sys[PATH_MAX],file[PATH_MAX];struct stat st;path(app,f->apps,BW_MEDIA_PUBLIC_TITLE);path(sys,app,"sce_sys");path(file,sys,"param.json");
    return lstat(app,&st)||!S_ISDIR(st.st_mode)||lstat(file,&st)||!S_ISREG(st.st_mode)?-1:0;
}
static int initialize(void){struct fixture *f=active;f->initializes++;if(f->change_init)change_other(f);return f->initialize_bad?-17:0;}
static int terminate(void){active->terminates++;return 0;}
static int install_common(void){
    struct fixture *f=active;if(f->create_row)f->own_row=1;if(f->change_install)change_other(f);return f->install_result;
}
static int install_title(const char *title,const char *parent,void *unused){
    struct fixture *f=active;char expected[PATH_MAX];CHECK(snprintf(expected,sizeof expected,"%s/",f->apps)>0);
    CHECK(!strcmp(title,BW_MEDIA_PUBLIC_TITLE)&&!strcmp(parent,expected)&&!unused);f->title_calls++;return install_common();
}
static int install_batch(void *unused){CHECK(!unused);active->batch_calls++;return install_common();}
static int sdk_open(void *opaque,struct bw_media_public_sdk *sdk){
    struct fixture *f=opaque;f->sdk_opens++;if(f->sdk_bad)return -1;
    *sdk=(struct bw_media_public_sdk){.initialize=initialize,.terminate=terminate,.title_dir=f->no_title?NULL:install_title,.install_all=install_batch};return 0;
}
static double now(void *opaque){return ((struct fixture *)opaque)->clock;}
static int wait_for(void *opaque,unsigned milliseconds){
    struct fixture *f=opaque;CHECK(milliseconds==250);f->waits++;f->clock+=(double)milliseconds/1000;
    if(f->change_wait&&f->waits==1)change_other(f);return 0;
}
static void report(void *opaque,const struct bw_media_public_record *record){struct fixture *f=opaque;CHECK(record&&record->reason);f->reports++;}
static int run(struct fixture *f,struct bw_media_public_record *record){
    struct bw_media_public_request q={.apps=f->apps,.database="modeled-read-only-title-provider",.opaque=f,.current=current,.title_idle=title_idle,.assets=assets,.sdk_open=sdk_open,.now=now,.wait=wait_for,.report=report};
    return bw_media_public_run(&q,record);
}
static void no_install(const struct fixture *f,const struct bw_media_public_record *r){
    CHECK(!f->title_calls&&!f->batch_calls&&!r->attempted&&!r->returned&&!r->registered&&r->phase==BW_MEDIA_PUBLIC_REFUSED);
}
int main(void){
    struct fixture f;struct bw_media_public_record r={0};
    begin(&f);CHECK(!run(&f,&r));CHECK(r.phase==BW_MEDIA_PUBLIC_VERIFIED&&r.registered&&r.attempted&&r.returned&&r.mode==BW_MEDIA_PUBLIC_TITLE_DIR);
    CHECK(f.title_calls==1&&!f.batch_calls&&f.initializes==1&&f.terminates==1&&f.clock>=12);finish(&f);

    begin(&f);memset(&r,0,sizeof r);make_app(&f,BW_MEDIA_PUBLIC_TITLE);f.own_row=1;CHECK(!run(&f,&r));
    CHECK(r.registered&&!r.attempted&&!r.returned&&!f.sdk_opens&&!f.initializes&&!f.title_calls&&!f.batch_calls&&f.clock>=12);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.no_title=1;CHECK(!run(&f,&r));CHECK(r.mode==BW_MEDIA_PUBLIC_BATCH&&f.batch_calls==1&&!f.title_calls&&f.initializes==1&&f.terminates==1);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.create_row=0;f.install_result=-29;CHECK(run(&f,&r)<0);
    CHECK(r.phase==BW_MEDIA_PUBLIC_UNCONFIRMED&&r.attempted&&r.returned&&!r.registered&&r.platform_status==-29&&f.title_calls==1&&!f.batch_calls&&f.terminates==1);
    unsigned opens=f.sdk_opens,stages=f.stages;CHECK(run(&f,&r)<0);CHECK(f.title_calls==1&&!f.batch_calls&&f.sdk_opens==opens&&f.stages==stages);
    f.own_row=1;CHECK(!run(&f,&r));CHECK(r.registered&&f.title_calls==1&&f.sdk_opens==opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.other_row=0;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.sdk_bad=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(f.sdk_opens==1&&!f.initializes&&!strcmp(r.reason,"normal_public_sdk_unavailable"));finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.initialize_bad=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(f.initializes==1&&!f.terminates&&r.initialize_status==-17);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.current_bad=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.stages&&!f.sdk_opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.assets_bad=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(f.stages==1&&!f.sdk_opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.change_wait=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens&&!strcmp(r.reason,"application_inventory_not_stable_before_sdk"));finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.change_init=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(f.initializes==1&&f.terminates==1&&!strcmp(r.reason,"application_inventory_changed_before_call"));finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.change_install=1;CHECK(run(&f,&r)<0);CHECK(r.phase==BW_MEDIA_PUBLIC_UNCONFIRMED&&r.attempted&&r.returned&&!r.registered&&f.title_calls==1&&f.terminates==1&&!strcmp(r.reason,"application_inventory_changed_after_call"));finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.title_busy=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);f.db_bad=1;CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens&&f.db_reads==1);finish(&f);

    begin(&f);memset(&r,0,sizeof r);char app[PATH_MAX],sys[PATH_MAX],file[PATH_MAX];path(app,f.apps,"KEEP00001");path(sys,app,"sce_sys");path(file,sys,"param.json");
    write_bytes(file,"{\"titleId\":\"KEEP00001\",\"titleId\":\"KEEP00001\"}");CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens);finish(&f);

    begin(&f);memset(&r,0,sizeof r);path(app,f.apps,"LINK00001");path(file,f.apps,"KEEP00001");CHECK(!symlink(file,app));CHECK(run(&f,&r)<0);no_install(&f,&r);CHECK(!f.sdk_opens);finish(&f);

    /* A returned SDK error cannot outweigh a stable, positively observed own
     * row; the row is the existing core's registration proof. */
    begin(&f);memset(&r,0,sizeof r);f.install_result=-31;CHECK(!run(&f,&r));CHECK(r.registered&&r.returned&&r.platform_status==-31&&f.title_calls==1&&!f.batch_calls);finish(&f);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualPublicMediaCore\":true,\"actualPosixInventory\":true,\"actualDocumentParser\":true,\"sdkClockAndReadonlyTitleProviderModeled\":true,\"actualSqlite\":false,\"consoleActions\":0}\n",cases,checks);return 0;
}
