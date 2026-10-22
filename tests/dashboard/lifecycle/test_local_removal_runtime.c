/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CHECK(x) do{__atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);if(!(x)){fprintf(stderr,"local runtime %s line%d: %s\n",scenario,__LINE__,#x);exit(1);}}while(0)
#define main component_fixture_main
#include "test_local_removal.c"
#undef main
static int runtime_destroy_fixture(struct bd_local_removal *p){
    if(!strcmp(scenario,"runtime-close-retained"))return 1;
    int rc=bd_local_removal_destroy(p);return !strcmp(scenario,"runtime-close-error")&&!rc?-1:rc;
}
#define bd_local_removal_destroy runtime_destroy_fixture
#include "local_removal_runtime.c"
#undef bd_local_removal_destroy
#include <stdatomic.h>

static struct bd_local_removal_runtime *runtime;
static atomic_int lease_valid=1,allow_work,wire_done;
static atomic_uint lease_calls,idle_calls,settings_calls;
static pthread_t caller;
static int runtime_lease(void *opaque,const char nonce[33]){CHECK(opaque==&row&&!strcmp(nonce,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));atomic_fetch_add(&lease_calls,1);return atomic_load(&lease_valid)?0:-1;}
static int runtime_idle(void *opaque){CHECK(opaque==&row&&!pthread_equal(pthread_self(),caller));atomic_fetch_add(&idle_calls,1);while(!atomic_load(&allow_work)){struct timespec t={0,1000000};nanosleep(&t,NULL);}return 0;}
static int runtime_settings(void *opaque,struct bw_sm_document *out){CHECK(opaque==&row&&!pthread_equal(pthread_self(),caller));atomic_fetch_add(&settings_calls,1);
    char body[BD_SOURCE_BYTES+128];const char *root=!strcmp(scenario,"runtime-scan-refused")?"/":games;int n=snprintf(body,sizeof body,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",root);CHECK(n>0&&(size_t)n<sizeof body);out->bytes=(unsigned char *)strdup(body);CHECK(out->bytes);out->size=(size_t)n;return 0;}
static int runtime_uninstall(void *opaque,const char title[10]){CHECK(!pthread_equal(pthread_self(),caller));owner=runtime->owner;int rc=uninstall(opaque,title);atomic_store(&wire_done,1);return rc;}
int bw_native_backend_external_current(struct bw_native_backend *p,const char n[33]){(void)p;(void)n;abort();}
int bw_native_setup_idle(void *o){(void)o;abort();}
int bw_startup_idle_ps5(void *o){(void)o;abort();}
static int poll_until(enum bw_game_uninstall_phase phase,struct bw_game_uninstall_result *result){
    for(unsigned i=0;i<5000;i++){int rc=bd_local_removal_runtime_state(runtime,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",result);if(result->phase==phase)return rc;struct timespec t={0,1000000};nanosleep(&t,NULL);}CHECK(0);return -1;
}
int main(int argc,char **argv){
    CHECK(argc==3);caller=pthread_self();strcpy(area,argv[1]);strcpy(scenario,argv[2]);fullpath(data,area,"data");fullpath(apps,area,"apps");fullpath(database,area,"app.db");fullpath(games,area,"Games");
    unsigned pkg=!strcmp(scenario,"runtime-pkg-unknown");strcpy(row.title,"Local fixture game");strcpy(row.title_id,"PPSA99981");strcpy(row.content_id,"UP0000-PPSA99981_00-LOCALFIXTURE0000");
    if(pkg){char app[BD_SOURCE_BYTES];fullpath(app,apps,row.title_id);fullpath(source,app,"app.pkg");}else fullpath(source,games,"Selected game");
    strcpy(row.source,source);strcpy(row.runtime_source,source);strcpy(row.source_type,pkg?"pkg":"folder");row.installed=row.source_available=1;row.installed_pkg=pkg;row.managed=!pkg;
    struct bd_local_environment env={apps,database,1,NULL};struct bd_local_observation observed;CHECK(!bd_local_observe(&row,&env,&observed)&&observed.metadata_verified);
    struct bd_catalog catalog;struct bd_item item;CHECK(!bd_catalog_init(&catalog,&item,1,7)&&!bd_catalog_add_local(&catalog,&observed.item)&&catalog.count==1);
    struct bd_local_removal_runtime_config cfg={.removal={.data_root=data,.apps_root=apps,.database=database,.folder_roots={games},.folder_root_count=1,.package_roots={apps},.package_root_count=1,
        .ops={.opaque=&row,.collect=collect,.uninstall=runtime_uninstall,.manual_present=manual_present,.manual_remove=manual_remove}},.opaque=&row,.reservation_current=runtime_lease,.idle=runtime_idle,.settings=runtime_settings};
    runtime=bd_local_removal_runtime_create(&cfg);CHECK(runtime&&bd_local_removal_runtime_capable(runtime,&item));
    struct bw_game_uninstall_result result={0};CHECK(!bd_local_removal_runtime_start(runtime,&item,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","PPSA99982"));
    CHECK(bd_local_removal_runtime_state(runtime,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",&result)<0);
    if(!strcmp(scenario,"runtime-busy"))atomic_store(&lease_valid,0);
    unsigned unknown=!strcmp(scenario,"runtime-unknown")||pkg;if(unknown)uninstall_unknown=uninstall_refuse=1;
    CHECK(bd_local_removal_runtime_start(runtime,&item,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",row.title_id)==1);
    CHECK(bd_local_removal_runtime_start(runtime,&item,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",row.title_id)==1);
    CHECK(!bd_local_removal_runtime_start(runtime,&item,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",row.title_id));
    CHECK(bd_local_removal_runtime_state(runtime,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",&result)<0);
    CHECK(bd_local_removal_runtime_destroy(runtime)==1);item.source_fingerprint[0]^=1;CHECK(!bd_local_removal_runtime_start(runtime,&item,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",row.title_id));item.source_fingerprint[0]^=1;
    atomic_store(&allow_work,1);
    if(!strcmp(scenario,"runtime-close-retained")||!strcmp(scenario,"runtime-close-error")){
        CHECK(poll_until(BW_GAME_REMOVE_UNKNOWN,&result)<0&&result.source_absent&&result.registration_confirmed&&uninstalls==1);
        CHECK(bd_local_removal_runtime_destroy(runtime)==1&&!bd_local_removal_runtime_start(runtime,&item,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",row.title_id));
        pthread_mutex_lock(&runtime->lock);CHECK(!runtime->terminal&&runtime->active);
        CHECK(!strcmp(scenario,"runtime-close-retained")?runtime->owner!=NULL:runtime->owner==NULL);
        /* Isolated fixture-only descriptor teardown of the retained owner.
         * Production has no API that forgets this uncertain operation. */
        if(runtime->owner){CHECK(!bd_local_removal_destroy(runtime->owner));runtime->owner=NULL;}
        runtime->active=0;runtime->terminal=1;pthread_mutex_unlock(&runtime->lock);goto teardown;
    }
    if(!strcmp(scenario,"runtime-busy")||!strcmp(scenario,"runtime-scan-refused")){
        CHECK(poll_until(BW_GAME_REMOVE_ERROR,&result)==1&&!result.mutation_started&&!uninstalls);
        char ledger[BD_SOURCE_BYTES];fullpath(ledger,data,"local-removals");struct stat absent;CHECK(lstat(ledger,&absent)<0&&errno==ENOENT);
    }
    else {
        if(unknown){CHECK(poll_until(BW_GAME_REMOVE_UNKNOWN,&result)<0&&result.mutation_started&&!result.library_removed);CHECK(bd_local_removal_runtime_destroy(runtime)==1);
            while(!atomic_load(&wire_done)){struct timespec t={0,1000000};nanosleep(&t,NULL);}CHECK(uninstalls==1);remove_registered(pkg);
        }
        CHECK(poll_until(BW_GAME_REMOVE_DONE,&result)==1&&result.library_removed&&result.source_absent&&result.registration_confirmed&&uninstalls==1);
        CHECK(pkg?result.files_total==0:result.files_total>0&&result.files_total==result.files_removed);
        CHECK(bd_local_removal_runtime_start(runtime,&item,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",row.title_id)==1&&uninstalls==1);
        CHECK(atomic_load(&idle_calls)&&atomic_load(&lease_calls)&&(pkg?!atomic_load(&settings_calls):atomic_load(&settings_calls)>0));
    }
teardown:CHECK(!bd_local_removal_runtime_destroy(runtime));runtime=NULL;owner=NULL;
    char destination[BD_SOURCE_BYTES];fullpath(destination,data,"local-removals/new/source");
    const char *valid="{\"status\":0,\"scan_paths\":[],\"scan_path_count\":0}";CHECK(!bd_local_removal_scan_excluded(valid,strlen(valid),destination));
    const char *bad[]={"{}","{\"status\":1,\"scan_paths\":[],\"scan_path_count\":0}","{\"status\":0,\"scan_paths\":[\"/\"],\"scan_path_count\":1}","{\"status\":0,\"scan_paths\":[],\"scan_path_count\":1}","{\"status\":0,\"scan_paths\":[],\"scan_path_count\":0,\"scan_paths\":[]}","{\"status\":0,\"scan_paths\":[\"/data/../data\"],\"scan_path_count\":1}","{\"status\":0,\"scan_paths\":[\"relative\"],\"scan_path_count\":1}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)CHECK(bd_local_removal_scan_excluded(bad[i],strlen(bad[i]),destination)<0);
    char alias[BD_SOURCE_BYTES],json[BD_SOURCE_BYTES+160];fullpath(alias,area,"scan-alias");CHECK(!symlink(data,alias));int n=snprintf(json,sizeof json,"{\"status\":0,\"scan_paths\":[\"%s\"],\"scan_path_count\":1}",alias);CHECK(n>0&&bd_local_removal_scan_excluded(json,(size_t)n,destination)<0);
    char path[BD_SOURCE_BYTES],keep[16];fullpath(path,area,"saved-data/save.dat");int fd=open(path,O_RDONLY);CHECK(fd>=0&&read(fd,keep,sizeof keep)==13&&!memcmp(keep,"original-save",13)&&!close(fd));
    printf("{\"passed\":true,\"scenario\":\"%s\",\"checks\":%u,\"uninstallCalls\":%u,\"backgroundWorkerOnly\":true,\"savedDataPreserved\":true}\n",scenario,checks,uninstalls);return 0;
}
