/* Actual readiness and bundle filesystem checks. Receipt/source/registration
 * adapters are explicit counters; actual admission production is tested by
 * the fast-ready and source-connect suites. No console/network actions. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#define BW_HOME_STARTUP 1
#define BW_PRELOAD_BUNDLE_HOST_TEST 1
#define bw_preload_bundle_verify_checked actual_legacy_verify
#define bw_preload_bundle_verify_source_checked actual_source_verify
#include "preload_bundle.c"
#undef bw_preload_bundle_verify_checked
#undef bw_preload_bundle_verify_source_checked
static unsigned legacy_checks,source_checks;
int bw_preload_bundle_verify_checked(const struct bw_preload_bundle_request *q,struct bw_preload_bundle_result *d){legacy_checks++;return actual_legacy_verify(q,d);}
int bw_preload_bundle_verify_source_checked(const struct bw_preload_bundle_request *q,const struct bw_preload_source_ampr *w,struct bw_preload_bundle_result *d){source_checks++;return actual_source_verify(q,w,d);}
#include "native_backend.c"
static unsigned checks,cases,opens,closes,connections,selections,manuals;
static int fault,source_checked,close_fault;
static struct bw_native_backend owner;
static struct bw_native_prepared prepared;
static struct bw_provider_binding identity;
static struct bw_reader_selection selected;
static struct bw_setup_generation settings_generation;
static struct bw_preload_source_ampr witness;
static char data[1024],root[1152];
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"readiness case%u line%u: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
int __real_close(int);
int __wrap_close(int fd){char link[64],name[1400];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,name,sizeof name-1);if(n>=0)name[n]=0;
    int fail=close_fault&&n>=0&&strstr(name,"/fakelib2/libSceAmpr.sprx");int result=__real_close(fd);if(fail){close_fault=0;errno=EIO;return -1;}return result;}
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
static int settings_read(void *p,struct bw_share_config *out,struct bw_setup_generation *g){C(p==&owner);memset(out,0,sizeof *out);*g=settings_generation;return BW_SETUP_VALID;}
int bw_native_launch_selection(const char *base,const struct bw_provider_binding *id,const struct bw_native_prepared *p,const char *run,struct bw_reader_selection *out){
    C(base==backend_data_root&&id==&identity&&p==&prepared&&!strcmp(run,prepared.run)&&out);selections++;*out=selected;return 0;}
int bw_native_launch_admission_open(struct bw_native_launch_admission **out,const char *base,const struct bw_provider_binding *id,const struct bw_native_prepared *p,const char *run,int (*current)(void *),void *opaque,const char **error){
    C(out&&!*out&&!strcmp(base,data)&&id==&identity&&p==&prepared&&!strcmp(run,prepared.run)&&current&&!current(opaque));opens++;source_checked=0;
    if(fault==2||fault==3){if(error)*error="Startup admission refused.";return fault==3?-2:-1;}
    *out=(struct bw_native_launch_admission *)(uintptr_t)1;return 0;}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *out){C(p&&out);*out=selected;return 0;}
int bw_native_launch_admission_source_connect(struct bw_native_launch_admission *p,const struct bw_share_config *c,const struct bw_reader_selection *s,double deadline,const char **error){
    C(p&&c&&s&&!memcmp(s,&selected,sizeof *s)&&deadline>now());(void)error;connections++;if(fault==1)return -1;source_checked=1;return 0;}
int bw_native_launch_admission_source_ampr(struct bw_native_launch_admission *p,const struct bw_preload_source_ampr **out){C(p&&out&&source_checked);*out=&witness;return 0;}
/* This fixture intentionally has no positive policy3 source. */
int bw_native_launch_admission_companions(struct bw_native_launch_admission *p,const struct bw_preload_companions **out){(void)p;(void)out;abort();}
int bw_native_launch_admission_close(struct bw_native_launch_admission **p){C(p);if(*p){closes++;*p=NULL;}return 0;}
enum bw_registration_state bw_native_registration_state(const char *base,const char *apps,const char *database,const struct bw_reader_selection *s){C(base&&apps&&database&&s&&!memcmp(s,&selected,sizeof *s));return BW_REG_MATCH;}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation op,const char *path,struct bw_sm_reply *out){C(c&&c->current&&!c->current(c->opaque)&&op==BW_SM_MANUAL_LIST&&!strcmp(path,root)&&out);manuals++;memset(out,0,sizeof *out);out->present=1;out->count=8;return 0;}
static int current(void *p){C(p==&owner);return 0;}
static int witness_current(void *p){return p==&owner&&source_checked?0:-1;}
static void file_write(const char *name,const void *bytes,size_t size){int fd=open(name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0755);C(fd>=0&&write(fd,bytes,size)==(ssize_t)size&&!close(fd));}
static void directory(const char *name){C(!mkdir(name,0755));}
static void reset(const char *base,int ampr){
    memset(&owner,0,sizeof owner);C(!pthread_mutex_init(&owner.lock,NULL));owner.active=1;owner.worker_kind=WORK_SCAN;owner.generation=5;owner.settings_revision=3;owner.ops.read_settings=settings_read;owner.opaque=&owner;
    memset(&prepared,0,sizeof prepared);prepared.state=BW_PREPARED_VALID;strcpy(prepared.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(prepared.game,sizeof prepared.game,"PPSA99999_%064u",0);memset(&identity,0,sizeof identity);identity.kind=BW_PROVIDER_FOLDER;identity.policy=BW_PROVIDER_POLICY_V1;
    strcpy(identity.binding.game,prepared.game);strcpy(identity.binding.title,"PPSA99999");
    memset(&selected,0,sizeof selected);strcpy(selected.game,prepared.game);strcpy(selected.title,"PPSA99999");strcpy(selected.run,prepared.run);
    memset(&settings_generation,0,sizeof settings_generation);
    snprintf(data,sizeof data,"%s/case-%02u",base,++cases);directory(data);char name[1300];snprintf(name,sizeof name,"%s/games",data);directory(name);snprintf(root,sizeof root,"%s/games/%s",data,prepared.game);directory(root);
    snprintf(name,sizeof name,"%s/native-prepared",data);directory(name);snprintf(name,sizeof name,"%s/native-prepared/%s",data,prepared.game);directory(name);backend_data_root=data;
    memset(&witness,0,sizeof witness);strcpy(witness.root,root);witness.present=(unsigned)ampr;witness.purpose=BW_SOURCE_AMPR_EXECUTION;witness.current_owner=witness_current;witness.owner=&owner;
    if(ampr){const char original[]="sealed original Ampr fixture, never the destination reader";witness.size=sizeof original;cp_sha_bytes(original,sizeof original,witness.sha256);snprintf(name,sizeof name,"%s/fakelib",root);directory(name);snprintf(name,sizeof name,"%s/fakelib/libSceAmpr.sprx",root);file_write(name,original,sizeof original);}
    source_checked=1;struct bw_preload_bundle_request q={root,current,&owner,now()+20};struct bw_preload_bundle_result d={0};C(!bw_preload_bundle_install_source_checked(&q,&witness,&d));
    opens=closes=connections=selections=manuals=legacy_checks=source_checks=0;fault=source_checked=close_fault=0;
}
static void finish(void){C(!pthread_mutex_destroy(&owner.lock));}
int main(int argc,char **argv){C(argc==2);
    reset(argv[1],0);C(!home_ready_observe(&owner,&identity,&prepared,&settings_generation));C(legacy_checks==1&&!source_checks&&!opens&&!connections&&!closes&&selections==2&&manuals==2);finish();
    reset(argv[1],1);C(!home_ready_observe(&owner,&identity,&prepared,&settings_generation));C(legacy_checks==1&&source_checks==1&&opens==1&&connections==1&&closes==1&&manuals==2);finish();
    reset(argv[1],1);fault=1;C(home_ready_observe(&owner,&identity,&prepared,&settings_generation)<0);C(legacy_checks==1&&!source_checks&&opens==1&&connections==1&&closes==1&&!manuals);finish();
    for(int mode=2;mode<=3;mode++){
        reset(argv[1],1);fault=mode;C(home_ready_observe(&owner,&identity,&prepared,&settings_generation)<0);
        C(legacy_checks==1&&!source_checks&&opens==1&&!connections&&!closes&&!manuals&&owner.control_release_unknown==(mode==3));finish();
    }
    reset(argv[1],0);char name[1300];snprintf(name,sizeof name,"%s/fakelib2/libSceAmpr.sprx",root);int fd=open(name,O_RDWR|O_NOFOLLOW);uint8_t b;C(fd>=0&&pread(fd,&b,1,0)==1);b^=1;C(pwrite(fd,&b,1,0)==1&&!close(fd));
    C(home_ready_observe(&owner,&identity,&prepared,&settings_generation)<0);C(legacy_checks==1&&!source_checks&&!opens&&!connections&&!manuals);finish();
    reset(argv[1],0);close_fault=1;C(home_ready_observe(&owner,&identity,&prepared,&settings_generation)<0);C(legacy_checks==1&&!source_checks&&!opens&&!connections&&!manuals&&owner.control_release_unknown);finish();
    reset(argv[1],0);snprintf(name,sizeof name,"%s/fakelib",root);directory(name);snprintf(name,sizeof name,"%s/fakelib/unknown.sprx",root);file_write(name,"unqualified",11);
    C(home_ready_observe(&owner,&identity,&prepared,&settings_generation)<0);C(legacy_checks==1&&!source_checks&&!opens&&!connections&&!manuals);finish();
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualReadinessAndBundle\":true,\"ordinaryReadinessSMBConnections\":0,\"sourceAmprBranchConnections\":1,\"otherRefusalsDoNotRetry\":true}\n",cases,checks);return 0;
}
