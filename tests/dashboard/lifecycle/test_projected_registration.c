/* Actual projected producer, execution source proof and ordinary ownership
 * intent/commit/offline retirement; public source/registration effects modeled. */
#define BW_PROJECTED_REMOVE_FIXTURE_ONLY 1
#include "test_projected_game_uninstall_e2e.c"
#include "native_registration_cleanup.h"
#include "native_registration_retirement.h"
/* This fixture admits FOLDER only. A container control branch is an error,
 * never a generic-success transport/witness substitute. Image-source control
 * qualification is deliberately outside this fixture's claims. */
int bw_container_smb_open_owned(struct bw_container_smb *p,struct smb2_context **owner,const char *name){(void)p;(void)owner;(void)name;abort();}
int bw_container_smb_open_owned_metadata(struct bw_container_smb *p,struct smb2_context **owner,const char *name){(void)p;(void)owner;(void)name;abort();}
int bw_container_smb_close(struct bw_container_smb *p){(void)p;abort();}
int __real_close(int);
static unsigned reg_close_armed,reg_after_mutation,reg_failed_close;
int __wrap_close(int fd){char proc[64],path[2048];int inject=0;snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);ssize_t n=readlink(proc,path,sizeof path-1);
    if(n>=0){path[n]=0;inject=reg_close_armed&&reg_after_mutation&&!reg_failed_close&&strstr(path,"/source.original.files")!=NULL;}
    int rc=__real_close(fd);if(inject){reg_failed_close++;errno=EIO;return -1;}return rc;}
struct reg_fixture {struct smb2_context *context;const char *mode;char game[1800],apps[1600],db[1600];unsigned manual,removes,uninstalls,foreign;};
static int reg_current(void *opaque){struct reg_fixture *f=opaque;return current(f->context);}
static int reg_sources(void *opaque,const struct bw_nrc_subject *subject){struct reg_fixture *f=opaque;C(subject&&!strcmp(subject->title,"PPSA99001")&&!strcmp(subject->source,f->game));return f->foreign?-1:0;}
static int reg_released(void *opaque,const struct bw_nrc_subject *subject){return reg_sources(opaque,subject);}
static int reg_manual(void *opaque,const struct bw_nrc_subject *subject){struct reg_fixture *f=opaque;C(!reg_sources(opaque,subject));return(int)f->manual;}
static int reg_remove(void *opaque,const struct bw_nrc_subject *subject){struct reg_fixture *f=opaque;C(!reg_sources(opaque,subject)&&++f->removes==1&&f->manual);f->manual=0;return 0;}
static void execute_sql(const char *path,const char *sql){sqlite3 *db=NULL;C(sqlite3_open(path,&db)==SQLITE_OK&&sqlite3_exec(db,sql,NULL,NULL,NULL)==SQLITE_OK&&sqlite3_close(db)==SQLITE_OK);}
static int reg_uninstall(void *opaque,const char title[10]){struct reg_fixture *f=opaque;C(!strcmp(title,"PPSA99001")&&!f->manual&&++f->uninstalls==1);char path[2000];
    snprintf(path,sizeof path,"%s/PPSA99001/sce_sys/param.json",f->apps);C(!unlink(path));snprintf(path,sizeof path,"%s/PPSA99001/sce_sys",f->apps);C(!rmdir(path));
    snprintf(path,sizeof path,"%s/PPSA99001/mount.lnk",f->apps);C(!unlink(path));snprintf(path,sizeof path,"%s/PPSA99001",f->apps);C(!rmdir(path));execute_sql(f->db,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA99001'");
    reg_after_mutation=1;
    return !strcmp(f->mode,"lost-uninstall")?-1:0;}
int main(int argc,char **argv){C(argc==6&&!strcmp(argv[1],"folder"));const char *mode=argv[5];
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");struct bw_provider_binding identity;
    C(!bw_provider_binding_init(&identity,&config,"fixture","PPSA99001",BW_PROVIDER_FOLDER));
    char data[1600],log[1600],state[1800],path[2000];struct reg_fixture f={.context=&context,.mode=mode,.manual=1};
    snprintf(data,sizeof data,"%s/data",argv[4]);C(!mkdir(data,0755));snprintf(f.apps,sizeof f.apps,"%s/apps",argv[4]);C(!mkdir(f.apps,0770));snprintf(f.db,sizeof f.db,"%s/app.db",argv[4]);empty_db(f.db);
    snprintf(log,sizeof log,"%s/game.log",argv[4]);file_write(log,"",0);struct bw_source_provider provider={0};
    C(!bw_source_provider_prepare_policy(&provider,&identity,&source,NULL,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    struct bw_native_send_files send={.data_root=data,.game_log=log,.provider=&provider,.current=current,.opaque=&context};struct bw_native_prepared prepared={0};
    C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.prepared_policy==3);bw_source_provider_destroy(&provider);C(!context.open);
    snprintf(f.game,sizeof f.game,"%s/games/%s",data,identity.binding.game);snprintf(state,sizeof state,"%s/native-prepared/%s",data,identity.binding.game);
    struct bw_native_launch_admission *startup=NULL;C(!bw_native_launch_admission_open(&startup,data,&identity,&prepared,"cccccccccccccccccccccccccccccccc",current,&context,NULL));struct bw_reader_selection selected;C(!bw_native_launch_admission_selection(startup,&selected)&&selected.prepared_policy==3);
    if(strcmp(mode,"no-source")){uint8_t map[32],generation[32];const char *why=NULL;int checked=bw_native_launch_admission_source(startup,&config,&source,&selected,map,generation,&why);if(checked)fprintf(stderr,"source proof: %s\n",why?why:"");C(!checked);}
    if(!strcmp(mode,"control-changed"))context.canceled=1;
    if(!strcmp(mode,"receipt-before-intent")){snprintf(path,sizeof path,"%s/blackwire.companions",f.game);mutate(path);}
    struct bw_nrc_environment env={data,f.apps,f.db};struct bw_nrc_ops ops={.opaque=&f,.current=reg_current,.sources=reg_sources,.uninstall=reg_uninstall};struct bw_nrc_ticket *ticket=NULL;struct bw_nrc_result result={0};
    int begun=!strcmp(mode,"no-startup")?bw_nrc_begin(&env,&selected,&ops,&ticket,&result):bw_nrc_begin_projected(&env,&selected,startup,&ops,&ticket,&result);
    if(!strcmp(mode,"no-source")||!strcmp(mode,"no-startup")||!strcmp(mode,"control-changed")||!strcmp(mode,"receipt-before-intent")){C(begun<0&&!ticket&&!f.removes&&!f.uninstalls);goto done;}
    if(begun)fprintf(stderr,"intent failed stage%s reason%s\n",result.stage?result.stage:"",result.reason?result.reason:"");C(!begun&&ticket&&result.state==BW_NRC_INTENT);
    static const char param[]="{\"titleId\":\"PPSA99001\",\"contentId\":\"UP0000-PPSA99001_00-BLACKWIREFIXTURE0\"}";
    snprintf(path,sizeof path,"%s/PPSA99001",f.apps);C(!mkdir(path,0755));snprintf(path,sizeof path,"%s/PPSA99001/sce_sys",f.apps);C(!mkdir(path,0755));snprintf(path,sizeof path,"%s/PPSA99001/sce_sys/param.json",f.apps);file_write(path,param,sizeof param-1);
    snprintf(path,sizeof path,"%s/PPSA99001/mount.lnk",f.apps);file_write(path,f.game,strlen(f.game));execute_sql(f.db,"INSERT INTO tbl_contentinfo VALUES('PPSA99001','UP0000-PPSA99001_00-BLACKWIREFIXTURE0')");
    if(!strcmp(mode,"receipt-before-commit")){snprintf(path,sizeof path,"%s/blackwire.companions",f.game);mutate(path);}
    struct bw_register_result registered={.add_attempted=1,.add_confirmed=1,.registered=1};int committed=bw_nrc_finish(ticket,0,&registered,&result);C(!bw_nrc_ticket_destroy(ticket));ticket=NULL;
    if(!strcmp(mode,"receipt-before-commit")){C(committed<0&&!f.uninstalls);goto done;}
    if(committed)fprintf(stderr,"commit failed stage%s reason%s\n",result.stage?result.stage:"",result.reason?result.reason:"");C(!committed&&result.state==BW_NRC_OWNED);
    C(!bw_native_launch_admission_close(&startup)&&!startup);unsigned before=context.requests;
    if(!strcmp(mode,"receipt-before-retire")){snprintf(path,sizeof path,"%s/blackwire.companions",f.game);mutate(path);}
    if(!strcmp(mode,"retire-foreign"))f.foreign=1;
    reg_close_armed=!strcmp(mode,"retire-close-unknown");
    struct bw_nrc_retirement_ops retire={.registration=ops,.released=reg_released,.outside_scan_roots=reg_released,.manual_present=reg_manual,.manual_remove=reg_remove};struct bw_nrc_retirement_result retired={0};
    int rc=bw_nrc_retire_present(&env,selected.title,selected.run,&retire,&retired);
    if(!strcmp(mode,"retire-close-unknown"))C(rc==BW_NRC_RELEASE_UNCONFIRMED&&reg_failed_close==1&&f.removes==1&&f.uninstalls==1);
    else if(!strcmp(mode,"receipt-before-retire")||!strcmp(mode,"retire-foreign"))C(rc<0&&!f.removes&&!f.uninstalls);
    else{if(rc||retired.registration.state!=BW_NRC_PENDING)fprintf(stderr,"retire rc%d state%d removes%u uninstalls%u stage%s reason%s\n",rc,retired.registration.state,f.removes,f.uninstalls,retired.registration.stage?retired.registration.stage:"",retired.registration.reason?retired.registration.reason:"");C(!rc&&retired.registration.state==BW_NRC_PENDING&&f.removes==1&&f.uninstalls==1);C(!bw_nrc_retire_present(&env,selected.title,selected.run,&retire,&retired)&&retired.registration.state==BW_NRC_ABSENT&&f.removes==1&&f.uninstalls==1);C(!bw_nrc_retire_present(&env,selected.title,selected.run,&retire,&retired)&&retired.registration.state==BW_NRC_ABSENT&&f.removes==1&&f.uninstalls==1);}
    C(context.requests==before&&!context.open);
done:
    if(ticket)C(!bw_nrc_ticket_destroy(ticket));C(!bw_native_launch_admission_close(&startup)&&!startup);C(!context.open);
    printf("{\"passed\":true,\"checks\":%u,\"actualProviderAndSend\":true,\"actualExecutionSourceProof\":true,\"actualRegistrationLedger\":true,\"normalRegistrationEffectsModeled\":true,\"offlineRetirementUsesNoSource\":true,\"mode\":\"%s\",\"consoleActions\":0}\n",checks,mode);return 0;
}
