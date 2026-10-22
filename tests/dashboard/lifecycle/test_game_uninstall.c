/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual Send, complete tree deletion, real readonly SQLite and fault I/O. */
#define main retained_fast_main
#include "test_fast_ready_play.c"
#undef main
#include "native_game_uninstall.h"
#include <sqlite3.h>

static unsigned module_opens,module_closes,unlinks,writes,registrations,phase_seen;
static unsigned partial_writes,close_failed;
static const char *fault_suffix;
static int unlink_failure,registration_pending;
static char test_data[2048],test_apps[2048],test_db[2048],test_root[2048],test_state[2048];
static struct bw_reader_selection registration_selection;
static const char *test_scenario;
static int owned_root_mode_case(void){return !strncmp(test_scenario,"owned-",6)&&strstr(test_scenario,"-0770")!=NULL;}
static unsigned char before_source[32],before_save[32];
static mode_t gu_mode(int flags,va_list a){return flags&O_CREAT?(mode_t)va_arg(a,int):0;}
int gu_test_open(const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=gu_mode(flags,a);va_end(a);int fd=open(name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_openat(int parent,const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=gu_mode(flags,a);va_end(a);int fd=openat(parent,name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_close(int fd){int result=close(fd);if(!result)module_closes++;if(!close_failed&&!strcmp(test_scenario,"close-uncertain")&&unlinks){close_failed=1;errno=EIO;return -1;}return result;}
int gu_test_dup(int fd){int result=dup(fd);if(result>=0)module_opens++;return result;}
int gu_test_closedir(DIR *dir){int result=closedir(dir);if(!result)module_closes++;return result;}
static int fd_suffix(int fd,const char *suffix){char fdname[64],name[4096];snprintf(fdname,sizeof fdname,"/proc/self/fd/%d",fd);ssize_t n=readlink(fdname,name,sizeof name-1);if(n<0)return 0;name[n]=0;size_t a=(size_t)n,b=strlen(suffix);return a>=b&&!strcmp(name+a-b,suffix);}
ssize_t gu_test_write(int fd,const void *body,size_t size){writes++;if(fault_suffix&&fd_suffix(fd,fault_suffix)){if(partial_writes++){errno=EIO;return -1;}return write(fd,body,size>7?7:size);}return write(fd,body,size);}
int gu_test_unlinkat(int parent,const char *name,int flags){unlinks++;if(unlink_failure){int perform=unlink_failure==2;unlink_failure=0;if(perform)CHECK(!unlinkat(parent,name,flags));errno=EIO;return -1;}return unlinkat(parent,name,flags);}
static void sql_execute(const char *sql){sqlite3 *db=NULL;CHECK(sqlite3_open(test_db,&db)==SQLITE_OK);char *error=NULL;CHECK(sqlite3_exec(db,sql,NULL,NULL,&error)==SQLITE_OK);sqlite3_free(error);CHECK(sqlite3_close(db)==SQLITE_OK);}
static void hash_name(const char *name,unsigned char digest[32]){size_t size;void *body=file_read(name,&size);cp_sha_bytes(body,size,digest);free(body);}
static void preserve_before(const char *base){char name[2048];path(name,base,"source/eboot.bin");hash_name(name,before_source);path(name,base,"save/saved-game.bin");hash_name(name,before_save);}
static void preserve_after(const char *base){char name[2048];unsigned char actual[32];path(name,base,"source/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));path(name,base,"save/saved-game.bin");hash_name(name,actual);CHECK(!memcmp(before_save,actual,32));path(name,base,"manual-full/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));}
static int removal_current(void *v){CHECK(v);return !strcmp(test_scenario,"active")||!strcmp(test_scenario,"unknown-idle")?-1:0;}
static int excluded(void *v,const struct bw_removed_source *s){CHECK(v&&s&&!strcmp(s->identity.binding.title,"PPSA99999"));return !strcmp(test_scenario,"scan-root")||!strcmp(test_scenario,"manual-source")||(!strcmp(test_scenario,"manual-owned")&&!registrations)?-1:0;}
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
static void observe(void *v,const struct bw_game_uninstall_progress *p){CHECK(v&&p&&p->files_done<=p->files_total);if(p->phase==BW_GAME_REMOVE_REGISTRATION)mutation_after_inventory(p);}
static int restore_absent(void *v,const struct bw_removed_source *s){CHECK(v&&s);struct stat st;return lstat(test_root,&st)&&errno==ENOENT&&lstat(test_state,&st)&&errno==ENOENT&&bw_registration_db_match(test_db,s->identity.binding.title,"UP0000-PPSA99999_00-BLACKWIREFIXTURE0")==0?0:-1;}
int main(int argc,char **argv){
    CHECK(argc==3||argc==4);test_scenario=argv[2];const char *shared="shared-apps-0770-";if(!strncmp(test_scenario,shared,strlen(shared)))test_scenario+=strlen(shared);scenario=test_scenario;char fixture[2048],log[2048],name[2048];
    path(test_data,argv[1],"data");path(source_root,argv[1],"source");path(fixture,argv[1],"metadata");path(log,argv[1],"game.log");path(test_apps,argv[1],"apps");path(test_db,argv[1],"app.db");
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");struct bw_provider_binding identity;CHECK(!bw_provider_binding_init(&identity,&config,"Fixture","PPSA99999",argc==4?BW_PROVIDER_CONTAINER:BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};struct bw_source_smb source={.smb=(struct smb2_context *)(uintptr_t)1,.deadline=cp_now()+60};provider.identity=identity;provider.source=&source;provider.bundle.ready=1;strcpy(provider.bundle.source,"Fixture");strcpy(provider.bundle.game,identity.binding.game);strcpy(provider.bundle.title,"PPSA99999");
    size_t size;path(name,fixture,"map.bin");provider.bundle.map.bytes=file_read(name,&provider.bundle.map.size);path(name,fixture,"digests.bin");provider.bundle.digests=file_read(name,&provider.bundle.digest_size);path(name,fixture,"selection.txt");void *selection=file_read(name,&size);CHECK(size<sizeof provider.bundle.selection);memcpy(provider.bundle.selection,selection,size);free(selection);
    struct bw_manifest map;CHECK(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&map,i,&row));if(row.flags==BW_MANIFEST_LOCAL){provider.bundle.map.local_count++;provider.bundle.map.local_bytes+=row.size;}else{provider.bundle.map.stream_count++;provider.bundle.map.remote_bytes+=row.size;}}
    memset(provider.bundle.generation_sha256,0x3a,32);struct bw_native_send_files send={.data_root=test_data,.game_log=log,.provider=&provider,.current=current,.opaque=&provider};struct bw_native_prepared prepared={0};
    CHECK(!bw_native_send_files(&send,&prepared)&&terminal_ready(&prepared));cases++;
    snprintf(test_root,sizeof test_root,"%s/games/%s",test_data,identity.binding.game);snprintf(test_state,sizeof test_state,"%s/native-prepared/%s",test_data,identity.binding.game);preserve_before(argv[1]);
    if(!strcmp(test_scenario,"owned-data-0770"))CHECK(!chmod(test_data,0770));
    if(!strcmp(test_scenario,"owned-games-0770")){path(name,test_data,"games");CHECK(!chmod(name,0770));}
    if(!strcmp(test_scenario,"owned-prepared-0770")){path(name,test_data,"native-prepared");CHECK(!chmod(name,0770));}
    if(!strcmp(test_scenario,"owned-game-0770"))CHECK(!chmod(test_root,0770));
    if(!strcmp(test_scenario,"owned-state-0770"))CHECK(!chmod(test_state,0770));
    if(!strcmp(test_scenario,"shared-apps-symlink")){char other[2100];snprintf(other,sizeof other,"%s.original",test_apps);CHECK(!rename(test_apps,other));CHECK(!symlink(other,test_apps));}
    memset(&registration_selection,0,sizeof registration_selection);strcpy(registration_selection.title,identity.binding.title);strcpy(registration_selection.game,identity.binding.game);strcpy(registration_selection.source,identity.binding.source);registration_selection.source_kind=identity.kind;registration_selection.source_policy=1;
    strcpy(registration_selection.run,"dddddddddddddddddddddddddddddddd");registration_selection.manifest_count=map.count;registration_selection.manifest_hash=map.checksum;registration_selection.eboot_size=262144;
    for(unsigned i=0;i<32;i++){snprintf(registration_selection.binding_hex+2*i,3,"%02x",identity.binding.sha256[i]);snprintf(registration_selection.prepared_record_hex+2*i,3,"%02x",prepared.record_sha256[i]);}
    struct bw_nrc_environment env={test_data,test_apps,test_db};struct bw_game_uninstall_capability capability={0};measuring=1;tree_reads=stream_opens=0;
    int probed=bw_native_game_uninstall_probe(&env,&identity,&prepared,&capability);measuring=0;CHECK(!tree_reads&&!stream_opens);
    if(owned_root_mode_case()||!strcmp(test_scenario,"shared-apps-symlink"))CHECK(probed<0&&!capability.can_uninstall&&!capability.can_restore);
    else if(!strcmp(test_scenario,"foreign-registration")||!strcmp(test_scenario,"pkg-registration")||!strcmp(test_scenario,"foreign-row"))CHECK(!capability.can_uninstall);
    else CHECK(!probed&&capability.can_uninstall&&!capability.removed&&!capability.can_restore);cases++;
    struct bw_game_uninstall_request request={.environment={test_data,test_apps,test_db},.identity=identity,.prepared=prepared,.ops={.opaque=&provider,.current=removal_current,.registration_absent=remove_registration,.source_excluded=excluded,.observe=observe}};
    strcpy(request.operation,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");strcpy(request.confirmed_title,identity.binding.title);
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
