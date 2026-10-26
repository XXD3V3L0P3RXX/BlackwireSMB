/* Actual strict Send + sealed admission producer + production source_connect.
 * Only SMB transport is replaced, with exact argument/ownership assertions. */
#define main retained_fixture_setup_main
#include "test_fast_ready_play.c"
#undef main

enum {SMB_OK,SMB_INIT_FAIL,SMB_CONNECT_FAIL,SMB_DISCONNECT_FAIL,SMB_CANCEL_AFTER_CONNECT};
static int smb_fault;
static unsigned smb_inits,smb_connects,smb_disconnects,smb_destroys;
static int observed_timeout;
static char observed_share[128];
static const char account[]="project-fixture-account",password[]="PRIVATE-CONNECTOR-TEST-SECRET";
struct smb2_context {unsigned owned;};
static struct smb2_context *owned_context;
struct smb2_context *smb2_init_context(void){
    smb_inits++;CHECK(!owned_context);if(smb_fault==SMB_INIT_FAIL)return NULL;
    owned_context=calloc(1,sizeof *owned_context);CHECK(owned_context);owned_context->owned=1;return owned_context;
}
void smb2_destroy_context(struct smb2_context *s){CHECK(s&&s==owned_context&&s->owned);smb_destroys++;s->owned=0;free(s);owned_context=NULL;}
void smb2_set_timeout(struct smb2_context *s,int timeout){CHECK(s==owned_context&&timeout>=1&&timeout<=10);observed_timeout=timeout;}
void smb2_set_user(struct smb2_context *s,const char *value){CHECK(s==owned_context&&!strcmp(value,account));}
void smb2_set_password(struct smb2_context *s,const char *value){CHECK(s==owned_context&&!strcmp(value,password));}
void smb2_set_sign(struct smb2_context *s,int sign){CHECK(s==owned_context&&sign==1);}
void smb2_set_security_mode(struct smb2_context *s,uint16_t mode){CHECK(s==owned_context&&mode==SMB2_NEGOTIATE_SIGNING_REQUIRED);}
int smb2_connect_share(struct smb2_context *s,const char *server,const char *share,const char *user){
    CHECK(s==owned_context&&!strcmp(server,"192.0.2.1")&&!strcmp(share,"FixtureShare")&&!strcmp(user,account));
    smb_connects++;strcpy(observed_share,share);
    if(smb_fault==SMB_CANCEL_AFTER_CONNECT)cancel_at=guard_calls+1;
    return smb_fault==SMB_CONNECT_FAIL?-EACCES:0;
}
int smb2_disconnect_share(struct smb2_context *s){CHECK(s==owned_context);smb_disconnects++;return smb_fault==SMB_DISCONNECT_FAIL?-EIO:0;}
static unsigned fd_count(void){
    unsigned count=0;DIR *d=opendir("/proc/self/fd");CHECK(d);struct dirent *row;
    while((row=readdir(d)))if(strcmp(row->d_name,".")&&strcmp(row->d_name,".."))count++;
    CHECK(!closedir(d));return count;
}
static void reset_transport(int fault){CHECK(!owned_context);smb_fault=fault;smb_inits=smb_connects=smb_disconnects=smb_destroys=0;observed_timeout=0;observed_share[0]=0;cancel_at=0;}
static struct bw_share_config configured(int secondary){
    struct bw_share_config c={0};strcpy(c.server,"192.0.2.1");strcpy(c.share,secondary?"DeletedTestShare":"FixtureShare");
    strcpy(c.share2,secondary?"FixtureShare":"OtherShare");strcpy(c.user,account);strcpy(c.password,password);return c;
}
int main(int argc,char **argv){
    CHECK(argc==4);CHECK(!retained_fixture_setup_main(argc,argv));scenario="source-connect-wrapper";
    char data[2048];path(data,argv[1],"data");
    struct bw_share_config first=configured(0);struct bw_provider_binding identity;
    CHECK(!bw_provider_binding_init(&identity,&first,"Fixture","PPSA99999",BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};provider.identity=identity;provider.bundle.ready=1;
    struct bw_native_prepared prepared={0};CHECK(!bw_native_send_verify(data,&identity,current,&provider,&prepared)&&prepared.state==BW_PREPARED_VALID);
    unsigned wrapper_cases=0,start_checks=checks,baseline=fd_count(),starting_posts=transport_posts;
    enum {PRIMARY,SECONDARY,INIT_FAIL,CONNECT_FAIL,DISCONNECT_FAIL,CANCEL_CONNECT,WRONG_SHARE,WRONG_SELECTION,EXPIRED,NONFINITE,TOO_LATE,SHORT_DEADLINE,SOURCE_CHANGED,RECHECK_FAIL,SOURCE_AND_DISCONNECT};
    const char *labels[]={"primary","secondary","init-failure","connect-failure","disconnect-failure","cancel-after-connect","wrong-share","wrong-selection","expired-deadline","nonfinite-deadline","overlong-deadline","short-deadline","changed-source","retained-witness-recheck","changed-source-and-disconnect"};
    for(int test=PRIMARY;test<=SOURCE_AND_DISCONNECT;test++){
        scenario=labels[test];
        wrapper_cases++;struct bw_native_launch_admission *admission=NULL;const char *error=NULL;
        CHECK(!bw_native_launch_admission_open(&admission,data,&identity,&prepared,"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",current,&provider,&error));
        struct bw_reader_selection selected={0};CHECK(!bw_native_launch_admission_selection(admission,&selected));
        struct bw_share_config config=configured(test==SECONDARY),before=config;struct bw_reader_selection altered=selected;
        int fault=test==INIT_FAIL?SMB_INIT_FAIL:test==CONNECT_FAIL?SMB_CONNECT_FAIL:test==DISCONNECT_FAIL||test==SOURCE_AND_DISCONNECT?SMB_DISCONNECT_FAIL:test==CANCEL_CONNECT?SMB_CANCEL_AFTER_CONNECT:SMB_OK;
        reset_transport(fault);unsigned lookups=remote_lookups;
        if(test==WRONG_SHARE){strcpy(config.share,"UnselectedShare");strcpy(config.share2,"AnotherShare");before=config;}
        if(test==WRONG_SELECTION)strcpy(altered.title,"PPSA99998");
        double deadline=cp_now()+20;
        if(test==EXPIRED)deadline=cp_now()-1;
        if(test==NONFINITE)deadline=__builtin_nan("");
        if(test==TOO_LATE)deadline=admission->deadline+1;
        if(test==SHORT_DEADLINE)deadline=cp_now()+1.8;
        char changed[2048];struct stat source_before;
        if(test==SOURCE_CHANGED||test==SOURCE_AND_DISCONNECT){path(changed,source_root,"sce_module/libfixture.sprx");CHECK(!stat(changed,&source_before));CHECK(!truncate(changed,source_before.st_size-1));}
        int result=bw_native_launch_admission_source_connect(admission,&config,&altered,deadline,&error);
        CHECK(!memcmp(&config,&before,sizeof config));CHECK(!owned_context);
        CHECK(!error||(!strstr(error,password)&&!strstr(error,account)));
        const struct bw_preload_source_ampr *w=NULL;
        if(test==PRIMARY||test==SECONDARY||test==SHORT_DEADLINE||test==RECHECK_FAIL){
            CHECK(!result&&!error&&smb_inits==1&&smb_connects==1&&smb_disconnects==1&&smb_destroys==1&&!strcmp(observed_share,"FixtureShare"));
            CHECK(remote_lookups>lookups&&admission->source_checked);
            CHECK(!bw_native_launch_admission_source_ampr(admission,&w)&&w&&w->present&&w->purpose==BW_SOURCE_AMPR_EXECUTION);
            CHECK(!bw_preload_source_ampr_current(w,admission->anchors[3].root));
            CHECK(test==SHORT_DEADLINE?observed_timeout>=1&&observed_timeout<=2:observed_timeout==10);
            if(test==RECHECK_FAIL){
                reset_transport(SMB_DISCONNECT_FAIL);
                CHECK(bw_native_launch_admission_source_connect(admission,&config,&selected,cp_now()+20,&error)<0);
                CHECK(admission->failed&&!owned_context&&smb_disconnects==1&&smb_destroys==1);
                CHECK(bw_preload_source_ampr_current(w,admission->anchors[3].root)<0);
                w=(const struct bw_preload_source_ampr *)(uintptr_t)1;
                CHECK(bw_native_launch_admission_source_ampr(admission,&w)<0&&!w);
            }
        }else{
            CHECK(result<0&&error);CHECK(bw_native_launch_admission_source_ampr(admission,&w)<0&&!w);
            if(test==WRONG_SHARE||test==WRONG_SELECTION||test==EXPIRED||test==NONFINITE||test==TOO_LATE)CHECK(!smb_inits&&!smb_connects&&remote_lookups==lookups);
            else if(test==INIT_FAIL)CHECK(smb_inits==1&&!smb_connects&&!smb_disconnects&&!smb_destroys&&remote_lookups==lookups);
            else CHECK(smb_inits==1&&smb_connects==1&&smb_disconnects==1&&smb_destroys==1&&admission->failed);
            if(test==DISCONNECT_FAIL)CHECK(admission->source_checked&&admission->failed);
            if(test==SOURCE_AND_DISCONNECT)CHECK(strstr(error,"selected PC source")&&!strstr(error,"positively released"));
            if(test==CONNECT_FAIL||test==CANCEL_CONNECT)CHECK(remote_lookups==lookups);
        }
        cancel_at=0;
        if(test==SOURCE_CHANGED||test==SOURCE_AND_DISCONNECT){CHECK(!truncate(changed,source_before.st_size));struct timespec times[2]={source_before.st_atim,source_before.st_mtim};CHECK(!utimensat(AT_FDCWD,changed,times,0));
            /* Restore the exact synthetic body, not merely its size/time. */
            uint8_t body[sizeof(uint8_t)*256*2048];for(size_t i=0;i<sizeof body;i++)body[i]=(uint8_t)(255-(i%256));
            file_write(changed,body,sizeof body,source_before.st_mode&07777);CHECK(!utimensat(AT_FDCWD,changed,times,0));}
        CHECK(!bw_native_launch_admission_close(&admission)&&!admission);
        CHECK(fd_count()==baseline&&transport_posts==starting_posts);
    }
    wrapper_cases++;scenario="removal-purpose-denied";reset_transport(SMB_OK);
    struct bw_native_launch_admission *removal=NULL;const char *removal_error=NULL;struct bw_native_prepared legacy=prepared;legacy.startup_upgrade_required=1;
    CHECK(!bw_native_launch_admission_open_for_removal(&removal,data,&identity,&legacy,"ffffffffffffffffffffffffffffffff",current,&provider,&removal_error));
    struct bw_share_config removal_config=configured(1),removal_before=removal_config;struct bw_reader_selection removal_selection=removal->selected;
    CHECK(bw_native_launch_admission_source_connect(removal,&removal_config,&removal_selection,cp_now()+20,&removal_error)<0&&removal_error);
    CHECK(!smb_inits&&!smb_connects&&!memcmp(&removal_config,&removal_before,sizeof removal_config));
    const struct bw_preload_source_ampr *forbidden=NULL;CHECK(bw_native_launch_admission_source_ampr(removal,&forbidden)<0&&!forbidden);
    CHECK(!bw_native_launch_admission_close(&removal)&&!removal&&fd_count()==baseline&&transport_posts==starting_posts);
    printf("{\"passed\":true,\"wrapperCases\":%u,\"wrapperChecks\":%u,\"actualStrictSendAndAdmission\":true,\"actualSourceConnectWrapper\":true,\"exactSecondaryShareSelected\":true,\"originalCredentialsPreserved\":true,\"disconnectFailurePoisonsWitness\":true,\"mockedSmbTransport\":true,\"consoleActions\":0}\n",wrapper_cases,checks-start_checks);
    return 0;
}

