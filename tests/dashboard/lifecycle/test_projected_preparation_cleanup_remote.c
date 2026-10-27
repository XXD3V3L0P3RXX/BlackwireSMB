/* Actual projected Send -> normal read-only SMP exclusion -> preparation-only
 * cleanup. Only SMP transport responses and the outer idle observation are
 * modeled. No positive replacement for the production exclusion collector. */
#define BW_PROJECTED_REMOVE_FIXTURE_ONLY 1
#include "test_projected_game_uninstall_e2e.c"
#include "native_game_uninstall_remote.h"
#include "native_registration_lifecycle.h"
#include "native_cleanup_remote.h"
/* Container source reads belong only to the synthetic producer above. No
 * execution admission or live connection is permitted by this test. */
int bw_container_smb_open_owned_metadata(struct bw_container_smb *p,struct smb2_context **owner,const char *name){(void)p;(void)owner;(void)name;abort();}
int bw_container_smb_close(struct bw_container_smb *p){(void)p;abort();}
static void sql_execute(const char *path,const char *sql){sqlite3 *db=NULL;C(sqlite3_open(path,&db)==SQLITE_OK&&sqlite3_exec(db,sql,NULL,NULL,NULL)==SQLITE_OK&&sqlite3_close(db)==SQLITE_OK);}
static void dir_hash_at(const char *path,const char *relative,struct cp_sha *sha){
    char full[2200];C(snprintf(full,sizeof full,"%s%s%s",path,*relative?"/":"",relative)>0);struct stat st;C(!lstat(full,&st));
    cp_sha_update(sha,relative,strlen(relative)+1);mode_t type=st.st_mode&S_IFMT;cp_sha_update(sha,&type,sizeof type);
    if(S_ISDIR(st.st_mode)){struct dirent **entries=NULL;int n=scandir(full,&entries,NULL,alphasort);C(n>=0);for(int i=0;i<n;i++){const char *name=entries[i]->d_name;if(strcmp(name,".")&&strcmp(name,"..")){char child[1700];C(snprintf(child,sizeof child,"%s%s%s",relative,*relative?"/":"",name)>0);dir_hash_at(path,child,sha);}free(entries[i]);}free(entries);}
    else if(S_ISLNK(st.st_mode)){char bytes[2200];ssize_t n=readlink(full,bytes,sizeof bytes);C(n>0&&(size_t)n<sizeof bytes);cp_sha_update(sha,bytes,(size_t)n);}
    else{C(S_ISREG(st.st_mode));uint8_t h[32];fingerprint(full,h);cp_sha_update(sha,h,32);}
}
static void tree_pin(const char *path,uint8_t hash[32]){struct cp_sha sha;cp_sha_init(&sha);dir_hash_at(path,"",&sha);cp_sha_final(&sha,hash);}
static int same_stat(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static void make_app_named(const char *apps,const char *title,const char *content,char app[1800],char param[2000]){C(snprintf(app,1800,"%s/%s",apps,title)>0&&!mkdir(app,0755));char sys[2000];C(snprintf(sys,sizeof sys,"%s/sce_sys",app)>0&&!mkdir(sys,0755));C(snprintf(param,2000,"%s/param.json",sys)>0);char json[512];int n=snprintf(json,sizeof json,"{\"titleId\":\"%s\",\"contentId\":\"%s\"}",title,content);C(n>0&&(size_t)n<sizeof json);file_write(param,json,(size_t)n);C(snprintf(sys,sizeof sys,"%s/sce_sys/icon0.png",app)>0);file_write(sys,"FOREIGN UNCHANGED ICON",22);}
static void make_app(const char *apps,const char *content,char app[1800],char param[2000]){make_app_named(apps,"PPSA99001",content,app,param);}
static int unexpected_outer_exclusion(void *opaque,const struct bw_removed_source *removed){(void)opaque;(void)removed;C(0);return -1;}
static unsigned descriptor_count(void){DIR *d=opendir("/proc/self/fd");C(d);unsigned n=0;for(struct dirent *e;(e=readdir(d));)if(e->d_name[0]!='.')n++;C(!closedir(d));return n;}

static struct {
    const char *mode;struct smb2_context *context;
    char root[1800],state[1800],apps[1800],db[1800],app[1800],scan[1800],link[2100],moved[1900];
    unsigned reads,games,settings,manual,versions,mutations,changed,ordinary_active,injected;char consumed[2200],extra_app[1800];uint8_t extra_hash[32];
} remote;
static int scenario(const char *s){return !strcmp(remote.mode,s);}
/* Actual Restore absence observation: no positive registration stub. */
static int restore_absent(void *opaque,const struct bw_removed_source *removed){
    C(opaque==remote.context&&removed&&!strcmp(removed->identity.binding.title,"PPSA99001"));if(current(opaque))return -1;
    struct stat st;const char *paths[]={remote.root,remote.state};for(unsigned i=0;i<2;i++)if(!lstat(paths[i],&st)||errno!=ENOENT)return -1;
    char app[2200];C(snprintf(app,sizeof app,"%s/PPSA99001",remote.apps)>0);if(!lstat(app,&st)||errno!=ENOENT)return -1;
    return bw_registration_db_match(remote.db,"PPSA99001","UP0000-PPSA99001_00-BLACKWIREFIXTURE0")!=0||current(opaque)?-1:0;
}
static void document(struct bw_sm_document *out,const char *text){out->size=strlen(text);out->bytes=malloc(out->size+1);C(out->bytes);memcpy(out->bytes,text,out->size+1);}
void bw_sm_document_clear(struct bw_sm_document *p){free(p->bytes);memset(p,0,sizeof *p);}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation operation,const char *root,struct bw_sm_reply *out){
    C(c&&c->port==10101&&c->current&&!c->current(c->opaque));memset(out,0,sizeof *out);
    if(operation==BW_SM_VERSION){C(!strcmp(root,"/"));remote.versions++;out->remove_supported=out->uninstall_supported=out->settings_supported=!scenario("capability-unknown");return 0;}
    if(operation==BW_SM_MANUAL_LIST){C(!strcmp(root,remote.root));remote.manual++;out->present=scenario("manual-present")||scenario("manual-post-present");out->count=out->present;if(scenario("manual-post-changing")&&!(remote.manual%2))out->present=1;return scenario("manual-post-error")?-1:0;}
    remote.mutations++;C(0);return -1;
}
int bw_sm_uninstall(const struct bw_sm_connection *c,const char title[10],struct bw_sm_uninstall_reply *out){(void)c;(void)title;(void)out;remote.mutations++;C(0);return -1;}
int bw_sm_unmount(const struct bw_sm_connection *c,const char title[10],struct bw_sm_unmount_reply *out){(void)c;(void)title;(void)out;remote.mutations++;C(0);return -1;}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    C(c&&c->port==10101&&c->current&&!c->current(c->opaque));remote.reads++;char text[8192];int n=-1;
    if(scenario("read-error"))return -1;
    if(remote.ordinary_active){struct stat consumed;if(!lstat(remote.consumed,&consumed)){C(S_ISREG(consumed.st_mode)&&consumed.st_size==128);remote.injected++;return -1;}}
    if(kind==BW_SM_OBSERVE_GAMES){remote.games++;
        if(scenario("malformed-games"))n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[]}");
        else if(scenario("cached-mounted")||scenario("cached-installed")||scenario("cached-managed")||scenario("cached-pkg")||scenario("cached-foreign"))
            n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"PPSA99001\",\"content_id\":\"%s\",\"installed\":%s,\"installed_pkg\":%s,\"managed\":%s,\"source_available\":true,\"image_backed\":false,\"mounted\":%s}]}",remote.root,remote.root,scenario("cached-foreign")?"FOREIGN":"UP0000-PPSA99001_00-BLACKWIREFIXTURE0",scenario("cached-installed")?"true":"false",scenario("cached-pkg")?"true":"false",scenario("cached-managed")?"true":"false",scenario("cached-mounted")?"true":"false");
        else if(scenario("changing-games")&&!(remote.games%2))n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"PPSA99002\",\"content_id\":\"NEUTRAL\",\"installed\":false,\"installed_pkg\":false,\"managed\":false,\"source_available\":true,\"image_backed\":false,\"mounted\":false}]}",remote.scan,remote.scan);
        else n=snprintf(text,sizeof text,"{\"status\":0,\"count\":0,\"games\":[]}");
    }else if(kind==BW_SM_OBSERVE_MANUAL_PATHS){
        if(scenario("malformed-manual"))n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[]}");
        else if(scenario("manual-present")||scenario("unknown-manual"))n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[\"%s\"]}",scenario("manual-present")?remote.root:"/not-a-known-source");
        else n=snprintf(text,sizeof text,"{\"status\":0,\"count\":0,\"paths\":[]}");
    }else{C(kind==BW_SM_OBSERVE_SETTINGS);remote.settings++;
        if(scenario("malformed-settings"))n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[]}");
        else if(scenario("settings-error"))return -1;
        else n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",scenario("scan-root")?remote.root:remote.scan);
        if(scenario("changing-settings")&&!(remote.settings%2))n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":0,\"scan_paths\":[]}");
        if(!remote.changed&&scenario("other-app-changing-inventory")){
            remote.changed=1;char extra_param[2000],extra_link[2100];make_app_named(remote.apps,"PPSA99003","UP0000-PPSA99003_00-OTHERAPPFIXTURE00",remote.extra_app,extra_param);
            C(snprintf(extra_link,sizeof extra_link,"%s/mount.lnk",remote.extra_app)>0);file_write(extra_link,remote.root,strlen(remote.root));tree_pin(remote.extra_app,remote.extra_hash);
        }
        if(!remote.changed&&(scenario("late-link")||scenario("root-rename")||scenario("app-replacement"))){
            remote.changed=1;
            if(scenario("root-rename")){C(!rename(remote.root,remote.moved));}
            else if(scenario("app-replacement")){C(!rename(remote.app,remote.moved)&&!mkdir(remote.app,0755));file_write(remote.link,remote.root,strlen(remote.root));}
            else file_write(remote.link,remote.root,strlen(remote.root));
        }
    }
    C(n>0&&(size_t)n<sizeof text);document(out,text);return 0;
}

int main(int argc,char **argv){C(argc==6||argc==7);int folder=!strcmp(argv[1],"folder");remote.mode=argv[5];
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;remote.context=&context;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc",scenario("ordinary-unrelated")?"PPSA99002":"PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    char data[1600],apps[1600],db[1600],save[1600],log[1600],state[1800],path[2200],app[1800]={0},param[2000]={0};
    C(snprintf(data,sizeof data,"%s/data",argv[4])>0&&!mkdir(data,0755));C(snprintf(apps,sizeof apps,"%s/apps",argv[4])>0&&!mkdir(apps,0770));C(snprintf(db,sizeof db,"%s/app.db",argv[4])>0);empty_db(db);
    C(snprintf(save,sizeof save,"%s/saved-game.bin",argv[4])>0);file_write(save,"untouched save",14);uint8_t save_hash[32];fingerprint(save,save_hash);
    C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);file_write(log,"",0);
    struct bw_source_provider provider={0};C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    struct bw_native_send_files send={.data_root=data,.game_log=log,.provider=&provider,.current=current,.opaque=&context};struct bw_native_prepared prepared={0};C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);
    C(snprintf(remote.root,sizeof remote.root,"%s/games/%s",data,binding.binding.game)>0);C(snprintf(state,sizeof state,"%s/native-prepared/%s",data,binding.binding.game)>0);C(snprintf(remote.scan,sizeof remote.scan,"%s/outside-scan",argv[4])>0&&!mkdir(remote.scan,0755));
    strcpy(remote.state,state);strcpy(remote.apps,apps);strcpy(remote.db,db);
    bw_source_provider_destroy(&provider);C(!context.open);unsigned requests=context.requests,fences=input.fences;
    C(snprintf(path,sizeof path,"%s/fakelib2",remote.root)>0);C(!mkdir(path,0755)||errno==EEXIST);C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",remote.root)>0);file_write(path,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size);C(!chmod(path,0755));
    struct bw_native_launch_admission *admission=NULL;C(!bw_native_launch_admission_open_for_removal(&admission,data,&binding,&prepared,"cccccccccccccccccccccccccccccccc",current,&context,NULL));const struct bw_preload_companions *companions=NULL;C(!bw_native_launch_admission_companions_for_removal(admission,&companions));
    struct bw_preload_bundle_upgrade_request selection={.bundle={remote.root,current,&context,now()+20},.data_root=data,.game=binding.binding.game,.prepared_run=prepared.run,.launch_run="cccccccccccccccccccccccccccccccc",.prepared_record_sha256=prepared.record_sha256};struct bw_preload_reader_pin pin={0};struct bw_preload_bundle_result diagnostic={0};C(!bw_preload_bundle_select_projection_for_removal(&selection,companions,&pin,&diagnostic));C(!bw_native_launch_admission_close(&admission));
    uint8_t ordinary_journal[32]={0},other_game_hash[32]={0},other_state_hash[32]={0};char ordinary_ledger[1800]={0},other_game[1800]={0},other_state[1800]={0};struct bw_game_uninstall_result ordinary={0};int ordinary_rc=0;
    int pending_ordinary=scenario("ordinary-consumed")||scenario("ordinary-other-binding")||scenario("ordinary-unrelated")||scenario("ordinary-intent-malformed")||scenario("ordinary-intent-linked")||scenario("ordinary-registration-missing")||scenario("ordinary-registration-modified");
    if(pending_ordinary||scenario("ordinary-completed")){
        C(snprintf(ordinary_ledger,sizeof ordinary_ledger,"%s/game-removals",data)>0);C(snprintf(remote.consumed,sizeof remote.consumed,"%s/%s.cccccccccccccccccccccccccccccccc.registration",ordinary_ledger,binding.binding.game)>0);
        struct bw_game_uninstall_request old={.environment={data,apps,db},.identity=binding,.prepared=prepared,.ops={.opaque=&context,.current=current},.reader_size=pin.size};memcpy(old.reader_sha256,pin.sha256,32);strcpy(old.operation,"cccccccccccccccccccccccccccccccc");strcpy(old.confirmed_title,binding.binding.title);
        remote.ordinary_active=pending_ordinary;ordinary_rc=bw_native_game_uninstall_normal(&old,10101,&ordinary);remote.ordinary_active=0;
        if(pending_ordinary)C(ordinary_rc<0&&ordinary.phase==BW_GAME_REMOVE_UNKNOWN&&ordinary.mutation_started&&!ordinary.files_removed&&remote.injected>0);else C(!ordinary_rc&&ordinary.phase==BW_GAME_REMOVE_DONE&&ordinary.registration_confirmed&&ordinary.library_removed&&ordinary.files_removed==ordinary.files_total&&ordinary.files_total>0&&!remote.injected);
        struct stat s;C(!lstat(remote.consumed,&s)&&s.st_size==128);char intent[2200];C(snprintf(intent,sizeof intent,"%s/%s.cccccccccccccccccccccccccccccccc.intent",ordinary_ledger,binding.binding.game)>0&&!lstat(intent,&s)&&s.st_size>128);
        C(snprintf(intent,sizeof intent,"%s/%s.cccccccccccccccccccccccccccccccc.files",ordinary_ledger,binding.binding.game)>0);
        if(pending_ordinary)C(lstat(intent,&s)<0&&errno==ENOENT);else C(!lstat(intent,&s)&&s.st_size==128);
        /* Fault only artifacts emitted by the real ordinary producer above. */
        if(scenario("ordinary-intent-malformed")||scenario("ordinary-intent-linked")){
            C(snprintf(intent,sizeof intent,"%s/%s.cccccccccccccccccccccccccccccccc.intent",ordinary_ledger,binding.binding.game)>0);
            if(scenario("ordinary-intent-malformed"))mutate(intent);
            else{char held[2200];C(snprintf(held,sizeof held,"%s/held-original-intent",argv[4])>0&&!rename(intent,held)&&!symlink(held,intent));}
        }
        if(scenario("ordinary-registration-missing"))C(!unlink(remote.consumed));
        if(scenario("ordinary-registration-modified"))mutate(remote.consumed);
        tree_pin(ordinary_ledger,ordinary_journal);
        if(scenario("ordinary-completed")||scenario("ordinary-other-binding")||scenario("ordinary-unrelated")){
            if(scenario("ordinary-completed")){struct bw_removed_ops restore={.opaque=&context,.current=current,.absent=restore_absent};struct bw_removed_result restored={0};C(!bw_removed_restore(data,&binding,"ffffffffffffffffffffffffffffffff",&restore,&restored)&&restored.state==BW_REMOVED_RESTORED);}
            else{strcpy(other_game,remote.root);strcpy(other_state,state);tree_pin(other_game,other_game_hash);tree_pin(other_state,other_state_hash);char old_game[75];strcpy(old_game,binding.binding.game);if(scenario("ordinary-unrelated")){C(folder&&argc==7&&strlen(argv[6])<sizeof context.root);strcpy(context.root,argv[6]);}else strcpy(config.share,"OtherFixtureShare");C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER)&&strcmp(old_game,binding.binding.game));C(snprintf(remote.root,sizeof remote.root,"%s/games/%s",data,binding.binding.game)>0);C(snprintf(state,sizeof state,"%s/native-prepared/%s",data,binding.binding.game)>0);strcpy(remote.state,state);}
            uint8_t old_receipt[32];memcpy(old_receipt,prepared.record_sha256,32);
            C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
            C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3&&memcmp(old_receipt,prepared.record_sha256,32));bw_source_provider_destroy(&provider);C(!context.open);requests=context.requests;fences=input.fences;
            C(snprintf(path,sizeof path,"%s/fakelib2",remote.root)>0);C(!mkdir(path,0755)||errno==EEXIST);C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",remote.root)>0);file_write(path,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size);C(!chmod(path,0755));
            C(!bw_native_launch_admission_open_for_removal(&admission,data,&binding,&prepared,"dddddddddddddddddddddddddddddddd",current,&context,NULL));C(!bw_native_launch_admission_companions_for_removal(admission,&companions));selection.prepared_run=prepared.run;selection.launch_run="dddddddddddddddddddddddddddddddd";selection.prepared_record_sha256=prepared.record_sha256;
            C(!bw_preload_bundle_select_projection_for_removal(&selection,companions,&pin,&diagnostic));C(!bw_native_launch_admission_close(&admission));
        }
    }
    if(!scenario("app-absent")){make_app(apps,scenario("foreign-db")?"UP0000-PPSA99001_00-OTHERFIXTURE0000":"UP0000-PPSA99001_00-BLACKWIREFIXTURE0",app,param);C(snprintf(remote.app,sizeof remote.app,"%s",app)>0);C(snprintf(remote.link,sizeof remote.link,"%s/mount.lnk",app)>0);sql_execute(db,scenario("foreign-db")?"INSERT INTO tbl_contentinfo VALUES('PPSA99001','UP0000-PPSA99001_00-OTHERFIXTURE0000')":"INSERT INTO tbl_contentinfo VALUES('PPSA99001','UP0000-PPSA99001_00-BLACKWIREFIXTURE0')");}
    int other_app=!strncmp(remote.mode,"other-app-",10);char held_other_app[1800]={0};uint8_t held_other_hash[32]={0};
    if(other_app){
        char moved_app[1800];C(snprintf(moved_app,sizeof moved_app,"%s/PPSA99002",apps)>0&&!rename(app,moved_app));strcpy(app,moved_app);
        C(snprintf(param,sizeof param,"%s/sce_sys/param.json",app)>0&&!unlink(param));const char different_param[]="{\"titleId\":\"PPSA99002\",\"contentId\":\"UP0000-PPSA99002_00-OTHERAPPFIXTURE00\"}";file_write(param,different_param,sizeof different_param-1);
        strcpy(remote.app,app);C(snprintf(remote.link,sizeof remote.link,"%s/mount.lnk",app)>0);
        const char *target=scenario("other-app-root")?remote.root:scenario("other-app-state")?state:remote.scan;
        if(scenario("other-app-linked-mount"))C(!symlink(remote.scan,remote.link));
        else if(scenario("other-app-nonregular-mount"))C(!mkdir(remote.link,0755));
        else if(scenario("other-app-malformed-mount"))file_write(remote.link,"../not-canonical",16);
        else if(scenario("other-app-oversized-mount")){char body[BW_NRC_PATH_BYTES+3];memset(body,'a',sizeof body);body[0]='/';file_write(remote.link,body,sizeof body);}
        else if(!scenario("other-app-no-link"))file_write(remote.link,target,strlen(target));
        if(scenario("other-app-linked-directory")){C(snprintf(held_other_app,sizeof held_other_app,"%s/held-other-app",argv[4])>0&&!rename(app,held_other_app)&&!symlink(held_other_app,app));tree_pin(held_other_app,held_other_hash);}
    }
    C(snprintf(remote.moved,sizeof remote.moved,"%s.held-old",scenario("app-replacement")?app:remote.root)>0);
    if(scenario("link-root")||scenario("link-parent")||scenario("link-child")||scenario("link-foreign")||scenario("link-alias")||scenario("link-malformed")||scenario("link-unreadable")){
        const char *target=remote.root;char alias[2200],parent[2200];
        if(scenario("link-parent")){C(snprintf(parent,sizeof parent,"%s/games",data)>0);target=parent;}
        if(scenario("link-child")){C(snprintf(parent,sizeof parent,"%s/sce_sys",remote.root)>0);target=parent;}
        if(scenario("link-foreign"))target=remote.scan;
        if(scenario("link-alias")){C(snprintf(alias,sizeof alias,"%s/alias",argv[4])>0&&!symlink(remote.root,alias));target=alias;}
        if(scenario("link-malformed"))target="../not-canonical";
        if(scenario("link-unreadable")){C(!symlink(remote.root,remote.link));}else file_write(remote.link,target,strlen(target));
    }
    if(scenario("link-state")||scenario("link-state-alias")||scenario("link-state-parent")||scenario("link-state-child")){
        const char *target=state;char extra[2200];if(scenario("link-state-alias")){C(snprintf(extra,sizeof extra,"%s/state-alias",argv[4])>0&&!symlink(state,extra));target=extra;}
        if(scenario("link-state-parent")){C(snprintf(extra,sizeof extra,"%s/native-prepared",data)>0);target=extra;}if(scenario("link-state-child")){C(snprintf(extra,sizeof extra,"%s/prepared.bin",state)>0);target=extra;}file_write(remote.link,target,strlen(target));
    }
    if(scenario("scan-alias")){C(!rmdir(remote.scan)&&!symlink(remote.root,remote.scan));}
    if(scenario("foreign-reader")){C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",remote.root)>0);mutate(path);}
    if(scenario("active")||scenario("unknown-idle"))context.canceled=1;
    uint8_t app_hash[32]={0},db_hash[32],game_hash[32],state_hash[32];struct stat db_before,param_before={0},app_before={0};fingerprint(db,db_hash);C(!lstat(db,&db_before));tree_pin(remote.root,game_hash);tree_pin(state,state_hash);if(*app){tree_pin(app,app_hash);C(!lstat(param,&param_before)&&!lstat(app,&app_before));}
    struct bw_preparation_remove_request request={.environment={data,apps,db},.identity=binding,.prepared=prepared,.ops={.opaque=&context,.current=current,.source_excluded=unexpected_outer_exclusion},.reader_size=pin.size};memcpy(request.reader_sha256,pin.sha256,32);strcpy(request.operation,(pending_ordinary||scenario("ordinary-completed"))?"dddddddddddddddddddddddddddddddd":"cccccccccccccccccccccccccccccccc");strcpy(request.confirmed_title,binding.binding.title);
    if(pending_ordinary||scenario("ordinary-completed")||scenario("link-state")||scenario("link-state-parent")||other_app){
        struct bw_nrc_environment e={data,apps,db};struct bw_game_uninstall_capability cap={0};int probe=bw_native_game_uninstall_probe(&e,&binding,&prepared,&cap);
        fprintf(stderr,"REGRESSION ADVISORY case=%s rc=%d canRemoveStartup=%u canUninstall=%u reason=%s\n",remote.mode,probe,cap.can_remove_startup,cap.can_uninstall,cap.reason?cap.reason:"");C(!probe&&cap.can_remove_startup&&!cap.can_uninstall);
    }
    unsigned descriptors=descriptor_count();struct bw_game_uninstall_result result={0};int rc=bw_native_preparation_remove_normal(&request,10101,&result);unsigned reads=remote.reads;
    /* Same-source installed/managed cache flags are not live-mount authority.
     * Existing strict policy accepts them only with the independent app-link,
     * manual, complete effective-root and current/idle proofs below. */
    int positive=scenario("missing-link")||scenario("foreign-db")||scenario("app-absent")||scenario("link-foreign")||scenario("cached-installed")||scenario("cached-managed")||scenario("ordinary-completed")||scenario("ordinary-unrelated")||scenario("other-app-outside")||scenario("other-app-no-link");
    if(positive){if(rc)fprintf(stderr,"normal cleanup rc%d reason%s files%u/%u reads%u\n",rc,result.reason?result.reason:"",result.files_removed,result.files_total,remote.reads);C(!rc&&result.phase==BW_GAME_REMOVE_DONE&&result.preparation_only&&result.source_absent&&result.prepared_record_absent&&!result.registration_confirmed&&!result.library_removed&&result.files_removed==result.files_total&&result.files_total>0&&!result.release_unknown);struct stat gone;C(lstat(remote.root,&gone)<0&&errno==ENOENT&&lstat(state,&gone)<0&&errno==ENOENT);C(reads>0&&remote.manual>0&&remote.versions>0);struct bw_game_uninstall_result replay={0};C(bw_native_preparation_remove_normal(&request,10101,&replay)<0&&!replay.files_removed&&remote.reads==reads);}
    else{if(!rc)fprintf(stderr,"UNEXPECTED remote success %s\n",remote.mode);C(rc<0&&!result.files_removed&&!result.preparation_only);uint8_t after[32];tree_pin(scenario("root-rename")&&remote.changed?remote.moved:remote.root,after);C(!memcmp(game_hash,after,32));tree_pin(state,after);C(!memcmp(state_hash,after,32));}
    if(pending_ordinary||scenario("ordinary-completed")){uint8_t after_journal[32];tree_pin(ordinary_ledger,after_journal);C(!memcmp(ordinary_journal,after_journal,32));if(*other_game){tree_pin(other_game,after_journal);C(!memcmp(other_game_hash,after_journal,32));tree_pin(other_state,after_journal);C(!memcmp(other_state_hash,after_journal,32));}}
    fprintf(stderr,"REGRESSION HISTORY case=%s firstOrdinaryRc=%d firstPhase=%u firstMutation=%u firstFiles=%u injectedReadFailures=%u secondRc=%d secondPhase=%u secondMutation=%u secondFiles=%u/%u secondReason=%s\n",remote.mode,ordinary_rc,(unsigned)ordinary.phase,ordinary.mutation_started,ordinary.files_removed,remote.injected,rc,(unsigned)result.phase,result.mutation_started,result.files_removed,result.files_total,result.reason?result.reason:"");
    if(pending_ordinary&&!scenario("ordinary-unrelated")){C(result.phase==BW_GAME_REMOVE_UNKNOWN);struct stat no_new;C(snprintf(path,sizeof path,"%s/preparation-removals",data)>0&&lstat(path,&no_new)<0&&errno==ENOENT);}
    if(scenario("link-state-child"))C(result.files_total>0&&result.reason&&strstr(result.reason,"existing console mount"));
    if(scenario("changing-games"))C(remote.games>=2);
    if(scenario("changing-settings"))C(remote.settings>=2);
    if(scenario("manual-present"))C(remote.manual>0);
    if(scenario("other-app-changing-inventory")){C(remote.changed&&*remote.extra_app);uint8_t preserved[32];tree_pin(remote.extra_app,preserved);C(!memcmp(remote.extra_hash,preserved,32));}
    if(*held_other_app){uint8_t preserved[32];tree_pin(held_other_app,preserved);C(!memcmp(held_other_hash,preserved,32));}
    C(descriptor_count()==descriptors);
    if(scenario("app-replacement")&&remote.changed){C(!unlink(remote.link)&&!rmdir(app)&&!rename(remote.moved,app));}
    if(scenario("late-link")&&remote.changed)C(!unlink(remote.link));
    struct bw_removed_source removed={0};struct bw_removed_result hidden={0};C(!bw_removed_lookup(data,&binding,&removed,&hidden)&&hidden.state==BW_REMOVED_VISIBLE);
    uint8_t after[32];struct stat st;fingerprint(db,after);C(!memcmp(db_hash,after,32)&&!lstat(db,&st)&&same_stat(&db_before,&st));fingerprint(save,after);C(!memcmp(save_hash,after,32));if(*app){tree_pin(app,after);C(!memcmp(app_hash,after,32)&&!lstat(param,&st)&&same_stat(&param_before,&st));C(!lstat(app,&st)&&st.st_dev==app_before.st_dev&&st.st_ino==app_before.st_ino&&st.st_mode==app_before.st_mode);}else{C(snprintf(path,sizeof path,"%s/PPSA99001",apps)>0&&lstat(path,&st)<0&&errno==ENOENT);}
    C(!remote.mutations&&!registration_calls&&context.requests==requests&&input.fences==fences&&!context.open);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"mode\":\"%s\",\"removedPreparation\":%s,\"actualNormalAdapter\":true,\"actualRemoteCollectorAndScanExclusion\":true,\"transportReads\":%u,\"mutationTransportCalls\":%u,\"postPreparationSourceRequests\":0,\"externalAppAndDbPreserved\":true,\"consoleActions\":0}\n",checks,remote.mode,positive?"true":"false",reads,remote.mutations);return 0;
}
