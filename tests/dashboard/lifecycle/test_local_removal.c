/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real selected-game filesystem, readonly SQLite admission and exact removal.
 * Only the normal app-manager/SMP transport effects are modeled. */
#define BD_LOCAL_REMOVAL_HOST_TEST 1
#include "local_removal.c"
#include <sqlite3.h>

static unsigned checks,uninstalls,manual_calls,guards,progress_calls,collect_calls;
static char area[BD_SOURCE_BYTES],data[BD_SOURCE_BYTES],apps[BD_SOURCE_BYTES],database[BD_SOURCE_BYTES],games[BD_SOURCE_BYTES],source[BD_SOURCE_BYTES];
static char scenario[64];static int busy,blocked,manual=1,manual_unknown,uninstall_unknown,uninstall_refuse,alternative;
static struct bd_smp_record row;static struct bd_local_removal *owner;
#ifndef CHECK
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"local removal %s line%d: %s\n",scenario,__LINE__,#x);exit(1);}}while(0)
#endif
static void fullpath(char out[BD_SOURCE_BYTES],const char *a,const char *b){CHECK(!lm_join(out,a,b));}
static void write_fixture(const char *path,const void *body,size_t n,mode_t mode){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,mode);CHECK(fd>=0&&!fchmod(fd,mode)&&write(fd,body,n)==(ssize_t)n&&!close(fd));}
static int guard(void *opaque){CHECK(opaque==&row);guards++;return busy?-1:0;}
static int outside(void *opaque,const char *path){CHECK(opaque==&row&&lm_under(path,data)&&strstr(path,"/local-removals/")&&strstr(path,"/source"));return blocked?-1:0;}
static int collect(void *opaque,struct bd_smp_record *out,size_t capacity,size_t *count){CHECK(opaque==&row&&capacity>=2);out[0]=row;*count=1;
    collect_calls++;
    if(!strcmp(scenario,"pkg-substitution")&&collect_calls==3)write_fixture(source,"different-package",17,0644);
    if(alternative){out[1]=row;strcpy(out[1].source,"/data/homebrew/Games/UnrelatedCopy");*count=2;}return 0;}
static void sql(const char *statement){sqlite3 *db=NULL;CHECK(sqlite3_open(database,&db)==SQLITE_OK);CHECK(sqlite3_exec(db,statement,NULL,NULL,NULL)==SQLITE_OK&&sqlite3_close(db)==SQLITE_OK);}
static void remove_registered(unsigned package){
    char app[BD_SOURCE_BYTES],path[BD_SOURCE_BYTES];fullpath(app,apps,row.title_id);fullpath(path,app,"sce_sys/param.json");CHECK(!unlink(path));fullpath(path,app,"sce_sys");CHECK(!rmdir(path));
    fullpath(path,app,package?"app.pkg":"mount.lnk");CHECK(!unlink(path)&&!rmdir(app));sql("DELETE FROM tbl_contentinfo WHERE titleId='PPSA99981';");
}
static int uninstall(void *opaque,const char title[10]){
    CHECK(opaque==&row&&!strcmp(title,row.title_id)&&owner&&owner->consumed&&owner->mutation);uninstalls++;
    char path[BD_SOURCE_BYTES];fullpath(path,owner->operation.path,"uninstall.called");struct stat marker;CHECK(!stat(path,&marker)&&marker.st_size==32&&(marker.st_mode&07777)==0600);
    fullpath(path,owner->operation.path,"intent.bin");CHECK(!stat(path,&marker)&&marker.st_size==LM_RECORD_BYTES);
    if(row.installed_pkg){CHECK(!owner->result.quarantined);}else{struct stat old;CHECK(lstat(source,&old)<0&&errno==ENOENT&&!stat(owner->quarantine,&old)&&old.st_ino==owner->source.st.st_ino);}
    if(!uninstall_refuse)remove_registered(row.installed_pkg);return uninstall_unknown?-1:uninstall_refuse?1:0;
}
static int manual_present(void *opaque,const char *path){CHECK(opaque==&row&&!strcmp(path,source));return manual;}
static int manual_remove(void *opaque,const char *path){CHECK(opaque==&row&&!strcmp(path,source)&&owner&&owner->manual_consumed);manual_calls++;if(!manual_unknown)manual=0;return manual_unknown?-1:0;}
static void progress(void *opaque,const struct bw_game_uninstall_progress *p){CHECK(opaque==&row&&p&&p->files_done<=p->files_total&&p->message);progress_calls++;}
/* Default component adapters are exercised too. Only wire transport and Sony
 * effects are modeled; the bounded actual document parser is unchanged. */
int bw_sm_observe(const struct bw_sm_connection *a,enum bw_sm_observation b,struct bw_sm_document *c){
    CHECK(!strncmp(scenario,"default-",8)&&a&&a->port==10101&&a->current&&!a->current(a->opaque)&&b==BW_SM_OBSERVE_GAMES);
    char body[4096];int n=snprintf(body,sizeof body,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"%s\",\"title_name\":\"Local fixture game\",\"content_id\":\"%s\",\"installed\":true,\"installed_pkg\":false,\"managed\":true,\"source_available\":true,\"image_backed\":false}]}",source,source,row.title_id,row.content_id);
    CHECK(n>0&&(size_t)n<sizeof body);c->bytes=(unsigned char *)strdup(body);CHECK(c->bytes);c->size=(size_t)n;return 0;}
void bw_sm_document_clear(struct bw_sm_document *d){free(d->bytes);memset(d,0,sizeof *d);}
int bw_sm_post(const struct bw_sm_connection *a,enum bw_sm_operation b,const char *c,struct bw_sm_reply *d){
    CHECK(!strncmp(scenario,"default-",8)&&a&&a->port==10101&&a->current&&!a->current(a->opaque)&&!strcmp(c,source));memset(d,0,sizeof *d);
    if(b==BW_SM_MANUAL_LIST){int rc=manual_present(&row,c);if(rc<0)return -1;d->present=(unsigned)rc;return 0;}
    CHECK(b==BW_SM_MANUAL_REMOVE);return manual_remove(&row,c);}
int bw_sm_uninstall(const struct bw_sm_connection *a,const char b[10],struct bw_sm_uninstall_reply *c){
    CHECK(!strncmp(scenario,"default-",8)&&a&&a->port==10101&&a->current&&!a->current(a->opaque));memset(c,0,sizeof *c);
    int rc=uninstall(&row,b);c->requested=rc==0;return rc?-1:0;}
int main(int argc,char **argv){
    CHECK(argc==3&&strlen(argv[1])<sizeof area&&strlen(argv[2])<sizeof scenario);strcpy(area,argv[1]);strcpy(scenario,argv[2]);fullpath(data,area,"data");fullpath(apps,area,"apps");fullpath(database,area,"app.db");fullpath(games,area,"Games");
    unsigned package=!strncmp(scenario,"pkg",3);strcpy(row.title,"Local fixture game");strcpy(row.title_id,"PPSA99981");strcpy(row.content_id,"UP0000-PPSA99981_00-LOCALFIXTURE0000");
    if(package){char app[BD_SOURCE_BYTES];fullpath(app,apps,row.title_id);fullpath(source,app,"app.pkg");}else fullpath(source,games,"Selected game");
    strcpy(row.source,source);strcpy(row.runtime_source,source);strcpy(row.source_type,package?"pkg":"folder");row.installed=row.source_available=1;row.installed_pkg=package;row.managed=!package;
    struct bd_local_environment environment={apps,database,1,NULL};struct bd_local_observation observed;CHECK(!bd_local_observe(&row,&environment,&observed)&&observed.metadata_verified&&observed.item.facts==BD_LOCAL_REQUIRED&&!observed.item.exclusions);
    struct bd_local_removal_request request={0};struct bd_catalog catalog;CHECK(!bd_catalog_init(&catalog,&request.selected,1,7)&&!bd_catalog_add_local(&catalog,&observed.item)&&catalog.count==1);
    strcpy(request.operation,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");strcpy(request.confirmation.id,request.selected.id);strcpy(request.confirmation.title_id,row.title_id);
    request.confirmation.kind=request.selected.kind;request.confirmation.catalog_generation=7;request.confirmation.confirmed=1;
    struct bd_local_removal_config config={.data_root=data,.apps_root=apps,.database=database,.folder_roots={games},.folder_root_count=1,
        .package_roots={apps},.package_root_count=1,.ops={&row,guard,outside,collect,uninstall,manual_present,manual_remove,progress}};
    CHECK(bd_local_removal_capable(&config,&request.selected));
    if(!strncmp(scenario,"default-",8)){config.smp_port=10101;config.ops.collect=NULL;config.ops.uninstall=NULL;config.ops.manual_present=NULL;config.ops.manual_remove=NULL;}
    char path[BD_SOURCE_BYTES];
    if(!strcmp(scenario,"confirmation"))request.confirmation.confirmed=0;
    if(!strcmp(scenario,"wrong-title"))strcpy(request.confirmation.title_id,"PPSA99982");
    if(!strcmp(scenario,"wrong-id"))request.confirmation.id[0]^=1;
    if(!strcmp(scenario,"wrong-generation"))request.confirmation.catalog_generation++;
    if(!strcmp(scenario,"wrong-fingerprint"))request.selected.source_fingerprint[0]^=1;
    if(!strcmp(scenario,"wrong-kind"))request.confirmation.kind=BD_INSTALLED_PACKAGE;
    if(!strcmp(scenario,"busy"))busy=1;
    if(!strcmp(scenario,"alternative"))alternative=1;
    if(!strcmp(scenario,"symlink")){fullpath(path,source,"assets/unsafe");CHECK(!symlink("../../../../saved-data/save.dat",path));}
    if(!strcmp(scenario,"hardlink")){char saved[BD_SOURCE_BYTES];fullpath(saved,area,"saved-data/save.dat");fullpath(path,source,"assets/unsafe");CHECK(!link(saved,path));}
    if(!strcmp(scenario,"special")){fullpath(path,source,"assets/unsafe");CHECK(!mkfifo(path,0600));}
    if(!strcmp(scenario,"scan-root"))blocked=1;
    if(!strcmp(scenario,"unknown-removed")||!strcmp(scenario,"unknown-present")||!strcmp(scenario,"pkg-unknown-removed")||!strcmp(scenario,"pkg-unknown-present")||!strcmp(scenario,"default-unknown"))uninstall_unknown=1;
    if(!strcmp(scenario,"unknown-present")||!strcmp(scenario,"pkg-unknown-present")||!strcmp(scenario,"refused")||!strcmp(scenario,"pkg-refused")||!strcmp(scenario,"default-refused"))uninstall_refuse=1;
    if(!strcmp(scenario,"manual-unknown"))manual_unknown=1;
    if(!strcmp(scenario,"registered-change")){char app[BD_SOURCE_BYTES];fullpath(app,apps,row.title_id);fullpath(path,app,"mount.lnk");write_fixture(path,"/data/homebrew/Games/Foreign",28,0644);}
    if(!strcmp(scenario,"db-changed-before"))sql("UPDATE tbl_contentinfo SET contentId='FOREIGN';");
    if(!strcmp(scenario,"foreign-ledger")||!strcmp(scenario,"nonce-collision")||!strcmp(scenario,"existing-ledger")||!strcmp(scenario,"pending-history")||!strcmp(scenario,"completed-history")||!strcmp(scenario,"malformed-history")){
        fullpath(path,data,"local-removals");CHECK(!mkdir(path,0700));char marker[BD_SOURCE_BYTES];fullpath(marker,path,".blackwire-local-removals");
        if(strcmp(scenario,"foreign-ledger"))write_fixture(marker,lm_scope,sizeof lm_scope-1,0600);
        if(!strcmp(scenario,"nonce-collision")){fullpath(marker,path,request.operation);CHECK(!mkdir(marker,0700));}
        if(!strcmp(scenario,"pending-history")||!strcmp(scenario,"completed-history")||!strcmp(scenario,"malformed-history")){
            char prior[BD_SOURCE_BYTES];const char *nonce="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";fullpath(prior,path,nonce);CHECK(!mkdir(prior,0700));
            uint8_t record[LM_RECORD_BYTES]={0};memcpy(record,"BWLRM001",8);memcpy(record+16,nonce,33);cp_sha_bytes(record,LM_RECORD_BYTES-32,record+LM_RECORD_BYTES-32);
            if(!strcmp(scenario,"malformed-history"))record[0]^=1;
            fullpath(marker,prior,"intent.bin");write_fixture(marker,record,sizeof record,0600);
            if(strcmp(scenario,"pending-history")){fullpath(marker,prior,"done");write_fixture(marker,record+LM_RECORD_BYTES-32,32,0600);}
        }
    }
    struct bd_local_removal_result result={0};int begin=bd_local_removal_begin(&config,&request,&owner,&result);
    unsigned refuse=!strcmp(scenario,"confirmation")||!strcmp(scenario,"wrong-title")||!strcmp(scenario,"wrong-id")||!strcmp(scenario,"wrong-generation")||
        !strcmp(scenario,"wrong-fingerprint")||!strcmp(scenario,"wrong-kind")||!strcmp(scenario,"busy")||!strcmp(scenario,"alternative")||
        !strcmp(scenario,"symlink")||!strcmp(scenario,"hardlink")||!strcmp(scenario,"special")||!strcmp(scenario,"registered-change")||
        !strcmp(scenario,"db-changed-before")||!strcmp(scenario,"foreign-ledger")||!strcmp(scenario,"nonce-collision")||!strcmp(scenario,"pending-history")||!strcmp(scenario,"malformed-history");
    if(refuse){CHECK(begin!=0&&!owner&&!uninstalls);goto finish;}
    if(!strcmp(scenario,"pkg-substitution")){CHECK(begin<0&&owner&&!uninstalls&&!result.library_removed);goto retained;}
    if(!strcmp(scenario,"scan-root")){CHECK(begin<0&&owner&&!uninstalls&&!owner->result.quarantined&&access(source,F_OK)==0);goto retained;}
    CHECK(begin==0&&owner&&uninstalls==1&&result.active&&!result.library_removed);CHECK(bd_local_removal_destroy(owner)==1);
    if(!strcmp(scenario,"unknown-present")||!strcmp(scenario,"pkg-unknown-present")){
        struct bd_local_removal *second=NULL;struct bd_local_removal_result other_result;struct bd_local_removal_request other=request;strcpy(other.operation,"cccccccccccccccccccccccccccccccc");
        CHECK(bd_local_removal_begin(&config,&other,&second,&other_result)!=0&&!second&&uninstalls==1);
    }
    if(!strcmp(scenario,"inventory-change")){fullpath(path,owner->quarantine,"assets/level.bin");write_fixture(path,"changed",7,0644);}
    if(!strcmp(scenario,"extra-entry")){fullpath(path,owner->quarantine,"assets/unexpected.bin");write_fixture(path,"preserve",8,0644);}
    if(!strcmp(scenario,"intent-change")){fullpath(path,owner->operation.path,"intent.bin");write_fixture(path,"changed",7,0600);}
    if(!strcmp(scenario,"inventory-replaced")){
        char other[BD_SOURCE_BYTES];fullpath(path,owner->operation.path,"inventory.bin");fullpath(other,owner->operation.path,"retained-inventory");CHECK(!rename(path,other));write_fixture(path,"changed",7,0600);
    }
    if(!strcmp(scenario,"new-original")){CHECK(!mkdir(source,0700));fullpath(path,source,"unrelated.txt");write_fixture(path,"preserve",8,0644);}
    if(!strcmp(scenario,"db-conflict"))sql("INSERT INTO tbl_contentinfo(titleId,contentId) VALUES('PPSA99981','FOREIGN');");
    if(!strcmp(scenario,"guard-after"))busy=1;
    if(!strcmp(scenario,"unknown-present")||!strcmp(scenario,"pkg-unknown-present")||!strcmp(scenario,"refused")||!strcmp(scenario,"pkg-refused")||!strcmp(scenario,"default-refused")||!strcmp(scenario,"db-conflict")||!strcmp(scenario,"guard-after")||!strcmp(scenario,"new-original")||!strcmp(scenario,"intent-change")||!strcmp(scenario,"inventory-replaced")){
        CHECK(bd_local_removal_poll(owner,&result)<=0&&!result.library_removed&&result.active&&!result.files_removed);
        CHECK(bd_local_removal_poll(owner,&result)<=0&&uninstalls==1&&!result.files_removed);goto retained;
    }
    if(!strcmp(scenario,"manual-unknown")){
        CHECK(bd_local_removal_poll(owner,&result)==0&&manual_calls==1&&!result.files_removed&&!result.library_removed);
        CHECK(bd_local_removal_poll(owner,&result)==0&&manual_calls==1);manual=0;
    }
    int finished=bd_local_removal_poll(owner,&result);
    if(!strcmp(scenario,"inventory-change")||!strcmp(scenario,"extra-entry")){CHECK(finished<0&&!result.files_removed&&!result.library_removed&&uninstalls==1);goto retained;}
    CHECK(finished==1&&result.phase==BW_GAME_REMOVE_DONE&&result.registration_confirmed&&result.source_absent&&result.library_removed&&!result.active&&uninstalls==1);
    CHECK(result.files_removed==result.files_total&&(package?!manual_calls:manual_calls==1));
    CHECK(bd_local_removal_poll(owner,&result)==1&&uninstalls==1);CHECK(!bd_local_removal_destroy(owner));owner=NULL;
    /* A durable consumed operation never adopts the old source again. */
    CHECK(bd_local_removal_begin(&config,&request,&owner,&result)!=0&&!owner&&uninstalls==1);goto finish;
retained:
    CHECK(owner&&bd_local_removal_destroy(owner)==1);CHECK(!lm_close(owner));owner=NULL; /* fixture-only teardown of isolated retained descriptors */
finish:
    fullpath(path,area,"saved-data/save.dat");char saved[16];int fd=open(path,O_RDONLY);CHECK(fd>=0&&read(fd,saved,sizeof saved)==13&&!memcmp(saved,"original-save",13)&&!close(fd));
    fullpath(path,games,"Unrelated game/keep.bin");fd=open(path,O_RDONLY);CHECK(fd>=0&&read(fd,saved,sizeof saved)==14&&!memcmp(saved,"unrelated-game",14)&&!close(fd));
    printf("{\"passed\":true,\"scenario\":\"%s\",\"checks\":%u,\"uninstallCalls\":%u,\"manualRemoveCalls\":%u,\"progressEvents\":%u,\"savedDataPreserved\":true,\"unrelatedSourcePreserved\":true}\n",scenario,checks,uninstalls,manual_calls,progress_calls);return 0;
}
