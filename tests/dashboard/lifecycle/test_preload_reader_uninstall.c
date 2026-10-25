/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual Send, complete tree deletion, real readonly SQLite and fault I/O. */
#define main retained_fast_main
#include "test_fast_ready_play.c"
#undef main
#include "native_game_uninstall.h"
#include "preload_bundle.h"
extern const uint8_t fixture_previous[];extern const size_t fixture_previous_size;
extern const uint8_t fixture_public07[];extern const size_t fixture_public07_size;
#include <sqlite3.h>

static unsigned module_opens,module_closes,unlinks,writes,registrations,phase_seen;
static unsigned partial_writes,close_failed,sync_failed,late_registration_created;
static const char *fault_suffix;
static int unlink_failure,registration_pending;
static unsigned continuation_active,continuation_interrupted,continuation_exclusions;
static char test_data[2048],test_apps[2048],test_db[2048],test_root[2048],test_state[2048];
static struct bw_reader_selection registration_selection;
static const char *test_scenario;
static int owned_root_mode_case(void){return !strncmp(test_scenario,"owned-",6)&&strstr(test_scenario,"-0770")!=NULL;}
static unsigned char before_source[32],before_save[32];
static int fd_suffix(int,const char *);
static int fd_removal_phase(int,const char *);
static mode_t gu_mode(int flags,va_list a){return flags&O_CREAT?(mode_t)va_arg(a,int):0;}
int gu_test_open(const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=gu_mode(flags,a);va_end(a);int fd=open(name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_openat(int parent,const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=gu_mode(flags,a);va_end(a);int fd=openat(parent,name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_close(int fd){int marker=!close_failed&&!strcmp(test_scenario,"continue-file-marker-close-uncertain")&&fd_removal_phase(fd,".files");int result=close(fd);if(!result)module_closes++;if(marker||(!close_failed&&(!strcmp(test_scenario,"close-uncertain")||!strcmp(test_scenario,"continue-close-uncertain"))&&unlinks)){close_failed=1;errno=EIO;return -1;}return result;}
int gu_test_dup(int fd){int result=dup(fd);if(result>=0)module_opens++;return result;}
int gu_test_closedir(DIR *dir){int result=closedir(dir);if(!result)module_closes++;return result;}
static int fd_suffix(int fd,const char *suffix){char fdname[64],name[4096];snprintf(fdname,sizeof fdname,"/proc/self/fd/%d",fd);ssize_t n=readlink(fdname,name,sizeof name-1);if(n<0)return 0;name[n]=0;size_t a=(size_t)n,b=strlen(suffix);return a>=b&&!strcmp(name+a-b,suffix);}
static int fd_removal_phase(int fd,const char *suffix){char fdname[64],name[4096];if(!fd_suffix(fd,suffix))return 0;snprintf(fdname,sizeof fdname,"/proc/self/fd/%d",fd);ssize_t n=readlink(fdname,name,sizeof name-1);if(n<0)return 0;name[n]=0;return strstr(name,"/game-removals/")!=NULL;}
int gu_test_fsync(int fd){if(!sync_failed&&!strcmp(test_scenario,"continue-file-marker-sync-uncertain")&&fd_removal_phase(fd,".files")){sync_failed=1;errno=EIO;return -1;}return fsync(fd);}
ssize_t gu_test_write(int fd,const void *body,size_t size){writes++;if(fault_suffix&&fd_suffix(fd,fault_suffix)){if(partial_writes++){errno=EIO;return -1;}return write(fd,body,size>7?7:size);}return write(fd,body,size);}
int gu_test_unlinkat(int parent,const char *name,int flags){unlinks++;if(unlink_failure){int perform=unlink_failure==2;unlink_failure=0;if(perform)CHECK(!unlinkat(parent,name,flags));errno=EIO;return -1;}return unlinkat(parent,name,flags);}
static void sql_execute(const char *sql){sqlite3 *db=NULL;CHECK(sqlite3_open(test_db,&db)==SQLITE_OK);char *error=NULL;CHECK(sqlite3_exec(db,sql,NULL,NULL,&error)==SQLITE_OK);sqlite3_free(error);CHECK(sqlite3_close(db)==SQLITE_OK);}
static void hash_name(const char *name,unsigned char digest[32]){size_t size;void *body=file_read(name,&size);cp_sha_bytes(body,size,digest);free(body);}
static void preserve_before(const char *base){char name[2048];path(name,base,"source/eboot.bin");hash_name(name,before_source);path(name,base,"save/saved-game.bin");hash_name(name,before_save);}
static void preserve_after(const char *base){char name[2048];unsigned char actual[32];path(name,base,"source/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));path(name,base,"save/saved-game.bin");hash_name(name,actual);CHECK(!memcmp(before_save,actual,32));path(name,base,"manual-full/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));}
static int removal_current(void *v){CHECK(v);return !strcmp(test_scenario,"active")||!strcmp(test_scenario,"unknown-idle")||
    (continuation_active&&(continuation_interrupted||!strcmp(test_scenario,"continue-active")||!strcmp(test_scenario,"continue-unknown-idle")||(!strcmp(test_scenario,"continue-late-active")&&unlinks)))?-1:0;}
static int excluded(void *v,const struct bw_removed_source *s){CHECK(v&&s&&!strcmp(s->identity.binding.title,"PPSA99999"));
    if(!strncmp(test_scenario,"continue-",9)){
        if(!continuation_active)return registrations?-1:0;continuation_exclusions++;
        if(!strcmp(test_scenario,"continue-source-excluded")||(!strcmp(test_scenario,"continue-late-exclusion")&&unlinks))return -1;
        if(!late_registration_created&&unlinks&&(!strcmp(test_scenario,"continue-late-app-reappeared")||!strcmp(test_scenario,"continue-late-db-reappeared"))){
            late_registration_created=1;if(!strcmp(test_scenario,"continue-late-app-reappeared")){char name[2048];path(name,test_apps,"PPSA99999");CHECK(!mkdir(name,0755));}
            else sql_execute("INSERT INTO tbl_contentinfo VALUES('PPSA99999','foreign-content');");
        }
        return 0;
    }
    return !strcmp(test_scenario,"scan-root")||!strcmp(test_scenario,"manual-source")||(!strcmp(test_scenario,"manual-owned")&&!registrations)?-1:0;
}
static int source_safe(void *v,const struct bw_removed_source *s){if(!strcmp(test_scenario,"manual-owned")){CHECK(v&&s&&!registrations);return 0;}return -1;}
static int remove_registration(void *v,const struct bw_removed_source *s){
    CHECK(v&&s&&++registrations==1);char name[2048];
    if(registration_pending)return -1;
    enum bw_registration_state state=bw_native_registration_state(test_data,test_apps,test_db,&registration_selection);
    if(state==BW_REG_ABSENT)return 0;
    if(state!=BW_REG_MATCH)return -1;
    path(name,test_apps,"PPSA99999/app.pkg");struct stat st;if(!lstat(name,&st)||errno!=ENOENT)return -1;
    /* Model only the ordinary manager's accepted effect, after actual exact
     * registered-source/param/read-only SQLite validation. No live transport. */
    path(name,test_apps,"PPSA99999/sce_sys/param.json");CHECK(!unlink(name));path(name,test_apps,"PPSA99999/sce_sys");CHECK(!rmdir(name));
    path(name,test_apps,"PPSA99999/mount.lnk");CHECK(!unlink(name));path(name,test_apps,"PPSA99999");CHECK(!rmdir(name));
    if(strcmp(test_scenario,"app-only-effect"))sql_execute("DELETE FROM tbl_contentinfo WHERE titleId='PPSA99999';");return !strcmp(test_scenario,"lost-registration-reply")?-1:0;
}
static void replace_same(const char *name){struct stat st;CHECK(!stat(name,&st));size_t n;void *body=file_read(name,&n);char saved[2100];snprintf(saved,sizeof saved,"%s.replaced",name);CHECK(!rename(name,saved));file_write(name,body,n,st.st_mode&07777);free(body);}
static void mutation_after_inventory(const struct bw_game_uninstall_progress *v){char name[2048],other[2048];
    if(phase_seen++)return;path(name,test_root,"startup.txt");
    if(!strcmp(test_scenario,"late-same-size-restored-mtime")){struct stat st;CHECK(!stat(name,&st));mutate_byte(name);struct timespec ts[2]={st.st_atim,st.st_mtim};CHECK(!utimensat(AT_FDCWD,name,ts,0));}
    if(!strcmp(test_scenario,"late-inode-replacement"))replace_same(name);
    if(!strcmp(test_scenario,"late-mode"))CHECK(!chmod(name,0755));
    if(!strcmp(test_scenario,"late-hardlink")){path(other,test_root,"other-link");CHECK(!link(name,other));}
    if(!strcmp(test_scenario,"late-new-file")){path(name,test_root,"new-foreign-file");file_write(name,"foreign",7,0644);}
    if(!strcmp(test_scenario,"late-root-replacement")){snprintf(other,sizeof other,"%s.original",test_root);CHECK(!rename(test_root,other));CHECK(!mkdir(test_root,0755));}
    if(!strcmp(test_scenario,"late-shared-apps-mode"))CHECK(!chmod(test_apps,0755));
    if(!strcmp(test_scenario,"late-shared-apps-replacement")){snprintf(other,sizeof other,"%s.original",test_apps);CHECK(!rename(test_apps,other));CHECK(!mkdir(test_apps,0770));}
    (void)v;
}
static void observe(void *v,const struct bw_game_uninstall_progress *p){CHECK(v&&p&&p->files_done<=p->files_total);if(p->phase==BW_GAME_REMOVE_REGISTRATION)mutation_after_inventory(p);
    if(continuation_active&&p->phase==BW_GAME_REMOVE_FILES&&!strcmp(test_scenario,"continue-cancel-after-arm"))continuation_interrupted=1;
}
static int restore_absent(void *v,const struct bw_removed_source *s){CHECK(v&&s);struct stat st;return lstat(test_root,&st)&&errno==ENOENT&&lstat(test_state,&st)&&errno==ENOENT&&bw_registration_db_match(test_db,s->identity.binding.title,"UP0000-PPSA99999_00-BLACKWIREFIXTURE0")==0?0:-1;}
static void continuation_case(struct bw_game_uninstall_request *original,const char *base){
    struct bw_game_uninstall_result result={0};struct stat st;char intent[2048],registration[2048],files[2048],done[2048],leaf[160],pathbuf[2048],other[2100];
    CHECK(bw_native_game_uninstall(original,&result)<0&&result.phase==BW_GAME_REMOVE_UNKNOWN&&result.mutation_started&&registrations==1&&!unlinks);cases++;
    snprintf(leaf,sizeof leaf,"game-removals/%s.%s.intent",original->identity.binding.game,original->operation);path(intent,test_data,leaf);
    snprintf(leaf,sizeof leaf,"game-removals/%s.%s.registration",original->identity.binding.game,original->operation);path(registration,test_data,leaf);
    snprintf(leaf,sizeof leaf,"game-removals/%s.%s.files",original->identity.binding.game,original->operation);path(files,test_data,leaf);
    snprintf(leaf,sizeof leaf,"game-removals/%s.%s.done",original->identity.binding.game,original->operation);path(done,test_data,leaf);
    CHECK(!lstat(intent,&st)&&st.st_size==4096&&!lstat(registration,&st)&&st.st_size==128);
    CHECK(lstat(files,&st)<0&&errno==ENOENT&&lstat(done,&st)<0&&errno==ENOENT&&!lstat(test_root,&st)&&!lstat(test_state,&st));
    struct bw_game_uninstall_files_request q={.environment=original->environment,.identity=original->identity,.prepared=original->prepared,
        .ops={.opaque=original->ops.opaque,.current=removal_current,.source_excluded=excluded,.observe=observe},.reader_size=original->reader_size};
    memcpy(q.operation,original->operation,33);memcpy(q.confirmed_title,original->confirmed_title,10);memcpy(q.reader_sha256,original->reader_sha256,32);
    continuation_active=1;
    if(!strcmp(test_scenario,"continue-new-operation"))memset(q.operation,'c',32);
    if(!strcmp(test_scenario,"continue-wrong-prepared"))q.prepared.record_sha256[0]^=1;
    if(!strcmp(test_scenario,"continue-wrong-confirmation"))strcpy(q.confirmed_title,"PPSA00001");
    if(!strcmp(test_scenario,"continue-no-current"))q.ops.current=NULL;
    if(!strcmp(test_scenario,"continue-no-exclusion"))q.ops.source_excluded=NULL;
    if(!strcmp(test_scenario,"continue-no-intent"))CHECK(!unlink(intent));
    if(!strcmp(test_scenario,"continue-no-registration"))CHECK(!unlink(registration));
    if(!strcmp(test_scenario,"continue-partial-intent")){int fd=open(intent,O_WRONLY);CHECK(fd>=0&&!ftruncate(fd,7)&&!close(fd));}
    if(!strcmp(test_scenario,"continue-partial-registration")){int fd=open(registration,O_WRONLY);CHECK(fd>=0&&!ftruncate(fd,7)&&!close(fd));}
    if(!strcmp(test_scenario,"continue-invalid-done"))file_write(done,"untrusted",9,0600);
    if(!strcmp(test_scenario,"continue-inventory-drift")){path(pathbuf,test_root,"startup.txt");struct stat before;CHECK(!stat(pathbuf,&before));mutate_byte(pathbuf);struct timespec ts[2]={before.st_atim,before.st_mtim};CHECK(!utimensat(AT_FDCWD,pathbuf,ts,0));}
    if(!strcmp(test_scenario,"continue-fine-metadata-drift")){path(pathbuf,test_root,"startup.txt");CHECK(!chmod(pathbuf,0644));}
    if(!strcmp(test_scenario,"continue-root-replacement")){snprintf(other,sizeof other,"%s.original",test_root);CHECK(!rename(test_root,other)&&!mkdir(test_root,0755));}
    if(!strcmp(test_scenario,"continue-state-replacement")){snprintf(other,sizeof other,"%s.original",test_state);CHECK(!rename(test_state,other)&&!mkdir(test_state,0755));}
    if(!strcmp(test_scenario,"continue-receipt-change")){path(pathbuf,test_state,"prepared.bin");mutate_byte(pathbuf);}
    if(!strcmp(test_scenario,"continue-param-change")){path(pathbuf,test_root,"sce_sys/param.json");mutate_byte(pathbuf);}
    if(!strcmp(test_scenario,"continue-stream-allocated")){path(pathbuf,test_root,"assets/stream-00000.pak");int fd=open(pathbuf,O_WRONLY);CHECK(fd>=0&&write(fd,"x",1)==1&&!close(fd));}
    if(!strcmp(test_scenario,"continue-extra-file")){path(pathbuf,test_root,"unexpected");file_write(pathbuf,"foreign",7,0644);}
    if(!strcmp(test_scenario,"continue-app-reappeared")){path(pathbuf,test_apps,"PPSA99999");CHECK(!mkdir(pathbuf,0755));}
    if(!strcmp(test_scenario,"continue-db-reappeared"))sql_execute("INSERT INTO tbl_contentinfo VALUES('PPSA99999','foreign-content');");
    if(!strcmp(test_scenario,"continue-partial-files"))fault_suffix=".files";
    if(!strcmp(test_scenario,"continue-unlink-refused"))unlink_failure=1;
    if(!strcmp(test_scenario,"continue-unlink-uncertain"))unlink_failure=2;
    unsigned requests=registrations;int rc=bw_native_game_uninstall_continue_files(&q,&result);cases++;
    CHECK(registrations==requests&&requests==1);
    if(!strcmp(test_scenario,"continue-success")){
        CHECK(!rc&&result.phase==BW_GAME_REMOVE_DONE&&result.library_removed&&result.registration_confirmed&&result.source_absent&&result.prepared_record_absent&&result.files_removed==result.files_total&&result.files_total>0);
        CHECK(continuation_exclusions>result.files_total&&lstat(test_root,&st)<0&&errno==ENOENT&&lstat(test_state,&st)<0&&errno==ENOENT);
        struct bw_removed_source removed;struct bw_removed_result hidden;CHECK(!bw_removed_lookup(test_data,&q.identity,&removed,&hidden)&&hidden.state==BW_REMOVED_COMMITTED);
        unsigned deleted=unlinks;CHECK(!bw_native_game_uninstall_continue_files(&q,&result)&&result.phase==BW_GAME_REMOVE_DONE&&unlinks==deleted&&registrations==requests);cases++;
    }else{
        CHECK(rc!=0&&result.phase==BW_GAME_REMOVE_UNKNOWN&&!result.library_removed);
        unsigned deleted=unlinks;fault_suffix=NULL;unlink_failure=0;continuation_interrupted=0;
        CHECK(bw_native_game_uninstall_continue_files(&q,&result)!=0&&result.phase==BW_GAME_REMOVE_UNKNOWN&&unlinks==deleted&&registrations==requests);cases++;
        if(strcmp(test_scenario,"continue-unlink-refused")&&strcmp(test_scenario,"continue-unlink-uncertain")&&strcmp(test_scenario,"continue-close-uncertain")&&strcmp(test_scenario,"continue-late-exclusion")&&strcmp(test_scenario,"continue-late-active")&&strcmp(test_scenario,"continue-late-app-reappeared")&&strcmp(test_scenario,"continue-late-db-reappeared"))CHECK(!unlinks);
    }
    preserve_after(base);CHECK(module_opens==module_closes);
}
int main(int argc,char **argv){
    CHECK(argc==3);test_scenario=argv[2];const char *shared="shared-apps-0770-";if(!strncmp(test_scenario,shared,strlen(shared)))test_scenario+=strlen(shared);scenario=test_scenario;char fixture[2048],log[2048],name[2048];
    struct bw_native_prepared digest_case={.state=BW_PREPARED_VALID};CHECK(!terminal_ready(&digest_case));digest_case.record_sha256[1]=1;CHECK(terminal_ready(&digest_case));digest_case.state=BW_PREPARED_UNKNOWN;CHECK(!terminal_ready(&digest_case));
    path(test_data,argv[1],"data");path(source_root,argv[1],"source");path(fixture,argv[1],"metadata");path(log,argv[1],"game.log");path(test_apps,argv[1],"apps");path(test_db,argv[1],"app.db");
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");struct bw_provider_binding identity;CHECK(!bw_provider_binding_init(&identity,&config,"Fixture","PPSA99999",BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};struct bw_source_smb source={.smb=(struct smb2_context *)(uintptr_t)1,.deadline=cp_now()+60};provider.identity=identity;provider.source=&source;provider.bundle.ready=1;strcpy(provider.bundle.source,"Fixture");strcpy(provider.bundle.game,identity.binding.game);strcpy(provider.bundle.title,"PPSA99999");
    size_t size;path(name,fixture,"map.bin");provider.bundle.map.bytes=file_read(name,&provider.bundle.map.size);path(name,fixture,"digests.bin");provider.bundle.digests=file_read(name,&provider.bundle.digest_size);path(name,fixture,"selection.txt");void *selection=file_read(name,&size);CHECK(size<sizeof provider.bundle.selection);memcpy(provider.bundle.selection,selection,size);free(selection);
    struct bw_manifest map;CHECK(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&map,i,&row));if(row.flags==BW_MANIFEST_LOCAL){provider.bundle.map.local_count++;provider.bundle.map.local_bytes+=row.size;}else{provider.bundle.map.stream_count++;provider.bundle.map.remote_bytes+=row.size;}}
    memset(provider.bundle.generation_sha256,0x3a,32);struct bw_native_send_files send={.data_root=test_data,.game_log=log,.provider=&provider,.current=current,.opaque=&provider};struct bw_native_prepared prepared={0};
    double send_start=cp_now();int send_result=bw_native_send_files(&send,&prepared);
    if(send_result||!terminal_ready(&prepared))fprintf(stderr,"initial actual Send rc=%d state=%d retirement=%d elapsed=%.6f guardCalls=%u providerChecks=%u error=%s errno=%d\n",send_result,prepared.state,prepared.retirement,cp_now()-send_start,guard_calls,provider_checks,prepared.retirement_error?prepared.retirement_error:"none",errno);
    CHECK(!send_result&&terminal_ready(&prepared));cases++;
    snprintf(test_root,sizeof test_root,"%s/games/%s",test_data,identity.binding.game);snprintf(test_state,sizeof test_state,"%s/native-prepared/%s",test_data,identity.binding.game);preserve_before(argv[1]);
    if(!strcmp(test_scenario,"owned-data-0770"))CHECK(!chmod(test_data,0770));
    if(!strcmp(test_scenario,"owned-games-0770")){path(name,test_data,"games");CHECK(!chmod(name,0770));}
    if(!strcmp(test_scenario,"owned-prepared-0770")){path(name,test_data,"native-prepared");CHECK(!chmod(name,0770));}
    if(!strcmp(test_scenario,"owned-game-0770"))CHECK(!chmod(test_root,0770));
    if(!strcmp(test_scenario,"owned-state-0770"))CHECK(!chmod(test_state,0770));
    if(!strcmp(test_scenario,"shared-apps-symlink")){char other[2100];snprintf(other,sizeof other,"%s.original",test_apps);CHECK(!rename(test_apps,other));CHECK(!symlink(other,test_apps));}
    memset(&registration_selection,0,sizeof registration_selection);strcpy(registration_selection.title,identity.binding.title);strcpy(registration_selection.game,identity.binding.game);strcpy(registration_selection.source,identity.binding.source);registration_selection.source_kind=1;registration_selection.source_policy=1;
    strcpy(registration_selection.run,"dddddddddddddddddddddddddddddddd");registration_selection.manifest_count=map.count;registration_selection.manifest_hash=map.checksum;registration_selection.eboot_size=262144;
    for(unsigned i=0;i<32;i++){snprintf(registration_selection.binding_hex+2*i,3,"%02x",identity.binding.sha256[i]);snprintf(registration_selection.prepared_record_hex+2*i,3,"%02x",prepared.record_sha256[i]);}
    struct bw_nrc_environment env={test_data,test_apps,test_db};struct bw_game_uninstall_capability capability={0};measuring=1;tree_reads=stream_opens=0;
    int probed=bw_native_game_uninstall_probe(&env,&identity,&prepared,&capability);measuring=0;CHECK(!tree_reads&&!stream_opens);
    if(owned_root_mode_case()||!strcmp(test_scenario,"shared-apps-symlink"))CHECK(probed<0&&!capability.can_uninstall&&!capability.can_restore);
    else if(!strcmp(test_scenario,"foreign-registration")||!strcmp(test_scenario,"pkg-registration")||!strcmp(test_scenario,"foreign-row"))CHECK(!capability.can_uninstall);
    else CHECK(!probed&&capability.can_uninstall&&!capability.removed&&!capability.can_restore);cases++;
    struct bw_game_uninstall_request request={.environment={test_data,test_apps,test_db},.identity=identity,.prepared=prepared,.ops={.opaque=&provider,.current=removal_current,.registration_absent=remove_registration,.source_excluded=excluded,.observe=observe}};
    strcpy(request.operation,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");strcpy(request.confirmed_title,identity.binding.title);
    struct bw_preload_bundle_result diagnostic={0};struct bw_preload_bundle_upgrade_request selection_request={.bundle={test_root,current,&provider,cp_now()+20},.data_root=test_data,.game=identity.binding.game,.prepared_run=prepared.run,.launch_run=request.operation,.prepared_record_sha256=prepared.record_sha256};
    unsigned absent=!strcmp(argv[2],"reader-absent"),old=!strcmp(argv[2],"reader-old")||!strcmp(argv[2],"reader-old-wrong-pin");
    unsigned public07=!strncmp(argv[2],"reader-public07",15);
    if(!absent){CHECK(!bw_preload_bundle_install_checked(&selection_request.bundle,&diagnostic));if(old||public07){path(name,test_root,"fakelib2/libSceAmpr.sprx");file_write(name,public07?fixture_public07:fixture_previous,public07?fixture_public07_size:fixture_previous_size,0755);}}
    if(!strcmp(argv[2],"reader-legacy-current")){
        static const char *companions[]={"libSceAppContent.sprx","libSceGameUpdate.sprx","libSceNpEntitlementAccess.sprx","libSceAmpr.sprx.orig"};
        for(unsigned i=0;i<4;i++){char source_name[2048],relative[256];size_t bytes;path(source_name,BW_PRELOAD_GLOBAL_ROOT,companions[i]);void *body=file_read(source_name,&bytes);snprintf(relative,sizeof relative,"fakelib2/%s",companions[i]);path(name,test_root,relative);file_write(name,body,bytes,0755);free(body);}
    }
    struct bw_preload_reader_pin selected_pin={0};CHECK(!bw_preload_bundle_select_for_removal(&selection_request,&selected_pin,&diagnostic)&&!diagnostic.mutation_started);
    request.reader_size=selected_pin.size;memcpy(request.reader_sha256,selected_pin.sha256,32);
    if(old||public07){uint8_t expected[32];cp_sha_bytes(public07?fixture_public07:fixture_previous,public07?fixture_public07_size:fixture_previous_size,expected);CHECK(selected_pin.size==(public07?fixture_public07_size:fixture_previous_size)&&!memcmp(expected,selected_pin.sha256,32));}
    if(!strcmp(argv[2],"reader-old-wrong-pin")||!strcmp(argv[2],"reader-public07-wrong-pin")){request.reader_size=bw_preload_embedded_image.size;memcpy(request.reader_sha256,bw_preload_embedded_image.sha256,32);test_scenario=scenario="reader-wrong-pin";}
    else if(!strcmp(argv[2],"reader-changed-after")||!strcmp(argv[2],"reader-public07-changed-after")){path(name,test_root,"fakelib2/libSceAmpr.sprx");mutate_byte(name);test_scenario=scenario="reader-changed-after-selection";}
    else test_scenario=scenario="normal";

    if(!strncmp(test_scenario,"continue-",9)){
        continuation_case(&request,argv[1]);
        printf("{\"passed\":true,\"scenario\":\"%s\",\"cases\":%u,\"checks\":%u,\"actualSend\":true,\"readonlySqlite\":true,\"moduleOpens\":%u,\"moduleCloses\":%u,\"unlinkAttempts\":%u,\"registrationCallbacks\":%u,\"continuationExclusions\":%u,\"consoleActions\":0}\n",argv[2],cases,checks,module_opens,module_closes,unlinks,registrations,continuation_exclusions);
        free(provider.bundle.map.bytes);free((void *)provider.bundle.digests);return 0;
    }
    if(!strcmp(test_scenario,"manual-owned")||!strcmp(test_scenario,"source-safe-refused"))request.ops.source_safe=source_safe;
    if(!strcmp(test_scenario,"repaired-generation")){char *run=strstr(provider.bundle.selection,"run_id=");CHECK(run);memset(run+7,'c',32);source.deadline=cp_now()+60;CHECK(!bw_native_send_files(&send,&prepared));request.prepared=prepared;cases++;}
    if(!strcmp(test_scenario,"missing-stream")){path(name,test_root,"assets/stream-00000.pak");CHECK(!unlink(name));}
    if(!strcmp(test_scenario,"changed-local")){path(name,test_root,"startup.txt");mutate_byte(name);}
    if(!strcmp(test_scenario,"unknown-file")){path(name,test_root,"unrelated-file");file_write(name,"leave",5,0644);}
    if(!strcmp(test_scenario,"unknown-empty-dir")){path(name,test_root,"unrelated-empty");CHECK(!mkdir(name,0755));}
    if(!strcmp(test_scenario,"linked-local")){path(name,test_root,"startup.txt");CHECK(!unlink(name));char other[2048];path(other,source_root,"startup.txt");CHECK(!symlink(other,name));}
    if(!strcmp(test_scenario,"hardlinked-local")){path(name,test_root,"startup.txt");char other[2048];path(other,argv[1],"hardlink-outside");CHECK(!link(name,other));}
    if(!strcmp(test_scenario,"allocated-stream")){path(name,test_root,"assets/stream-00000.pak");int fd=open(name,O_WRONLY);CHECK(fd>=0&&write(fd,"x",1)==1&&!close(fd));}
    if(!strcmp(test_scenario,"wrong-control")){path(name,test_state,"stream.conf");file_write(name,"version=2\nrun_id=foreign\n",25,0644);}
    if(!strcmp(test_scenario,"unknown-state")){path(name,test_state,"private-unrelated");file_write(name,"leave",5,0600);}
    if(!strcmp(test_scenario,"wrong-confirmation"))strcpy(request.confirmed_title,"PPSA00001");
    if(!strcmp(test_scenario,"wrong-generation"))request.prepared.record_sha256[0]^=1;
    if(!strcmp(test_scenario,"partial-intent"))fault_suffix=".intent";
    if(!strcmp(test_scenario,"partial-registration"))fault_suffix=".registration";
    if(!strcmp(test_scenario,"partial-files"))fault_suffix=".files";
    if(!strcmp(test_scenario,"partial-done"))fault_suffix=".done";
    if(!strcmp(test_scenario,"unlink-refused"))unlink_failure=1;
    if(!strcmp(test_scenario,"unlink-uncertain"))unlink_failure=2;
    if(!strcmp(test_scenario,"registration-pending"))registration_pending=1;
    struct bw_game_uninstall_result result={0};int rc=bw_native_game_uninstall(&request,&result);cases++;
    int success=!strcmp(test_scenario,"normal")||!strcmp(test_scenario,"registered")||!strcmp(test_scenario,"repaired-generation")||!strcmp(test_scenario,"lost-registration-reply")||!strcmp(test_scenario,"manual-owned");
    if(success){if(rc)fprintf(stderr,"removal rc=%d phase=%u reason=%s files=%u registrations=%u\n",rc,result.phase,result.reason?result.reason:"none",result.files_removed,registrations);CHECK(!rc&&result.phase==BW_GAME_REMOVE_DONE&&result.registration_confirmed&&result.source_absent&&result.prepared_record_absent&&result.library_removed&&result.files_removed>0&&result.files_removed==result.files_total);struct stat st;CHECK(lstat(test_root,&st)<0&&errno==ENOENT&&lstat(test_state,&st)<0&&errno==ENOENT);
        struct bw_removed_source removed;struct bw_removed_result hidden;CHECK(!bw_removed_lookup(test_data,&identity,&removed,&hidden)&&hidden.state==BW_REMOVED_COMMITTED);CHECK(!bw_native_game_uninstall_probe(&env,&identity,&prepared,&capability)&&capability.removed&&capability.can_restore&&!capability.can_uninstall);unsigned deletes=unlinks,requests=registrations;CHECK(!bw_native_game_uninstall(&request,&result)&&result.phase==BW_GAME_REMOVE_DONE&&unlinks==deletes&&registrations==requests);cases++;
        struct bw_removed_ops restore={&provider,removal_current,restore_absent};CHECK(!bw_removed_restore(test_data,&identity,"cccccccccccccccccccccccccccccccc",&restore,&hidden)&&hidden.state==BW_REMOVED_RESTORED);CHECK(!bw_removed_lookup(test_data,&identity,&removed,&hidden)&&hidden.state==BW_REMOVED_VISIBLE);cases++;
    }else {CHECK(rc!=0&&result.phase!=BW_GAME_REMOVE_DONE&&!result.library_removed);unsigned deletes=unlinks,requests=registrations;fault_suffix=NULL;unlink_failure=0;registration_pending=0;
        /* A shared parent changing during this admitted frame must stop it.
         * Do not reissue an uncertain operation with a new parent baseline. */
        if(strcmp(test_scenario,"late-shared-apps-mode")&&strcmp(test_scenario,"late-shared-apps-replacement"))CHECK(bw_native_game_uninstall(&request,&result)!=0&&unlinks==deletes&&registrations==requests);
        else CHECK(result.phase==BW_GAME_REMOVE_UNKNOWN&&unlinks==deletes&&registrations==requests);cases++;
        if(strcmp(test_scenario,"partial-done")&&strcmp(test_scenario,"close-uncertain")){struct stat st;CHECK(!lstat(test_root,&st));}
    }
    if(owned_root_mode_case()||!strcmp(test_scenario,"shared-apps-symlink"))CHECK(!registrations&&!unlinks&&!result.mutation_started);
    if(!strcmp(test_scenario,"owned-data-0770")||!strcmp(test_scenario,"owned-games-0770")||!strcmp(test_scenario,"owned-prepared-0770")||!strcmp(test_scenario,"shared-apps-symlink"))CHECK(!writes);
    if(!strcmp(test_scenario,"late-shared-apps-mode")||!strcmp(test_scenario,"late-shared-apps-replacement"))CHECK(!registrations&&!unlinks);
    preserve_after(argv[1]);CHECK(module_opens==module_closes);
    printf("{\"passed\":true,\"scenario\":\"%s\",\"cases\":%u,\"checks\":%u,\"actualSend\":true,\"readonlySqlite\":true,\"moduleOpens\":%u,\"moduleCloses\":%u,\"unlinkAttempts\":%u,\"registrationCallbacks\":%u,\"consoleActions\":0}\n",argv[2],cases,checks,module_opens,module_closes,unlinks,registrations);
    free(provider.bundle.map.bytes);free((void *)provider.bundle.digests);return 0;
}
