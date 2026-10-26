/* Actual Send completion/provision integration; filesystem receipt, bundled
 * module and Sony registration transports are explicit recording models. */
#define _DEFAULT_SOURCE 1
#include "send_diagnostic.h"
#include "provision-under-test.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned cases,checks,copies,bundles,begins,registers,commits,destroys,startup_closes,job_success;
static int fault,pending_registration;
static struct bw_native_backend owner;static struct bw_native_prepared prepared;
static struct bw_catalog_item item;static struct bw_native_prepared library_prepared;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"case%u line%u %s\n",cases,__LINE__,#x);exit(1);}}while(0)
#define BW_AMPR_MODEL_CHECK C
#include "../dashboard/lifecycle/source_ampr_admission_model.h"
static int prepare_current(void *v){C(v==&owner);return 0;}
static int prepare_authority(struct bw_native_backend *p,enum bw_prepare_guard_point point){C(p==&owner);C(point==BW_PREPARE_CONTROL||point==BW_PREPARE_RELEASE);return 0;}
static int nonce(void *v,char out[33]){C(v==&owner);memset(out,'c',32);out[32]=0;return 0;}
static int prepare(void *v,struct bw_share_config *c,const struct bw_native_prepare_selection *s,int(*current)(void*),void*g,bw_source_observer_fn o,void*obs,struct bw_native_prepared*out){
    C(v==&owner&&c&&s==&owner.sending.selection&&current==prepare_current&&g==&owner&&o&&obs==&owner.sending.token);copies++;*out=prepared;return 0;
}
void bw_native_sensitive_clear(void *p,size_t n){memset(p,0,n);}
void bw_native_job_observe(void*a,const struct bw_source_progress*b){(void)a;(void)b;}
int bw_native_job_finish(const struct bw_native_job_token*t,enum bw_native_job_failure f){C(t==&owner.sending.token&&f==BW_JOB_SUCCESS);job_success++;return 0;}
int bw_native_job_setup_message(const struct bw_native_job_token*t,const char*m){C(t==&owner.sending.token&&m&&strstr(m,"Startup files kept"));return 0;}
int bw_native_job_reuse_prepared(const struct bw_native_job_token*t,uint32_t count,uint64_t bytes){C(t==&owner.sending.token&&count==prepared.local_count&&bytes==prepared.local_bytes);return 0;}
int bw_native_job_finish_setup(const struct bw_native_job_token*t,int valid,int registered,const char*m){C(t==&owner.sending.token&&valid&&registered==(fault==0)&&m&&m[0]);job_success++;return 0;}
int bw_native_launch_admission_open(struct bw_native_launch_admission **out,const char*root,const struct bw_provider_binding*identity,const struct bw_native_prepared*expected,const char*run,int(*guard)(void*),void*opaque,const char**error){
    C(!strcmp(root,backend_data_root)&&identity&&expected->state==BW_PREPARED_VALID&&strcmp(run,expected->run)&&guard&&!guard(opaque));
    if(fault==7||fault==8){C(!*out);if(error)*error="Startup admission refused.";return fault==8?-2:-1;}
    *out=(struct bw_native_launch_admission*)malloc(1);C(*out);ampr_model_open(*out);return 0;
}
int bw_native_launch_admission_selection(struct bw_native_launch_admission*p,struct bw_reader_selection*out){C(p);memset(out,0,sizeof *out);strcpy(out->game,prepared.game);strcpy(out->title,"PPSA04734");strcpy(out->run,"cccccccccccccccccccccccccccccccc");return 0;}
int bw_native_launch_admission_current(struct bw_native_launch_admission*p,const char*root,const struct bw_provider_binding*identity,const struct bw_reader_selection*s,struct bw_native_prepared*out){C(p&&!strcmp(root,backend_data_root)&&identity&&s&&!out&&!strcmp(s->game,prepared.game));return 0;}
int bw_native_launch_admission_close(struct bw_native_launch_admission **p){if(*p){startup_closes++;free(*p);*p=NULL;}return 0;}
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request*q,const struct bw_preload_bundle_control*c,struct bw_preload_bundle_result*out){
    C(q&&c&&!begins&&!registers&&!q->bundle.current(q->bundle.opaque)&&!c->checkpoint(c->opaque));C(!strcmp(q->game,prepared.game)&&!strcmp(q->prepared_run,prepared.run)&&strcmp(q->launch_run,q->prepared_run)&&q->prepared_record_sha256);bundles++;if(fault==1||fault==2){out->release_unknown=fault==2;return -1;}return 0;
}
int bw_nrl_begin(const struct bw_nrl_request*r,struct bw_nrl_owner **out,struct bw_nrc_result*result){C(bundles==1&&!registers&&!r->current(r->opaque,BW_NRL_REGISTER));(void)result;begins++;if(fault==3)return -1;*out=(struct bw_nrl_owner*)malloc(1);C(*out);return 0;}
int bw_native_register(const struct bw_native_register_request*r,struct bw_register_result*out){C(bundles==1&&begins==1&&!registers&&r->startup&&!r->current(r->guard));registers++;out->registered=fault!=4;return fault==4?-1:0;}
int bw_nrl_registered(struct bw_nrl_owner*p,int registered,const struct bw_register_result*checked,struct bw_nrc_result*out){C(p&&registers==1&&checked);(void)out;commits++;if(fault==5)return BW_NRC_RELEASE_UNCONFIRMED;return registered?-1:0;}
int bw_nrl_destroy(struct bw_nrl_owner **p){if(*p){destroys++;free(*p);*p=NULL;}return fault==6?-1:0;}
static int registration_pending_refresh(struct bw_native_backend*p){C(p==&owner);if(pending_registration){p->registration_pending_count=1;strcpy(p->registration_pending[0].title,"PPSA04734");}return 0;}
static void reset(int mode){
    cases++;memset(&owner,0,sizeof owner);memset(&prepared,0,sizeof prepared);memset(&item,0,sizeof item);memset(&library_prepared,0,sizeof library_prepared);C(!pthread_mutex_init(&owner.lock,NULL));
    fault=mode;pending_registration=mode==4||mode==5;copies=bundles=begins=registers=commits=destroys=startup_closes=job_success=0;
    owner.active=1;owner.worker_kind=WORK_PREPARE;owner.generation=4;owner.settings_revision=3;owner.opaque=&owner;owner.ops.prepare=prepare;owner.ops.nonce=nonce;
    owner.library.catalog.items=&item;owner.library.catalog.count=1;strcpy(item.title_id,"PPSA04734");owner.library.prepared=&library_prepared;
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.20");strcpy(config.share,"OwnedBackups");owner.sending.config=config;
    C(!bw_provider_binding_init(&owner.sending.selection.identity,&config,"Owned Folder","PPSA04734",BW_PROVIDER_FOLDER));memset(owner.sending.selection.run,'a',32);
    prepared.state=BW_PREPARED_VALID;prepared.bytes_verified=1;prepared.local_count=1;prepared.entries=2;prepared.local_bytes=1024;prepared.remote_bytes=1024*1024;prepared.allocated_bytes=4096;
    strcpy(prepared.game,owner.sending.selection.identity.binding.game);strcpy(prepared.run,owner.sending.selection.run);memset(prepared.record_sha256,0x12,32);
}
int main(void){
    for(int mode=0;mode<=6;mode++){
        reset(mode);C(!prepare_worker(&owner));C(copies==1&&job_success==1&&library_prepared.state==BW_PREPARED_VALID&&library_prepared.bytes_verified&&owner.preparation_blocked==(mode==2||mode==5||mode==6)&&!owner.active&&startup_closes==1);
        if(mode==0){C(bundles==1&&begins==1&&registers==1&&commits==1&&destroys==1&&owner.home_ready[0]==1&&!owner.control_release_unknown);}
        else{C(owner.home_ready[0]==2&&owner.home_setup_reason[0]);if(mode<=2)C(!begins&&!registers);if(mode==3)C(begins==1&&!registers);if(mode>=4)C(registers==1&&commits==1);C(owner.control_release_unknown==(mode==2||mode==5||mode==6));}
        if(mode==4||mode==5)C(strstr(owner.home_setup_reason,"Setup step")&&!strstr(owner.home_setup_reason,"choose Play"));
        C(!pthread_mutex_destroy(&owner.lock));
    }
    reset(0);unsigned invalid=0;C(!home_prepare_provision(&owner,&prepared,&invalid));C(!invalid&&!copies&&bundles==1&&registers==1&&owner.home_ready[0]==1);C(!pthread_mutex_destroy(&owner.lock));
    reset(0);struct bw_share_config container_config={0};strcpy(container_config.server,"192.0.2.20");strcpy(container_config.share,"OwnedBackups");
    C(!bw_provider_binding_init(&owner.sending.selection.identity,&container_config,"Owned.ffpfsc","PPSA04734",BW_PROVIDER_CONTAINER));strcpy(prepared.game,owner.sending.selection.identity.binding.game);
    C(!home_prepare_provision(&owner,&prepared,&invalid));C(!invalid&&!copies&&bundles==1&&registers==1&&owner.home_ready[0]==1);C(!pthread_mutex_destroy(&owner.lock));
    for(int mode=7;mode<=8;mode++){
        reset(mode);invalid=0;C(home_prepare_provision(&owner,&prepared,&invalid)<0&&invalid);
        C(owner.control_release_unknown==(mode==8)&&!bundles&&!begins&&!registers&&!commits&&!startup_closes&&owner.home_ready[0]==2);
        C(strstr(owner.home_setup_reason,"startup-admission"));C(!pthread_mutex_destroy(&owner.lock));
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualSendCompletionAndProvisionOrder\":true,\"sourceReceiptBundleAndSonyBoundariesModeled\":true}\n",cases,checks);return 0;
}
