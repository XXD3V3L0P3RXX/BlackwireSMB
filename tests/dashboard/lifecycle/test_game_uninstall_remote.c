/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual strict Send + explicit deletion core + normal remote adapters. Only
 * fixed normal SMP replies/effects and readback time are modeled; no console. */
#define main retained_fast_main
#define bw_sm_post retained_game_sm_post
#include "test_fast_ready_play.c"
#undef bw_sm_post
#undef main
#include "native_game_uninstall_remote.h"
#include "native_registration_maintenance.h"
#include "native_registration_lifecycle.h"
#include "native_cleanup_remote.h"
#include <sqlite3.h>

static unsigned module_opens,module_closes;
static char test_data[2048],test_apps[2048],test_db[2048],test_root[2048],test_state[2048];
static unsigned char before_source[32],before_save[32];
static const char *test_scenario;
static mode_t remote_mode(int flags,va_list a){return flags&O_CREAT?(mode_t)va_arg(a,int):0;}
int gu_test_open(const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=remote_mode(flags,a);va_end(a);int fd=open(name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_openat(int parent,const char *name,int flags,...){va_list a;va_start(a,flags);mode_t m=remote_mode(flags,a);va_end(a);int fd=openat(parent,name,flags,m);if(fd>=0)module_opens++;return fd;}
int gu_test_close(int fd){int rc=close(fd);if(!rc)module_closes++;return rc;}
int gu_test_dup(int fd){int rc=dup(fd);if(rc>=0)module_opens++;return rc;}
int gu_test_closedir(DIR *dir){int rc=closedir(dir);if(!rc)module_closes++;return rc;}
int gu_test_fsync(int fd){return fsync(fd);}
ssize_t gu_test_write(int fd,const void *body,size_t size){return write(fd,body,size);}
int gu_test_unlinkat(int parent,const char *name,int flags){return unlinkat(parent,name,flags);}
static void sql_execute(const char *sql){sqlite3 *db=NULL;CHECK(sqlite3_open(test_db,&db)==SQLITE_OK);char *error=NULL;CHECK(sqlite3_exec(db,sql,NULL,NULL,&error)==SQLITE_OK);sqlite3_free(error);CHECK(sqlite3_close(db)==SQLITE_OK);}
static void hash_name(const char *name,unsigned char digest[32]){size_t size;void *body=file_read(name,&size);cp_sha_bytes(body,size,digest);free(body);}
static void preserve_before(const char *base){char name[2048];path(name,base,"source/eboot.bin");hash_name(name,before_source);path(name,base,"save/saved-game.bin");hash_name(name,before_save);}
static void preserve_after(const char *base){char name[2048];unsigned char actual[32];path(name,base,"source/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));path(name,base,"save/saved-game.bin");hash_name(name,actual);CHECK(!memcmp(before_save,actual,32));path(name,base,"manual-full/eboot.bin");hash_name(name,actual);CHECK(!memcmp(before_source,actual,32));}

static unsigned api_manual,api_remove_calls,api_uninstall_calls,api_unmount_calls,api_mounted,api_reads,api_sleeps;
static unsigned api_mutated,api_fail_current,api_observed,api_late_changed;
static unsigned adapter_close_failed;
static unsigned continuing_files;
static unsigned final_release_versions,final_release_manual,final_release_changed;
static uint64_t virtual_ns;
static char api_scan[2048];
static const char *remote_case;
static int is_case(const char *name){return !strcmp(remote_case,name);}
static int file_case(void){return !strncmp(remote_case,"continue-",9);}
static int maintenance_case(void){return !strncmp(remote_case,"maintenance-",12);}
static int final_release_case(void){return strstr(remote_case,"final-release-")!=NULL;}
static int prepared_ready(const struct bw_native_prepared *p){unsigned nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=p->record_sha256[i];return p->state==BW_PREPARED_VALID&&nonzero;}
static int remote_current(void *p){CHECK(p);return api_fail_current||is_case("active")||is_case("unknown-idle")?-1:0;}
int gur_test_clock_gettime(clockid_t kind,struct timespec *out){CHECK(kind==CLOCK_MONOTONIC);out->tv_sec=(time_t)(100+virtual_ns/1000000000);out->tv_nsec=(long)(virtual_ns%1000000000);return 0;}
int gur_test_nanosleep(const struct timespec *delay,struct timespec *left){CHECK(delay&&delay->tv_sec==0&&delay->tv_nsec==100000000);(void)left;api_sleeps++;virtual_ns+=100000000;return 0;}
int gur_test_close(int fd){struct stat st;int inject=(is_case("adapter-close-uncertain")||(continuing_files&&is_case("continue-close-uncertain")))&&!adapter_close_failed&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&!st.st_nlink;int rc=close(fd);if(inject){adapter_close_failed=1;errno=EIO;return -1;}return rc;}
static void remote_document(struct bw_sm_document *out,const char *text){out->size=strlen(text);out->bytes=malloc(out->size+1);CHECK(out->bytes);memcpy(out->bytes,text,out->size+1);}
void bw_sm_document_clear(struct bw_sm_document *p){free(p->bytes);memset(p,0,sizeof *p);}
static void consumed_registration(void){
    if(maintenance_case()){
        char name[2048];path(name,test_data,"registration-maintenance/PPSA99999.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.manual");
        struct stat st;CHECK(!lstat(name,&st)&&S_ISREG(st.st_mode)&&st.st_size==128&&(st.st_mode&07777)==0600);return;
    }
    char ledger[2048],name[2048];path(ledger,test_data,"game-removals");DIR *dir=opendir(ledger);CHECK(dir);unsigned found=0;
    for(struct dirent *e;(e=readdir(dir));){size_t n=strlen(e->d_name);if(n<13||strcmp(e->d_name+n-13,".registration"))continue;
        path(name,ledger,e->d_name);struct stat st;CHECK(!lstat(name,&st)&&S_ISREG(st.st_mode)&&st.st_size==128&&(st.st_mode&07777)==0600);found++;}
    CHECK(!closedir(dir)&&found==1);
}
static void normal_uninstall_effect(void){
    char name[2048];path(name,test_apps,"PPSA99999/sce_sys/param.json");CHECK(!unlink(name));path(name,test_apps,"PPSA99999/sce_sys");CHECK(!rmdir(name));
    path(name,test_apps,"PPSA99999/mount.lnk");if(unlink(name))CHECK(errno==ENOENT);
    path(name,test_apps,"PPSA99999");CHECK(!rmdir(name));
    if(!is_case("db-row-retained"))sql_execute("DELETE FROM tbl_contentinfo WHERE titleId='PPSA99999';");
}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation operation,const char *root,struct bw_sm_reply *out){
    CHECK(c&&c->current&&!c->current(c->opaque));memset(out,0,sizeof *out);
    if(operation==BW_SM_VERSION){if(final_release_case()&&api_remove_calls)final_release_versions++;out->remove_supported=out->uninstall_supported=out->settings_supported=1;out->unmount_supported=!is_case("unmount-capability");if(is_case("capability-unknown"))out->settings_supported=0;return 0;}
    CHECK(!strcmp(root,test_root));
    if(operation==BW_SM_MANUAL_LIST){
        if(!api_late_changed&&(is_case("send-only-manual-param-change")||is_case("receipt-during-manual"))){
            char name[2048];api_late_changed=1;path(name,is_case("receipt-during-manual")?test_state:test_root,is_case("receipt-during-manual")?"prepared.bin":"sce_sys/param.json");mutate_byte(name);
        }out->present=api_manual;out->count=api_manual;return 0;
    }
    CHECK(operation==BW_SM_MANUAL_REMOVE&&++api_remove_calls==1);consumed_registration();CHECK(!api_mounted);
    if(is_case("manual-no-effect")){out->present=1;return -1;}
    api_manual=0;out->changed=1;
    if(is_case("remove-mount-link")){char name[2048];path(name,test_apps,"PPSA99999/mount.lnk");CHECK(!unlink(name));}
    if(is_case("late-app-replacement")){char name[2048],old[2100];path(name,test_apps,"PPSA99999");snprintf(old,sizeof old,"%s.old",name);CHECK(!rename(name,old)&&!mkdir(name,0755));}
    if(is_case("late-source-replacement")){char old[2100];snprintf(old,sizeof old,"%s.old",test_root);CHECK(!rename(test_root,old)&&!mkdir(test_root,0755));}
    if(is_case("cancel-after-manual"))api_fail_current=1;
    return is_case("manual-lost-reply")?-1:0;
}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    CHECK(c&&c->current&&!c->current(c->opaque));api_reads++;char text[8192];int n=-1;
    if(file_case()&&api_uninstall_calls&&!continuing_files)return -1;
    if(kind==BW_SM_OBSERVE_GAMES){
        if(is_case("malformed-games")){remote_document(out,"{\"status\":0,\"count\":1,\"games\":[]}");return 0;}
        char alternative[2048];path(alternative,test_data,"foreign-same-title");
        const char *source=is_case("alternate-source")?alternative:test_root;
        int empty=(is_case("normal-empty-cache")||file_case())&&api_uninstall_calls;
        const char *content=empty?"":"UP0000-PPSA99999_00-BLACKWIREFIXTURE0";
        if(continuing_files&&is_case("continue-foreign-cache"))content="FOREIGN-CONTENT";
        n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"PPSA99999\",\"content_id\":\"%s\",\"installed\":%s,\"installed_pkg\":%s,\"managed\":false,\"source_available\":true,\"image_backed\":%s,\"mounted\":%s}]}",source,source,content,empty?"false":"true",is_case("pkg-source")?"true":"false",is_case("image-source")?"true":"false",api_mounted?"true":"false");
    }else if(kind==BW_SM_OBSERVE_MANUAL_PATHS){
        if(is_case("unknown-manual"))n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[\"%s/unknown-manual\"]}",test_data);
        else if(api_manual)n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[\"%s\"]}",test_root);
        else n=snprintf(text,sizeof text,"{\"status\":0,\"count\":0,\"paths\":[]}");
    }else{CHECK(kind==BW_SM_OBSERVE_SETTINGS);
        if(is_case("settings-error"))return -1;
        n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",is_case("scan-root")?test_root:api_scan);
        if(is_case("changing-settings")&&api_reads%2==0)n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s/other\"]}",api_scan);
    }
    CHECK(n>0&&(size_t)n<sizeof text);remote_document(out,text);
    /* The third version after manual removal starts the final release
     * snapshot. Replace its retained object after its last HTTP response. */
    if(final_release_case()&&!final_release_changed&&kind==BW_SM_OBSERVE_MANUAL_PATHS&&final_release_versions==3&&++final_release_manual==2){
        char target[2048],saved[2100];const char *root=test_root;
        if(strstr(remote_case,"app-replacement")){path(target,test_apps,"PPSA99999");root=target;}
        snprintf(saved,sizeof saved,"%s.replaced",root);CHECK(!rename(root,saved)&&!mkdir(root,0755));final_release_changed=1;
    }
    return 0;
}
int bw_sm_uninstall(const struct bw_sm_connection *c,const char title[10],struct bw_sm_uninstall_reply *out){
    CHECK(c&&c->current&&!c->current(c->opaque)&&!strcmp(title,"PPSA99999")&&!api_manual&&!api_mounted&&++api_uninstall_calls==1);consumed_registration();memset(out,0,sizeof *out);
    if(final_release_case()){fprintf(stderr,"title-only uninstall reached after final release replacement: changed=%u manual=%u uninstall=%u\n",final_release_changed,api_remove_calls,api_uninstall_calls);return -1;}
    if(is_case("uninstall-no-effect"))return -1;
    normal_uninstall_effect();out->requested=1;return is_case("uninstall-lost-reply")?-1:0;
}
int bw_sm_unmount(const struct bw_sm_connection *c,const char title[10],struct bw_sm_unmount_reply *out){
    CHECK(c&&c->current&&!c->current(c->opaque)&&!strcmp(title,"PPSA99999")&&++api_unmount_calls==1&&!api_remove_calls&&!api_uninstall_calls);consumed_registration();
    char name[2048];path(name,test_apps,"PPSA99999/mount.lnk");struct stat st;CHECK(!lstat(name,&st)&&S_ISREG(st.st_mode));
    CHECK(bw_registration_db_match(test_db,title,"UP0000-PPSA99999_00-BLACKWIREFIXTURE0")==1);
    memset(out,0,sizeof *out);out->response_complete=1;
    if(is_case("runtime-busy")){out->http_status=409;out->api_status=16;return -1;}
    if(!is_case("unmount-no-effect"))api_mounted=0;out->released=1;out->http_status=200;
    return is_case("unmount-lost-reply")?-1:0;
}
static void remote_observe(void *p,const struct bw_game_uninstall_progress *v){CHECK(p&&v&&v->files_done<=v->files_total);api_observed++;
    if(v->phase==BW_GAME_REMOVE_REGISTRATION&&!api_mutated++){
        char pathbuf[2048];if(is_case("receipt-mutated")){path(pathbuf,test_state,"prepared.bin");mutate_byte(pathbuf);}
        if(is_case("local-mutated")){path(pathbuf,test_root,"startup.txt");mutate_byte(pathbuf);}
    }
}
static unsigned descriptor_count(void){DIR *d=opendir("/proc/self/fd");CHECK(d);unsigned n=0;for(struct dirent *e;(e=readdir(d));)if(e->d_name[0]!='.')n++;CHECK(!closedir(d));return n;}
static int owner_current(void *p){struct bw_nrc_remote *r=p;return r->connection.current(r->connection.opaque);}
static void committed_owner(const struct bw_game_uninstall_request *request,const struct bw_manifest *map){
    struct bw_reader_selection selected={0};selected.source_kind=BW_PROVIDER_FOLDER;selected.source_policy=BW_PROVIDER_POLICY_V1;
    strcpy(selected.source,request->identity.binding.source);strcpy(selected.game,request->identity.binding.game);strcpy(selected.title,request->confirmed_title);strcpy(selected.run,"dddddddddddddddddddddddddddddddd");
    selected.eboot_size=262144;selected.manifest_count=map->count;selected.manifest_hash=map->checksum;
    for(unsigned i=0;i<32;i++){snprintf(selected.binding_hex+2*i,3,"%02x",request->identity.binding.sha256[i]);snprintf(selected.prepared_record_hex+2*i,3,"%02x",request->prepared.record_sha256[i]);}
    struct bw_nrc_remote remote={.connection={.current=remote_current,.opaque=request->ops.opaque}};
    struct bw_nrc_ops ops={.opaque=&remote,.current=owner_current,.sources=bw_nrc_remote_sources};
    struct bw_nrc_ticket *ticket=NULL;struct bw_nrc_result result;CHECK(!bw_nrc_begin(&request->environment,&selected,&ops,&ticket,&result));
    char name[2048],app[2048];path(app,test_apps,"PPSA99999");CHECK(!mkdir(app,0755));path(name,app,"sce_sys");CHECK(!mkdir(name,0755));path(name,app,"sce_sys/param.json");
    const char body[]="{\"titleId\":\"PPSA99999\",\"contentId\":\"UP0000-PPSA99999_00-BLACKWIREFIXTURE0\"}";file_write(name,body,sizeof body-1,0644);
    path(name,app,"mount.lnk");char mount[2100];int n=snprintf(mount,sizeof mount,"%s\n",test_root);CHECK(n>0);file_write(name,mount,(size_t)n,0644);sql_execute("INSERT INTO tbl_contentinfo VALUES('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0');");
    struct bw_register_result registered={.add_attempted=1,.registered=1};CHECK(!bw_nrc_finish(ticket,0,&registered,&result)&&result.state==BW_NRC_OWNED);CHECK(!bw_nrc_ticket_destroy(ticket));cases++;
}
static void retained_scope_cases(const char *base){
    char parent[2048],root[2048],moved[2100],scan[2048],text[8192];path(parent,base,"scope-parent");CHECK(!mkdir(parent,0755));path(root,parent,"source");CHECK(!mkdir(root,0755));path(scan,base,"scope-scan");CHECK(!mkdir(scan,0755));
    struct bw_nrl_retained_source w={.parent=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW),.root=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW)};CHECK(w.parent>=0&&w.root>=0&&!fstat(w.parent,&w.parent_identity)&&!fstat(w.root,&w.root_identity));
    struct bw_nrc_subject s={0};strcpy(s.source,root);int okay=1;struct bw_sm_connection c={.current=current,.opaque=&okay};
    int n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",scan);CHECK(n>0&&!bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w));cases++;
    snprintf(moved,sizeof moved,"%s.renamed",root);CHECK(!rename(root,moved));CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w)<0);CHECK(!rename(moved,root));cases++;
    CHECK(!rename(root,moved)&&!mkdir(root,0755));CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w)<0);CHECK(!rmdir(root)&&!rename(moved,root));cases++;
    char oldparent[2100];snprintf(oldparent,sizeof oldparent,"%s.renamed",parent);CHECK(!rename(parent,oldparent)&&!mkdir(parent,0755));CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w)<0);CHECK(!rmdir(parent)&&!rename(oldparent,parent));cases++;
    CHECK(!rmdir(scan)&&!symlink(root,scan));CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w)<0);CHECK(!unlink(scan)&&!mkdir(scan,0755));cases++;
    struct bw_nrl_retained_source wrong=w;wrong.root_identity.st_ino^=1;CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&wrong)<0);cases++;
    wrong=w;wrong.parent=-1;CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&wrong)<0);cases++;
    CHECK(!rmdir(root));struct stat removed;CHECK(!fstat(w.root,&removed)&&removed.st_nlink==0&&!bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w));cases++;
    wrong=w;wrong.root_identity.st_nlink=0;CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&wrong)<0);cases++;
    CHECK(bw_nrl_settings_exclude(&c,&s,text,(size_t)n)<0);cases++; /* prior retirement policy is unchanged */
    CHECK(!mkdir(root,0755));CHECK(bw_nrl_settings_exclude_retained(&c,&s,text,(size_t)n,&w)<0);cases++;
    CHECK(!close(w.root)&&!close(w.parent));
}
int main(int argc,char **argv){
    CHECK(argc==3);remote_case=argv[2];test_scenario=remote_case;scenario=remote_case;char fixture[2048],log[2048],name[2048];
    path(test_data,argv[1],"data");path(source_root,argv[1],"source");path(fixture,argv[1],"metadata");path(log,argv[1],"game.log");path(test_apps,argv[1],"apps");path(test_db,argv[1],"app.db");path(api_scan,argv[1],"unrelated-scan");CHECK(!mkdir(api_scan,0755));
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");struct bw_provider_binding identity;CHECK(!bw_provider_binding_init(&identity,&config,"Fixture","PPSA99999",BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};struct bw_source_smb source={.smb=(struct smb2_context *)(uintptr_t)1,.deadline=cp_now()+60};provider.identity=identity;provider.source=&source;provider.bundle.ready=1;strcpy(provider.bundle.source,"Fixture");strcpy(provider.bundle.game,identity.binding.game);strcpy(provider.bundle.title,"PPSA99999");
    size_t size;path(name,fixture,"map.bin");provider.bundle.map.bytes=file_read(name,&provider.bundle.map.size);path(name,fixture,"digests.bin");provider.bundle.digests=file_read(name,&provider.bundle.digest_size);path(name,fixture,"selection.txt");void *selected=file_read(name,&size);CHECK(size<sizeof provider.bundle.selection);memcpy(provider.bundle.selection,selected,size);free(selected);
    struct bw_manifest map;CHECK(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&map,i,&row));if(row.flags==BW_MANIFEST_LOCAL){provider.bundle.map.local_count++;provider.bundle.map.local_bytes+=row.size;}else{provider.bundle.map.stream_count++;provider.bundle.map.remote_bytes+=row.size;}}
    memset(provider.bundle.generation_sha256,0x3a,32);struct bw_native_send_files send={.data_root=test_data,.game_log=log,.provider=&provider,.current=current,.opaque=&provider};struct bw_native_prepared prepared={0};int sent=bw_native_send_files(&send,&prepared);
    if(sent||!prepared_ready(&prepared))fprintf(stderr,"actual Send result=%d preparedState=%d digestPrefix=%02x%02x retirement=%u reason=%s\n",sent,prepared.state,prepared.record_sha256[0],prepared.record_sha256[1],prepared.retirement,prepared.retirement_error?prepared.retirement_error:"none");
    CHECK(!sent&&prepared_ready(&prepared));cases++;
    snprintf(test_root,sizeof test_root,"%s/games/%s",test_data,identity.binding.game);snprintf(test_state,sizeof test_state,"%s/native-prepared/%s",test_data,identity.binding.game);preserve_before(argv[1]);
    api_manual=!is_case("send-only")&&!is_case("registered-no-manual");api_mounted=is_case("mounted-source")||is_case("runtime-busy")||is_case("unmount-no-effect")||is_case("unmount-lost-reply")||is_case("unmount-capability");
    struct bw_game_uninstall_request request={.environment={test_data,test_apps,test_db},.identity=identity,.prepared=prepared,.ops={.opaque=&provider,.current=remote_current,.observe=remote_observe}};
    strcpy(request.operation,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");strcpy(request.confirmed_title,"PPSA99999");
    if(is_case("owned-registration")||is_case("owner-old-receipt"))committed_owner(&request,&map);
    if(is_case("owner-old-receipt")){unsigned char prior[32];memcpy(prior,prepared.record_sha256,32);char *run=strstr(provider.bundle.selection,"run_id=");CHECK(run);memset(run+7,'c',32);provider.bundle.generation_sha256[0]^=1;source.deadline=cp_now()+60;CHECK(!bw_native_send_files(&send,&prepared)&&memcmp(prior,prepared.record_sha256,32));request.prepared=prepared;cases++;}
    unsigned fds=descriptor_count();struct bw_game_uninstall_result out={0};int rc;
    if(maintenance_case()){
        struct bw_registration_maintenance_request q={.environment=request.environment,.identity=identity,.prepared=prepared,.current=remote_current,.opaque=&provider};
        memcpy(q.operation,request.operation,33);memcpy(q.confirmed_title,request.confirmed_title,10);struct bw_registration_maintenance_result maintained={0};
        rc=bw_native_registration_maintain(&q,0,&maintained);CHECK(descriptor_count()==fds);cases++;
        if(final_release_case())CHECK(rc&&maintained.state==BW_REG_MAINT_UNKNOWN&&final_release_changed&&!api_uninstall_calls&&api_remove_calls==1);
        else CHECK(!rc&&maintained.state==BW_REG_MAINT_ABSENT&&maintained.prepared_retained&&api_uninstall_calls==1&&api_remove_calls==1);
        unsigned removes=api_remove_calls,uninstalls=api_uninstall_calls;
        (void)bw_native_registration_maintain(&q,0,&maintained);CHECK(api_remove_calls==removes&&api_uninstall_calls==uninstalls);cases++;
        goto complete;
    }
    rc=bw_native_game_uninstall_normal(&request,0,&out);CHECK(descriptor_count()==fds);cases++;
    if(file_case()){
        CHECK(rc&&out.phase==BW_GAME_REMOVE_UNKNOWN&&out.mutation_started&&!out.files_removed&&out.files_total>0);
        CHECK(api_remove_calls==1&&api_uninstall_calls==1);consumed_registration();
        char journal[2048],intent[2048],leaf[128],registration[2048],files[2048];path(journal,test_data,"game-removals");
        snprintf(leaf,sizeof leaf,"%s.%s.intent",identity.binding.game,request.operation);path(intent,journal,leaf);
        snprintf(leaf,sizeof leaf,"%s.%s.registration",identity.binding.game,request.operation);path(registration,journal,leaf);
        snprintf(leaf,sizeof leaf,"%s.%s.files",identity.binding.game,request.operation);path(files,journal,leaf);
        struct stat st;CHECK(lstat(files,&st)&&errno==ENOENT);unsigned char ih[32],rh[32],after[32];hash_name(intent,ih);hash_name(registration,rh);
        continuing_files=1;
        if(is_case("continue-recreated-app")){path(name,test_apps,"PPSA99999");CHECK(!mkdir(name,0755));}
        if(is_case("continue-recreated-db"))sql_execute("INSERT INTO tbl_contentinfo VALUES('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0');");
        if(is_case("continue-receipt-drift")){path(name,test_state,"prepared.bin");mutate_byte(name);}
        if(is_case("continue-current-refused"))api_fail_current=1;
        if(is_case("continue-missing-registration"))CHECK(!unlink(registration));
        struct bw_game_uninstall_files_request next={.environment=request.environment,.identity=request.identity,.prepared=request.prepared,
            .ops={.opaque=&provider,.current=remote_current,.observe=remote_observe},.reader_size=request.reader_size};
        memcpy(next.operation,request.operation,33);memcpy(next.confirmed_title,request.confirmed_title,10);memcpy(next.reader_sha256,request.reader_sha256,32);
        unsigned removes=api_remove_calls,uninstalls=api_uninstall_calls;
        rc=bw_native_game_uninstall_files_normal(&next,0,&out);CHECK(descriptor_count()==fds);cases++;
        CHECK(api_remove_calls==removes&&api_uninstall_calls==uninstalls);hash_name(intent,after);CHECK(!memcmp(ih,after,32));
        if(!is_case("continue-missing-registration")){hash_name(registration,after);CHECK(!memcmp(rh,after,32));}
        if(is_case("continue-empty-cache")){if(rc)fprintf(stderr,"files continuation rc=%d phase=%u reason=%s\n",rc,out.phase,out.reason?out.reason:"none");
            CHECK(!rc&&out.phase==BW_GAME_REMOVE_DONE&&out.library_removed&&out.files_removed==out.files_total&&out.files_total>0);
        }else if(is_case("continue-close-uncertain")){
            CHECK(rc&&adapter_close_failed&&out.phase==BW_GAME_REMOVE_UNKNOWN&&out.source_absent&&out.prepared_record_absent);
        }else{CHECK(rc&&out.phase==BW_GAME_REMOVE_UNKNOWN&&!out.files_removed&&!out.library_removed);CHECK(!lstat(test_root,&st));CHECK(lstat(files,&st)&&errno==ENOENT);}
        goto complete;
    }
    unsigned success=is_case("send-only")||is_case("registered")||is_case("registered-no-manual")||is_case("manual-lost-reply")||is_case("uninstall-lost-reply")||is_case("remove-mount-link")||is_case("owned-registration")||is_case("normal-empty-cache")||is_case("mounted-source");
    if(is_case("adapter-close-uncertain")){
        CHECK(rc&&adapter_close_failed&&out.phase==BW_GAME_REMOVE_UNKNOWN&&out.library_removed&&out.source_absent&&out.prepared_record_absent);
        CHECK(out.reason&&!strcmp(out.reason,"normal_adapter_descriptor_close_unconfirmed"));unsigned remove=api_remove_calls,uninstall=api_uninstall_calls;
        CHECK(bw_native_game_uninstall_normal(&request,0,&out)!=0&&api_remove_calls==remove&&api_uninstall_calls==uninstall);cases++;
    }else if(success){if(rc)fprintf(stderr,"normal adapter rc=%d phase=%u reason=%s manual=%u uninstall=%u files=%u/%u\n",rc,out.phase,out.reason?out.reason:"none",api_remove_calls,api_uninstall_calls,out.files_removed,out.files_total);
        CHECK(!rc&&out.phase==BW_GAME_REMOVE_DONE&&out.source_absent&&out.prepared_record_absent&&out.library_removed&&out.files_removed==out.files_total&&out.files_total>0);
        CHECK(api_remove_calls==(is_case("send-only")||is_case("registered-no-manual")?0u:1u));CHECK(api_uninstall_calls==(!is_case("send-only")));
        if(is_case("owned-registration")){struct bw_nrc_candidate list[2];size_t n=0;CHECK(!bw_nrc_candidates(&request.environment,list,2,&n)&&n==0);cases++;}
        /* A fresh adapter has no borrowed deleted-root witness. It may not
         * reconstruct destructive authority or invent completion after a
         * restart, even if the first frame already committed its tombstone. */
        unsigned remove=api_remove_calls,uninstall=api_uninstall_calls;CHECK(bw_native_game_uninstall_normal(&request,0,&out)!=0&&out.phase==BW_GAME_REMOVE_UNKNOWN&&api_remove_calls==remove&&api_uninstall_calls==uninstall);cases++;
    }else{
        if(!rc)fprintf(stderr,"unexpected completion case=%s\n",remote_case);CHECK(rc&&out.phase!=BW_GAME_REMOVE_DONE&&!out.library_removed);
        unsigned remove=api_remove_calls,uninstall=api_uninstall_calls;CHECK(bw_native_game_uninstall_normal(&request,0,&out)!=0&&api_remove_calls==remove&&api_uninstall_calls==uninstall);cases++;
        struct stat st;CHECK(!lstat(test_root,&st));
        if(is_case("runtime-busy")||is_case("unmount-no-effect")||is_case("unmount-lost-reply"))CHECK(api_unmount_calls==1&&!api_remove_calls&&!api_uninstall_calls);
        if(is_case("unmount-capability"))CHECK(!api_unmount_calls&&!api_remove_calls&&!api_uninstall_calls);
        if(is_case("manual-no-effect"))CHECK(api_remove_calls==1&&!api_uninstall_calls&&api_sleeps<=100);
        if(is_case("uninstall-no-effect")||is_case("db-row-retained"))CHECK(api_remove_calls==1&&api_uninstall_calls==1&&api_sleeps<=100);
        if(is_case("late-app-replacement")||is_case("late-source-replacement")||is_case("cancel-after-manual"))CHECK(api_remove_calls==1&&!api_uninstall_calls);
        if(final_release_case())CHECK(final_release_changed&&api_remove_calls==1&&!api_uninstall_calls&&!out.files_removed);
        if(is_case("owner-old-receipt")||is_case("send-only-manual-param-change")||is_case("receipt-during-manual"))CHECK(!api_remove_calls&&!api_uninstall_calls);
    }
complete:
    CHECK(descriptor_count()==fds);preserve_after(argv[1]);CHECK(module_opens==module_closes);
    if(is_case("send-only"))retained_scope_cases(argv[1]);
    printf("{\"passed\":true,\"scenario\":\"%s\",\"cases\":%u,\"checks\":%u,\"actualSend\":true,\"actualCore\":true,\"readonlySqlite\":true,\"unmountCalls\":%u,\"manualRemoveCalls\":%u,\"uninstallCalls\":%u,\"readbackSleeps\":%u,\"sourceOriginalAndSavePreserved\":true,\"consoleActions\":0}\n",remote_case,cases,checks,api_unmount_calls,api_remove_calls,api_uninstall_calls,api_sleeps);
    free(provider.bundle.map.bytes);free((void *)provider.bundle.digests);return 0;
}
