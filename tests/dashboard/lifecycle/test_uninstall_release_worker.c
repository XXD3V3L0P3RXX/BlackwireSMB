/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual removal worker invokes the actual normal adapter/core. Only the
 * external settings/idle observation is a fixed host fixture. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#include "preload_bundle.h"
#include "native_backend.c"
static struct bw_native_backend release_owner;
static struct bw_share_config release_config;
static struct bw_setup_generation release_generation;
static const struct bw_game_uninstall_request *release_request;
static unsigned release_initialized,release_calls,release_force_idle,release_idle_calls,release_checks,release_pending;
static int release_status;
#define RCHECK(x) do{release_checks++;if(!(x)){fprintf(stderr,"release worker line %d: %s\n",__LINE__,#x);abort();}}while(0)

/* Fresh normal Send admission against actual NRC lifecycle records. The
 * separate subtree is host fixture data; it never repairs/adopts console files. */
#include "native_registration_retirement.h"
#include <dirent.h>
#include <fcntl.h>
#include <sqlite3.h>
static unsigned owner_admission_cases,owner_admission_retry_cases,owner_admission_reserved_release,owner_admission_close_arm,owner_admission_close_injected;
static char owner_admission_close_name[2048];
unsigned bw_test_send_owner_admission_cases(void){return owner_admission_cases;}
unsigned bw_test_send_owner_admission_retry_cases(void){return owner_admission_retry_cases;}
unsigned bw_test_send_owner_reserved_release(void){return owner_admission_reserved_release;}
int bw_test_send_owner_close_fault(int fd){
    if(!owner_admission_close_arm)return 0;char proc[64],name[4096];snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(proc,name,sizeof name-1);if(n<=0)return 0;name[n]=0;
    if(strcmp(name,owner_admission_close_name))return 0;
    owner_admission_close_arm=0;owner_admission_close_injected++;return 1;
}
static void oa_path(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);RCHECK(n>0&&n<2048);}
static void oa_file(const char *name,const void *data,size_t size,mode_t mode){
    int fd=open(name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,mode);RCHECK(fd>=0);
    const unsigned char *p=data;size_t at=0;while(at<size){ssize_t n=write(fd,p+at,size-at);RCHECK(n>0);at+=(size_t)n;}
    RCHECK(!fsync(fd)&&!close(fd));
}
static void oa_sql(const char *database,const char *sql){
    sqlite3 *db=NULL;RCHECK(sqlite3_open(database,&db)==SQLITE_OK);char *error=NULL;
    RCHECK(sqlite3_exec(db,sql,NULL,NULL,&error)==SQLITE_OK);sqlite3_free(error);RCHECK(sqlite3_close(db)==SQLITE_OK);
}
static int oa_name_order(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
static void oa_tree(struct cp_sha *hash,const char *name,unsigned depth){
    RCHECK(depth<12);struct stat st;RCHECK(!lstat(name,&st));cp_sha_update(hash,&st.st_mode,sizeof st.st_mode);
    cp_sha_update(hash,&st.st_dev,sizeof st.st_dev);cp_sha_update(hash,&st.st_ino,sizeof st.st_ino);
    cp_sha_update(hash,&st.st_nlink,sizeof st.st_nlink);cp_sha_update(hash,&st.st_size,sizeof st.st_size);
    if(S_ISDIR(st.st_mode)){
        DIR *dir=opendir(name);RCHECK(dir);char *names[256];size_t count=0;struct dirent *entry;
        while((entry=readdir(dir))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;RCHECK(count<256);names[count++]=strdup(entry->d_name);RCHECK(names[count-1]);}
        RCHECK(!closedir(dir));qsort(names,count,sizeof *names,oa_name_order);
        for(size_t i=0;i<count;i++){char child[2048];cp_sha_update(hash,names[i],strlen(names[i])+1);oa_path(child,name,names[i]);oa_tree(hash,child,depth+1);free(names[i]);}
    }else{RCHECK(S_ISREG(st.st_mode));int fd=open(name,O_RDONLY|O_NOFOLLOW);RCHECK(fd>=0);unsigned char bytes[4096];ssize_t n;
        while((n=read(fd,bytes,sizeof bytes))>0)cp_sha_update(hash,bytes,(size_t)n);RCHECK(!n&&!close(fd));}
}
static void oa_snapshot(const char *area,unsigned char out[32]){struct cp_sha hash;cp_sha_init(&hash);oa_tree(&hash,area,0);cp_sha_final(&hash,out);}
struct owner_admission_context {
    struct bw_share_config config;struct bw_setup_generation generation;struct bw_provider_binding selected;
    char apps[2048],database[2048];unsigned nonce_calls,prepare_calls,uninstall_calls,manual,complete;
};
static int oa_current(void *p){RCHECK(p);return 0;}
static int oa_sources(void *p,const struct bw_nrc_subject *s){RCHECK(p&&s&&!strcmp(s->title,"PPSA99999"));return 0;}
static int oa_subject_ok(void *p,const struct bw_nrc_subject *s){return oa_sources(p,s);}
static int oa_manual(void *p,const struct bw_nrc_subject *s){RCHECK(!oa_sources(p,s));return ((struct owner_admission_context *)p)->manual?1:0;}
static int oa_manual_remove(void *p,const struct bw_nrc_subject *s){RCHECK(!oa_sources(p,s));return 1;}
static int oa_uninstall(void *opaque,const char title[10]){
    struct owner_admission_context *p=opaque;RCHECK(!strcmp(title,"PPSA99999"));p->uninstall_calls++;
    if(!p->complete)return 1;char app[2048],name[2048];oa_path(app,p->apps,title);
    oa_path(name,app,"sce_sys/param.json");RCHECK(!unlink(name));oa_path(name,app,"sce_sys");RCHECK(!rmdir(name));
    oa_path(name,app,"mount.lnk");RCHECK(!unlink(name)&&!rmdir(app));
    oa_sql(p->database,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA99999'");return 0;
}
static int oa_settings(void *opaque,struct bw_share_config *config,struct bw_setup_generation *generation){
    struct owner_admission_context *p=opaque;*config=p->config;*generation=p->generation;return BW_SETUP_VALID;
}
static int oa_nonce(void *opaque,char out[33]){struct owner_admission_context *p=opaque;p->nonce_calls++;memset(out,'e',32);out[32]=0;return 0;}
static int oa_prepare(void *opaque,struct bw_share_config *config,const struct bw_native_prepare_selection *selection,
    int (*guard)(void *),void *owner,bw_source_observer_fn observer,void *progress,struct bw_native_prepared *out){
    struct owner_admission_context *p=opaque;(void)observer;(void)progress;(void)out;
    RCHECK(config_equal(config,&p->config)&&!memcmp(&selection->identity,&p->selected,sizeof p->selected)&&guard&&!guard(owner));
    p->prepare_calls++;return BW_SEND_FAILED; /* Admission proved; no fixture copy or fabricated Ready. */
}
void bw_test_send_owner_admission(const struct bw_game_uninstall_request *original){
    const char *modes[]={"empty","intent","same","other","pending","retiring","complete","malformed","linked","close-unknown"};
    const char *saved_data=backend_data_root;struct bw_nrc_environment saved_environment=registration_environment;
    for(unsigned mode=0;mode<sizeof modes/sizeof *modes;mode++){
        struct owner_admission_context context={0};strcpy(context.config.server,"192.0.2.1");strcpy(context.config.share,"FixtureShare");
        context.generation.present=1;context.generation.file.st_mode=S_IFREG|0600;context.generation.file.st_nlink=1;memset(context.generation.sha256,0x51,32);
        char area[2048],leaf[128],data[2048],name[2048],root[2048],moved[2100],log[2048];unsigned char marker_before=0;
        snprintf(leaf,sizeof leaf,"send-owner-admission-%s",modes[mode]);oa_path(area,original->environment.data_root,leaf);RCHECK(!mkdir(area,0700));
        oa_path(data,area,"data");RCHECK(!mkdir(data,0700));oa_path(context.apps,area,"apps");RCHECK(!mkdir(context.apps,0700));oa_path(context.database,area,"app.db");
        oa_sql(context.database,"CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)");
        struct bw_nrc_environment environment={data,context.apps,context.database};
        struct bw_source_provider *provider=original->ops.opaque;struct bw_native_prepared prepared={0};
        oa_path(log,area,"game.log");oa_file(log,"",0,0644);struct bw_native_send_files actual={.data_root=data,.game_log=log,.provider=provider,.current=oa_current,.opaque=&context};
        RCHECK(!bw_native_send_files(&actual,&prepared)&&prepared.state==BW_PREPARED_VALID);
        struct bw_reader_selection selected={0};struct bw_manifest manifest;
        RCHECK(!bw_manifest_open(&manifest,provider->bundle.map.bytes,provider->bundle.map.size));
        selected.source_kind=provider->identity.kind;selected.source_policy=provider->identity.policy;
        strcpy(selected.source,provider->identity.binding.source);strcpy(selected.game,provider->identity.binding.game);strcpy(selected.title,provider->identity.binding.title);
        selected.manifest_count=manifest.count;selected.manifest_hash=manifest.checksum;
        for(uint32_t i=0;i<manifest.count;i++){struct bw_manifest_record row;RCHECK(!bw_manifest_get(&manifest,i,&row));if(!strcmp(row.path,"eboot.bin"))selected.eboot_size=row.size;}
        RCHECK(selected.eboot_size);memset(selected.run,'d',32);selected.run[32]=0;
        for(unsigned i=0;i<32;i++){snprintf(selected.prepared_record_hex+2*i,3,"%02x",prepared.record_sha256[i]);snprintf(selected.binding_hex+2*i,3,"%02x",provider->identity.binding.sha256[i]);}
        oa_path(name,data,"games");oa_path(root,name,original->identity.binding.game);
        struct bw_nrc_ops ops={.opaque=&context,.current=oa_current,.sources=oa_sources,.uninstall=oa_uninstall};
        struct bw_nrc_ticket *ticket=NULL;struct bw_nrc_result result={0};
        if(mode){
            RCHECK(!bw_nrc_begin(&environment,&selected,&ops,&ticket,&result)&&ticket);
            if(mode!=1){
                char app[2048];oa_path(app,context.apps,selected.title);RCHECK(!mkdir(app,0755));oa_path(name,app,"sce_sys");RCHECK(!mkdir(name,0755));
                oa_path(name,app,"sce_sys/param.json");const char param[]="{\"titleId\":\"PPSA99999\",\"contentId\":\"UP0000-PPSA99999_00-BLACKWIREFIXTURE0\"}";
                oa_file(name,param,sizeof param-1,0644);oa_path(name,app,"mount.lnk");char mount[2100];int n=snprintf(mount,sizeof mount,"%s\n",root);RCHECK(n>0&&(size_t)n<sizeof mount);oa_file(name,mount,(size_t)n,0644);
                oa_sql(context.database,"INSERT INTO tbl_contentinfo VALUES('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0')");
                struct bw_register_result registered={.add_attempted=1,.registered=1};RCHECK(!bw_nrc_finish(ticket,0,&registered,&result)&&result.state==BW_NRC_OWNED);
            }
            RCHECK(!bw_nrc_ticket_destroy(ticket));ticket=NULL;
        }
        if(mode==4||mode==6){
            int n=snprintf(moved,sizeof moved,"%s.retained",root);RCHECK(n>0&&(size_t)n<sizeof moved&&!rename(root,moved));context.complete=mode==6;
            RCHECK(!bw_nrc_reconcile(&environment,selected.title,selected.run,&ops,&result)&&result.state==BW_NRC_PENDING&&context.uninstall_calls==1);
            if(mode==6)RCHECK(!bw_nrc_reconcile(&environment,selected.title,selected.run,&ops,&result)&&result.state==BW_NRC_ABSENT&&context.uninstall_calls==1);
        }
        if(mode==5){
            context.manual=1;struct bw_nrc_retirement_ops retirement={.registration=ops,.released=oa_subject_ok,.outside_scan_roots=oa_subject_ok,.manual_present=oa_manual,.manual_remove=oa_manual_remove};
            struct bw_nrc_retirement_result retired={0};RCHECK(!bw_nrc_retire_present(&environment,selected.title,selected.run,&retirement,&retired)&&retired.registration.state==BW_NRC_PENDING&&retired.manual_remove_attempted&&!context.uninstall_calls);
        }
        if(mode==7){oa_path(name,data,"registration-owners/.blackwire-registration-owners");int fd=open(name,O_RDWR|O_NOFOLLOW);RCHECK(fd>=0&&pread(fd,&marker_before,1,0)==1&&pwrite(fd,"X",1,0)==1&&!fsync(fd)&&!close(fd));}
        if(mode==8){char owner[2048],foreign[2048];snprintf(leaf,sizeof leaf,"%s.%s.owner",selected.title,selected.run);oa_path(name,data,"registration-owners");oa_path(owner,name,leaf);oa_path(foreign,area,"linked-owner");RCHECK(!link(owner,foreign));}
        struct bw_nrc_candidate candidates[BW_NRC_CANDIDATE_LIMIT];size_t count=0;int inventory=bw_nrc_candidates(&environment,candidates,BW_NRC_CANDIDATE_LIMIT,&count);
        if(mode==7||mode==8)RCHECK(inventory&&count==0);
        else{RCHECK(!inventory&&count==(mode==0||mode==1||mode==6?0u:1u));if(mode==4)RCHECK(candidates[0].pending&&!candidates[0].retiring);if(mode==5)RCHECK(candidates[0].pending&&candidates[0].retiring);}
        struct bw_native_backend *p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL)&&!pthread_mutex_init(&p->dispatch_lock,NULL));
        p->opaque=&context;p->ops.read_settings=oa_settings;p->ops.prepare=oa_prepare;p->ops.nonce=oa_nonce;p->ops.idle=oa_current;
        p->settings_enabled=p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->settings_generation=context.generation;p->settings_revision=2;p->generation=5;p->phase=BACKEND_READY;
        strcpy(p->server,context.config.server);strcpy(p->share,context.config.share);strcpy(p->setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        p->jobs=bw_native_jobs_create();p->library.catalog.items=calloc(1,sizeof *p->library.catalog.items);p->library.prepared=calloc(1,sizeof *p->library.prepared);
        RCHECK(p->jobs&&p->library.catalog.items&&p->library.prepared);p->library.catalog.count=p->library.catalog.capacity=1;
        struct bw_catalog_item *item=p->library.catalog.items;strcpy(item->source,(mode==3||mode==6)?"Fixture Alternative":"Fixture");strcpy(item->title_id,selected.title);item->state=BW_CATALOG_FOLDER;
        RCHECK(!bw_source_catalog_binding(&context.selected,&context.config,item));if(mode!=3&&mode!=6)RCHECK(!memcmp(&context.selected,&original->identity,sizeof context.selected));
        char body[2048];int n=snprintf(body,sizeof body,"{\"name\":\"%s\",\"sourceBinding\":\"%s\",\"libraryGeneration\":5,\"settingsRevision\":2}",item->source,context.selected.binding.game);RCHECK(n>0&&(size_t)n<sizeof body);
        backend_data_root=data;registration_environment=environment;
        unsigned char before[32],after[32];oa_snapshot(area,before);
        if(mode==9){oa_path(owner_admission_close_name,data,"registration-owners/.blackwire-registration-owners");owner_admission_close_arm=1;}
        struct bw_native_response response={0};const char *reservation="ffffffffffffffffffffffffffffffff";
        if(mode==9){RCHECK(!bw_native_backend_external_reserve(p,reservation,p->setup_token));
            RCHECK(!bw_native_backend_handle_reserved(p,reservation,"POST","/api/send",body,(size_t)n,p->setup_token,&response));}
        else RCHECK(!send_owned(p,NULL,body,(size_t)n,p->setup_token,&response));
        unsigned allowed=mode==0||mode==1||mode==2||mode==6;
        if(allowed){RCHECK(response.status==202&&p->joinable);RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;RCHECK(context.nonce_calls==1&&context.prepare_calls==1&&!p->active);}
        else{RCHECK(response.status==409&&!p->active&&!p->joinable&&!context.nonce_calls&&!context.prepare_calls&&!p->sending.token.owner);
            struct bw_native_job_view jobs={0};RCHECK(!bw_native_jobs_snapshot(p->jobs,&jobs)&&!jobs.count);RCHECK(strstr(response.bytes,"No startup files were sent"));}
        bw_native_response_clear(&response);
        if(mode==9){RCHECK(owner_admission_close_injected==1&&!owner_admission_close_arm&&p->control_release_unknown);
            RCHECK(!bw_native_backend_external_release(p,reservation)&&!*p->external_nonce&&p->control_release_unknown);owner_admission_reserved_release++;
            RCHECK(!send_owned(p,NULL,body,(size_t)n,p->setup_token,&response)&&response.status==409&&!context.nonce_calls&&!context.prepare_calls);bw_native_response_clear(&response);}
        else RCHECK(!p->control_release_unknown);
        oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));
        if(mode==7||mode==8){
            /* Correct only the deliberately injected host fault, then retry
             * this same resident. An ordinary refusal must not become a
             * permanent global registration-unknown cache. */
            if(mode==7){oa_path(name,data,"registration-owners/.blackwire-registration-owners");int fd=open(name,O_WRONLY|O_NOFOLLOW);RCHECK(fd>=0&&pwrite(fd,&marker_before,1,0)==1&&!fsync(fd)&&!close(fd));}
            else{oa_path(name,area,"linked-owner");RCHECK(!unlink(name));}
            oa_snapshot(area,before);RCHECK(!send_owned(p,NULL,body,(size_t)n,p->setup_token,&response)&&response.status==202&&p->joinable);
            RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;RCHECK(context.nonce_calls==1&&context.prepare_calls==1&&!p->active&&!p->control_release_unknown);
            bw_native_response_clear(&response);oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));owner_admission_retry_cases++;
        }
        RCHECK(!bw_native_jobs_destroy(p->jobs));bw_native_library_destroy(&p->library);RCHECK(!pthread_mutex_destroy(&p->dispatch_lock)&&!pthread_mutex_destroy(&p->lock));free(p);
        owner_admission_cases++;
    }
    backend_data_root=saved_data;registration_environment=saved_environment;
}

static unsigned setup_checks_done,setup_discovers,setup_saves;
static struct bw_native_backend *setup_checkpoint;
unsigned bw_test_uninstall_release_checks(void){return release_checks;}
static int release_settings(void *unused,struct bw_share_config *config,struct bw_setup_generation *generation){(void)unused;*config=release_config;*generation=release_generation;return BW_SETUP_VALID;}
void bw_test_uninstall_release_final_close(void){release_force_idle=1;}
void bw_test_uninstall_expect_pending(void){release_pending=1;}
static int release_idle(void *unused){(void)unused;return release_force_idle&&++release_idle_calls>1?-1:0;}
static int setup_nonce(void *unused,char nonce[33]){(void)unused;memset(nonce,'b',32);nonce[32]=0;return 0;}
static int setup_discover(void *opaque,struct bw_share_config *config,struct bw_native_library *out){
    struct bw_native_backend *p=opaque;RCHECK(config&&out);setup_discovers++;memset(out,0,sizeof *out);
    if(p==setup_checkpoint){RCHECK(!pthread_mutex_lock(&p->lock));p->control_release_unknown=1;RCHECK(!pthread_mutex_unlock(&p->lock));}return 0;
}
static int setup_save(void *unused,const struct bw_share_config *config,const struct bw_setup_generation *expected,const char *nonce,int (*guard)(void *),void *owner,struct bw_setup_saved *saved){
    (void)unused;RCHECK(guard&&!guard(owner)&&nonce_valid(nonce)&&bw_native_setup_generation_equal(expected,&release_generation));
    setup_saves++;release_config=*config;release_generation.sha256[0]++;*saved=(struct bw_setup_saved){.published=1,.verified=1};RCHECK(!guard(owner));return 0;
}
static void setup_fixture(struct bw_native_backend *p){
    if(!p->jobs){p->jobs=bw_native_jobs_create();RCHECK(p->jobs);}
    p->opaque=p;p->settings_enabled=p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->settings_generation=release_generation;
    p->ops.read_settings=release_settings;p->ops.save_settings=setup_save;p->ops.discover=setup_discover;p->ops.idle=release_idle;p->ops.nonce=setup_nonce;
    strcpy(p->server,release_config.server);strcpy(p->share,release_config.share);strcpy(p->share2,release_config.share2);
}
static void setup_editable(struct bw_native_backend *p,unsigned expected){
    struct bw_native_response response={0};RCHECK(!bw_native_backend_handle_authenticated(p,"GET","/api/state",NULL,0,NULL,&response)&&response.status==200);
    RCHECK(strstr(response.bytes,expected?"\"editable\":true":"\"editable\":false"));bw_native_response_clear(&response);
}
static int setup_post(struct bw_native_backend *p,const char *share){
    char body[512];int n=snprintf(body,sizeof body,"{\"server\":\"192.0.2.1\",\"share\":\"%s\",\"revision\":%u,\"keepCredentials\":true}",share,p->settings_revision);RCHECK(n>0&&(size_t)n<sizeof body);
    struct bw_native_response response={0};RCHECK(!bw_native_backend_handle_authenticated(p,"POST","/api/setup",body,(size_t)n,p->setup_token,&response));int status=response.status;bw_native_response_clear(&response);return status;
}
static int setup_late_guard(void *opaque,uint32_t operation){struct bw_native_backend *p=opaque;RCHECK(operation==p->setup_operation&&!pthread_mutex_lock(&p->lock));p->control_release_unknown=1;RCHECK(!pthread_mutex_unlock(&p->lock));return 0;}
static void setup_late_checks(void){
    /* A service callback returning success must not hide uncertainty it just
     * discovered. Use a separate owner; never clear an existing sticky flag. */
    struct bw_native_backend *p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL));
    p->active=1;p->worker_kind=WORK_SETUP;p->setup_operation=1;p->settings_idle=setup_late_guard;p->settings_idle_owner=p;
    RCHECK(settings_idle(p)&&p->control_release_unknown);RCHECK(!pthread_mutex_destroy(&p->lock));free(p);
    p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL)&&!pthread_mutex_init(&p->dispatch_lock,NULL));setup_fixture(p);
    p->phase=BACKEND_READY;p->settings_revision=2;strcpy(p->setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");setup_checkpoint=p;
    struct bw_share_config before=release_config;unsigned saves=setup_saves,discovers=setup_discovers;
    RCHECK(setup_post(p,"FixtureShare-Checkpoint")==202&&p->joinable);RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;
    RCHECK(setup_discovers==discovers+1&&setup_saves==saves&&config_equal(&before,&release_config)&&p->control_release_unknown&&!p->active);
    setup_editable(p,0);RCHECK(setup_post(p,"FixtureShare-Checkpoint")==409&&setup_saves==saves);setup_checkpoint=NULL;
    RCHECK(!bw_native_jobs_destroy(p->jobs));RCHECK(!pthread_mutex_destroy(&p->dispatch_lock)&&!pthread_mutex_destroy(&p->lock));free(p);
}
static void setup_after_removal(struct bw_native_backend *p,unsigned unknown){
    if(setup_checks_done)return;setup_checks_done=1;setup_fixture(p);setup_editable(p,!unknown);
    unsigned operation=p->setup_operation,saves=setup_saves,discovers=setup_discovers;
    int status=setup_post(p,"FixtureShare-Checked");
    if(unknown){RCHECK(status==409&&!p->active&&p->setup_operation==operation&&setup_saves==saves&&setup_discovers==discovers&&(p->control_release_unknown||p->removing.unknown));setup_editable(p,0);}
    else{RCHECK(status==202&&p->joinable);RCHECK(!pthread_join(p->worker,NULL));p->joinable=0;
        RCHECK(setup_discovers==discovers+1&&setup_saves==saves+1&&!p->active&&!p->control_release_unknown&&!strcmp(release_config.share,"FixtureShare-Checked"));setup_editable(p,1);setup_late_checks();}
    RCHECK(!bw_native_jobs_destroy(p->jobs));p->jobs=NULL;
}
static int release_normal(void *unused,const struct bw_native_removal_selection *selected,int (*guard)(void *),void *opaque,void (*observe)(void *,const struct bw_game_uninstall_progress *),void *observer,struct bw_game_uninstall_result *out){
    (void)unused;(void)observer;RCHECK(!memcmp(&selected->identity,&release_request->identity,sizeof selected->identity));release_calls++;
    struct bw_game_uninstall_request q=*release_request;q.ops=(struct bw_game_uninstall_ops){.opaque=opaque,.current=guard,.observe=observe};
    release_status=bw_native_game_uninstall_normal(&q,0,out);return release_status;
}
static void diagnostic_reason_checks(struct bw_native_backend *p){
    const char *original=p->removing.result.reason;char long_text[1024],split_utf8[196];
    memset(long_text,'A',sizeof long_text-1);long_text[sizeof long_text-1]=0;
    memset(split_utf8,'B',191);memcpy(split_utf8+191,"\xe2\x82\xac",4);
    const char *values[]={"quoted\"slash\\line\n",long_text,split_utf8};
    for(unsigned i=0;i<3;i++){
        p->removing.result.reason=values[i];struct bw_json json={.maximum=2048};removal_state_json(&json,p);
        RCHECK(!json.bad&&json.used<1200&&strstr(json.bytes,"\"diagnosticReason\":")&&p->removing.result.reason==values[i]);
        if(!i)RCHECK(strstr(json.bytes,"quoted\\\"slash\\\\line\\u000a")&&!strchr(json.bytes,'\n'));
        if(i==2)RCHECK(bw_json_utf8(json.bytes)&&strstr(json.bytes,"\xe2")==NULL);
        free(json.bytes);
    }p->removing.result.reason=original;
}
int bw_test_uninstall_reserved_worker(const struct bw_game_uninstall_request *q,struct bw_game_uninstall_result *out,unsigned expect_release){
    if(!release_initialized){
        release_initialized=1;RCHECK(!pthread_mutex_init(&release_owner.lock,NULL)&&!pthread_mutex_init(&release_owner.dispatch_lock,NULL));
        strcpy(release_config.server,"192.0.2.1");strcpy(release_config.share,"FixtureShare");release_generation.present=1;release_generation.file.st_mode=S_IFREG|0600;release_generation.file.st_nlink=1;memset(release_generation.sha256,0x31,32);
        release_owner.ops.read_settings=release_settings;release_owner.ops.idle=release_idle;release_owner.ops.uninstall=release_normal;
        release_owner.phase=BACKEND_READY;release_owner.settings_status=BW_SETUP_VALID;release_owner.settings_known=1;release_owner.settings_generation=release_generation;release_owner.settings_revision=2;release_owner.generation=5;
        strcpy(release_owner.setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    unsigned before=release_calls;
    if(release_owner.control_release_unknown||release_owner.removing.unknown){*out=release_owner.removing.result;RCHECK((expect_release||release_pending)&&out->release_unknown==expect_release&&out->phase==BW_GAME_REMOVE_UNKNOWN&&before==release_calls);return -1;}
    release_request=q;struct bw_native_backend *p=&release_owner;memset(&p->removing,0,sizeof p->removing);
    p->removing.selected.identity=q->identity;p->removing.selected.prepared=q->prepared;p->removing.selected.connection=release_generation;
    memcpy(p->removing.selected.operation,q->operation,33);memcpy(p->removing.selected.confirmed_title,q->confirmed_title,10);memcpy(p->external_nonce,q->operation,33);
    p->removing.state=REMOVAL_RUNNING;p->removing.generation=p->generation;p->removing.epoch=p->play_epoch;p->removing.revision=p->settings_revision;p->worker_kind=WORK_UNINSTALL;p->active=1;
    (void)removal_worker(p);*out=p->removing.result;
    if(release_calls!=before+1||p->active||!release_status)fprintf(stderr,"release chain before=%u after=%u active=%d status=%d phase=%u release=%u reason=%s\n",before,release_calls,p->active,release_status,(unsigned)out->phase,out->release_unknown,out->reason?out->reason:"none");
    RCHECK(release_calls==before+1&&!p->active&&release_status);
    RCHECK(out->mutation_started==release_pending&&!out->files_removed&&!out->source_absent&&!out->library_removed);
    RCHECK(out->release_unknown==expect_release&&p->control_release_unknown==(int)expect_release);
    RCHECK(p->removing.state==(expect_release||release_pending?REMOVAL_UNKNOWN:REMOVAL_ERROR));
    struct bw_json json={.maximum=2048};removal_state_json(&json,p);RCHECK(!json.bad&&strstr(json.bytes,expect_release?"\"releaseUnknown\":true":"\"releaseUnknown\":false"));free(json.bytes);
    diagnostic_reason_checks(p);
    /* Even a new queue reservation cannot restore control authority after an
     * uncertain release. An ordinary refusal retains that fresh authority. */
    RCHECK(!bw_native_backend_external_release(p,q->operation));
    const char *next="cccccccccccccccccccccccccccccccc";
    RCHECK(!bw_native_backend_external_reserve(p,next,p->setup_token));
    int current=bw_native_backend_external_current(p,next);RCHECK((current!=0)==(expect_release!=0));
    RCHECK(!bw_native_backend_external_release(p,next));
    setup_after_removal(p,expect_release||release_pending);
    return release_status;
}
