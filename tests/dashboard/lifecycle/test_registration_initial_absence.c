/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual lifecycle, ledger, no-follow collector and readonly SQLite. Only the
 * normal SMP API and joined-process CONTROL results are modeled. */
#define main retained_cleanup_cases
#define nrc_test_close nrl_legacy_close
#include "test_registration_cleanup.c"
#undef nrc_test_close
#undef main
#include "native_registration_lifecycle.h"
#include "native_cleanup_remote.h"
#include "register_job.h"

static int root_close_error,release_close_enabled,release_arm_game;
static unsigned release_close_hits;
static char release_close_path[P];
#ifndef BW_NRC_RELEASE_UNCONFIRMED
#define BW_NRC_RELEASE_UNCONFIRMED (-2) /* Baseline-only reproduction. */
#endif
int nrc_test_close(int fd){int fail=root_close_error&&fd>=0&&fd<4096&&!strcmp(fd_path[fd],"/");
    if(release_close_enabled&&fd>=0&&fd<4096&&fd_live[fd]&&!strcmp(fd_path[fd],release_close_path)&&++release_close_hits==1)fail=1;
    int rc=nrl_legacy_close(fd);if(fail){errno=EIO;return -1;}return rc;}
static void arm_release_close(const char *path){CHECK(strlen(path)<P);strcpy(release_close_path,path);release_close_hits=0;release_close_enabled=1;}
static void released_fault(void){release_close_enabled=0;CHECK(release_close_hits==1&&open_count==close_count);}
struct lifecycle_fixture {
    struct fixture f;
    struct bw_nrl_owner *owner;
    struct bw_nrl_request request;
    char scan[P];
    unsigned manual,removes,uninstall_calls,unmount_calls,observations,register_guards,retire_guards;
    int unmount_error,unmount_no_effect,unmount_cap_missing,unmount_current_failure,uninstall_known_refusal;
    int current_error,source_conflict,settings_error,remove_error,remove_effect,uninstall_error,uninstall_effect,release_allowed,source_mounted,source_empty,drop_mount;
    unsigned adds,cache_fault,late_fault;
    double tick;
};
static struct lifecycle_fixture *api;
static int control(void *opaque,enum bw_nrl_control point){
    struct lifecycle_fixture *t=opaque;CHECK(t==api);
    if(point==BW_NRL_REGISTER)t->register_guards++;
    else{CHECK(point==BW_NRL_RETIRE);t->retire_guards++;if(!t->release_allowed)return -1;}
    return t->current_error;
}
static void document(struct bw_sm_document *out,const char *text){out->size=strlen(text);out->bytes=malloc(out->size+1);CHECK(out->bytes);memcpy(out->bytes,text,out->size+1);}
void bw_sm_document_clear(struct bw_sm_document *p){free(p->bytes);memset(p,0,sizeof *p);}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *out){
    CHECK(api&&c&&c->current&&!c->current(c->opaque));memset(out,0,sizeof *out);
    if(kind==BW_SM_VERSION){out->capabilities=7;out->remove_supported=out->uninstall_supported=out->settings_supported=1;out->unmount_supported=!api->unmount_cap_missing;return 0;}
    CHECK(!strcmp(root,api->f.source));
    if(kind==BW_SM_MANUAL_LIST){out->count=out->present=api->manual;return 0;}
    if(kind==BW_SM_MANUAL_ADD){
        char intent[P];struct stat st;marker_path(&api->f,"intent",intent);
        CHECK(!api->manual&&++api->adds==1&&!lstat(intent,&st)&&S_ISREG(st.st_mode)&&st.st_size==8224);
        registered(&api->f);api->source_empty=0;api->source_mounted=1;api->manual=1;out->present=out->changed=1;return 0;
    }
    CHECK(kind==BW_SM_MANUAL_REMOVE&&++api->removes==1);
    CHECK(api->unmount_calls==1&&!api->source_mounted);
    char pending[P];marker_path(&api->f,"pending",pending);struct stat st;CHECK(!lstat(pending,&st)&&st.st_size==128&&pending_synced&&pending_dir_synced);
    marker_path(&api->f,"retire",pending);CHECK(!lstat(pending,&st)&&st.st_size==128);
    if(api->remove_effect){api->manual=0;if(api->drop_mount){char link[P];path(link,api->f.app,"mount.lnk");CHECK(!unlink(link));}}
    out->present=api->manual;out->changed=api->remove_effect;return api->remove_error;
}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    CHECK(api&&c&&c->current);if(c->current(c->opaque))return -1;api->observations++;char text[8192];int n=-1;
    if(kind==BW_SM_OBSERVE_GAMES){
        if(release_arm_game){release_arm_game=0;arm_release_close(api->f.data);}
        n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"%s\",\"content_id\":\"%s\",\"installed\":%s,\"installed_pkg\":%s,\"managed\":false,\"source_available\":true,\"image_backed\":false,\"mounted\":%s}]}",api->f.source,api->f.source,api->f.selected.title,api->source_empty?"":api->f.content,api->source_empty?"false":"true",api->source_conflict?"true":"false",api->source_mounted?"true":"false");
        /* Synthetic typed variants of the observed stale-cache row. */
        if(api->cache_fault){
            const char *old=NULL,*replacement=NULL;char runtime[P+32];
            switch(api->cache_fault){
            case 1:old="\"installed\":false";replacement="\"installed\":true";break;
            case 2:old="\"managed\":false";replacement="\"managed\":true";break;
            case 3:old="\"mounted\":false";replacement="\"mounted\":true";break;
            case 4:old="\"source_type\":\"folder\"";replacement="\"source_type\":\"pkg\"";break;
            case 5:old="\"image_backed\":false";replacement="\"image_backed\":true";break;
            case 6:old="\"content_id\":\"\"";replacement="\"content_id\":\"foreign\"";break;
            case 7:old="\"source_available\":true";replacement="\"source_available\":\"true\"";break;
            case 8:old="\"content_id\":\"\"";replacement="\"content_id\":\"\",\"content_id\":\"\"";break;
            case 9:old="\"source_type\":\"folder\"";replacement="\"source_type\":\"image\"";break;
            case 10:CHECK(snprintf(runtime,sizeof runtime,"\"runtime_path\":\"%s\"",api->f.source)>0);old=runtime;replacement="\"runtime_path\":\"/unrelated\"";break;
            default:CHECK(0);
            }
            char *at=strstr(text,old);CHECK(at);size_t a=(size_t)(at-text),b=strlen(old),z=strlen(replacement);
            CHECK(strlen(text)-b+z<sizeof text);memmove(at+z,at+b,strlen(at+b)+1);memcpy(text+a,replacement,z);n=(int)strlen(text);
        }
        if(api->late_fault&&api->observations==1){
            if(api->late_fault==1)make(api->f.app,0755);
            else if(api->late_fault==2)row(&api->f,api->f.content);
            else if(api->late_fault==3)api->current_error=-1;
            else if(api->late_fault==4){char p[P];path(p,api->f.root,"saved-apps");CHECK(!rename(api->f.apps,p));make(api->f.apps,0755);}
            else CHECK(0);
        }
    }else if(kind==BW_SM_OBSERVE_MANUAL_PATHS){
        if(api->manual)n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"paths\":[\"%s\"]}",api->f.source);
        else n=snprintf(text,sizeof text,"{\"status\":0,\"count\":0,\"paths\":[]}");
    }else{CHECK(kind==BW_SM_OBSERVE_SETTINGS);if(api->settings_error)return -1;
        n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",api->scan);
    }
    CHECK(n>0&&(size_t)n<sizeof text);document(out,text);return 0;
}
int bw_sm_uninstall(const struct bw_sm_connection *c,const char title[10],struct bw_sm_uninstall_reply *out){
    CHECK(api&&c&&c->current&&!c->current(c->opaque)&&!strcmp(title,api->f.selected.title)&&!api->manual&&++api->uninstall_calls==1);
    char p[P];struct stat st;marker_path(&api->f,"uninstall",p);CHECK(!lstat(p,&st)&&st.st_size==128);
    CHECK(!api->source_mounted);memset(out,0,sizeof *out);out->requested=1;
    if(api->uninstall_known_refusal){out->requested=0;out->response_complete=out->refused_before_platform=1;out->http_status=409;out->api_status=16;strcpy(out->error_reason,"title_unmount_busy");return -1;}
    if(api->uninstall_effect){remove_registration(&api->f);api->source_empty=1;api->source_mounted=0;}
    return api->uninstall_error;
}
int bw_sm_unmount(const struct bw_sm_connection *c,const char title[10],struct bw_sm_unmount_reply *out){
    CHECK(api&&c&&c->current&&!c->current(c->opaque)&&!strcmp(title,api->f.selected.title)&&++api->unmount_calls==1);
    char p[P];struct stat st;marker_path(&api->f,"retire",p);CHECK(!lstat(p,&st)&&st.st_size==128);
    path(p,api->f.app,"mount.lnk");CHECK(!lstat(p,&st)&&S_ISREG(st.st_mode));
    CHECK(bw_registration_db_match(api->f.db,title,api->f.content)==1&&!api->removes&&!api->uninstall_calls);
    memset(out,0,sizeof *out);out->response_complete=1;out->http_status=api->unmount_error?409:200;out->api_status=api->unmount_error?16:0;
    if(api->unmount_error)return -1;
    if(!api->unmount_no_effect)api->source_mounted=0;out->released=1;
    if(api->unmount_current_failure)api->current_error=-1;return 0;
}
static void init_lifecycle(struct lifecycle_fixture *t,const char *name){
    memset(t,0,sizeof *t);init(&t->f,name);api=t;root_close_error=0;
    path(t->scan,t->f.root,"unrelated-scan");t->manual=1;t->remove_effect=1;t->uninstall_effect=1;
    /* The existing bundle is installed before source-root ownership capture.
     * Creating/changing it later is tested as a real refusal below. */
    char p[P];path(p,t->f.source,"fakelib2");make(p,0755);
    t->request=(struct bw_nrl_request){.environment=t->f.env,.selection=&t->f.selected,.current=control,.opaque=t};
}
static void start(struct lifecycle_fixture *t){
    struct bw_nrc_result r;int opened=bw_nrl_begin(&t->request,&t->owner,&r);
    if(opened)fprintf(stderr,"lifecycle begin rc=%d state=%d reason=%s registration=%d guards=%u observations=%u\n",opened,r.state,r.reason?r.reason:"none",bw_native_registration_state(t->f.data,t->f.apps,t->f.db,&t->f.selected),t->register_guards,t->observations);
    CHECK(!opened&&t->owner&&r.state==BW_NRC_INTENT);
    registered(&t->f);struct bw_register_result reg={1,1,0,1,"actual fixed registration fixture",0,0};
    CHECK(!bw_nrl_registered(t->owner,0,&reg,&r)&&r.state==BW_NRC_OWNED&&t->register_guards);
}
static struct bw_native_reader_release release_of(struct lifecycle_fixture *t){
    struct bw_native_reader_release r={.runtime_released=1,.io_released=1,.private_released=1};memcpy(r.run,t->f.selected.run,33);t->release_allowed=1;return r;
}
static int retire(struct lifecycle_fixture *t,struct bw_native_reader_release *release,struct bw_nrc_retirement_result *out){
    unsigned char before[4][32],after[4][32];char p[P];path(p,t->f.source,"eboot.bin");hash_file(p,before[0]);hash_file(t->f.receipt,before[1]);
    path(p,t->f.source,"sce_sys/param.json");hash_file(p,before[2]);path(p,t->f.root,"full-manual/eboot.bin");hash_file(p,before[3]);
    int rc=bw_nrl_retire(t->owner,release,out);
    path(p,t->f.source,"eboot.bin");hash_file(p,after[0]);hash_file(t->f.receipt,after[1]);path(p,t->f.source,"sce_sys/param.json");hash_file(p,after[2]);path(p,t->f.root,"full-manual/eboot.bin");hash_file(p,after[3]);CHECK(!memcmp(before,after,sizeof before));return rc;
}
static void finish(struct lifecycle_fixture *t){CHECK(!bw_nrl_destroy(&t->owner)&&!t->owner&&open_count==close_count);}
static int settings_guard(void *p){return *(int *)p;}
static int retirement_guard(void *p){return control(p,BW_NRL_RETIRE);}
static struct bw_nrc_subject owner_subject(struct lifecycle_fixture *t){
    struct bw_nrc_candidate rows[2];size_t count=0;CHECK(!bw_nrc_candidates(&t->f.env,rows,2,&count)&&count==1);return rows[0].subject;
}
static void settings_cases(void){
    struct lifecycle_fixture t;init_lifecycle(&t,"roots-canonical-and-absent");struct bw_nrc_subject s={0};strcpy(s.source,t.f.source);int cancelled=0;struct bw_sm_connection c={.current=settings_guard,.opaque=&cancelled};
    char text[4096];int n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",t.scan);CHECK(n>0&&!bw_nrl_settings_exclude(&c,&s,text,(size_t)n));
    make(t.scan,0755);CHECK(!bw_nrl_settings_exclude(&c,&s,text,(size_t)n));
    const char *all="{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"/\"]}";CHECK(bw_nrl_settings_exclude(&c,&s,all,strlen(all)));
    const char *bad[]={"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[]}","{\"status\":0,\"scan_path_count\":0,\"scan_paths\":[],\"scan_path_count\":0}","{\"status\":1,\"scan_path_count\":0,\"scan_paths\":[]}","{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"/data/../blackwire\"]}","{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"relative\"]}"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)CHECK(bw_nrl_settings_exclude(&c,&s,bad[i],strlen(bad[i]))<0);
    cancelled=1;CHECK(bw_nrl_settings_exclude(&c,&s,text,(size_t)n)<0);cancelled=0;
    CHECK(rmdir(t.scan)==0&&symlink(t.f.source,t.scan)==0);CHECK(bw_nrl_settings_exclude(&c,&s,text,(size_t)n)<0);CHECK(unlink(t.scan)==0);
    n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",t.f.games);CHECK(n>0&&bw_nrl_settings_exclude(&c,&s,text,(size_t)n)<0);
    const char *defaults="{\"status\":0,\"scan_path_count\":0,\"scan_paths\":[]}";CHECK(!bw_nrl_settings_exclude(&c,&s,defaults,strlen(defaults)));
    root_close_error=1;n=snprintf(text,sizeof text,"{\"status\":0,\"scan_path_count\":1,\"scan_paths\":[\"%s\"]}",t.scan);CHECK(bw_nrl_settings_exclude(&c,&s,text,(size_t)n)<0);root_close_error=0;CHECK(open_count==close_count);
}
static int register_current(void *opaque){return control(opaque,BW_NRL_REGISTER);}
static enum bw_registration_state register_state(void *opaque){
    struct lifecycle_fixture *t=opaque;return bw_native_registration_state(t->f.data,t->f.apps,t->f.db,&t->f.selected);
}
static int register_post(void *opaque,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *reply){
    struct bw_sm_connection c={.current=register_current,.opaque=opaque};return bw_sm_post(&c,kind,root,reply);
}
static double register_now(void *opaque){return ((struct lifecycle_fixture *)opaque)->tick;}
static void register_pause(void *opaque,unsigned ms){((struct lifecycle_fixture *)opaque)->tick+=(double)ms/1000;}
static void register_progress(void *opaque,const char *message){CHECK(opaque==api&&message&&message[0]);}
static void cached_absent(struct lifecycle_fixture *t,const char *name){init_lifecycle(t,name);t->manual=0;t->source_empty=1;}
static void normal_register(struct lifecycle_fixture *t,struct bw_register_result *out){
    struct bw_register_ops ops={t,register_current,register_state,register_post,register_now,register_pause,register_progress};
    CHECK(!bw_register_job(t->f.source,&ops,out)&&out->add_attempted==1&&out->add_confirmed==1&&!out->scan_attempted&&out->registered==1&&t->adds==1);
    CHECK(bw_native_registration_state(t->f.data,t->f.apps,t->f.db,&t->f.selected)==BW_REG_MATCH);
}
static void no_add_or_owner(struct lifecycle_fixture *t){
    char p[P];struct stat st;CHECK(!t->owner&&!t->adds&&!t->removes&&!t->uninstall_calls&&!t->unmount_calls);
    marker_path(&t->f,"owner",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);CHECK(open_count==close_count);
}
int main(int argc,char **argv){
    program=argv[0];case_name="startup";CHECK((argc==2||argc==3)&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);
    struct lifecycle_fixture t;struct bw_nrc_result r;struct bw_register_result reg;struct bw_nrc_retirement_result rr;
    cached_absent(&t,"core-begin-failed-context-close");t.f.sources_value=-1;arm_release_close(t.f.data);struct bw_nrc_ticket *ticket=NULL;
    int core_result=bw_nrc_begin(&t.f.env,&t.f.selected,&t.f.ops,&ticket,&r);released_fault();CHECK(!ticket&&r.state==BW_NRC_UNKNOWN&&!t.adds);
    if(argc==3&&!strcmp(argv[2],"--baseline-core-close")){
        CHECK(core_result==-1);printf("{\"passed\":true,\"baselineReleaseSeverityDropped\":true,\"cases\":%u,\"checks\":%u}\n",cases,checks);return 0;
    }
    CHECK(core_result==BW_NRC_RELEASE_UNCONFIRMED&&!strcmp(r.stage,"ownership_intent_current_sources"));no_add_or_owner(&t);
    cached_absent(&t,"lifecycle-begin-propagates-context-close");t.cache_fault=1;release_arm_game=1;
    CHECK(bw_nrl_begin(&t.request,&t.owner,&r)==BW_NRC_RELEASE_UNCONFIRMED&&!t.owner);released_fault();no_add_or_owner(&t);
    cached_absent(&t,"registered-ticket-close-remains-unknown");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));normal_register(&t,&reg);
    arm_release_close(t.f.data);CHECK(bw_nrl_registered(t.owner,0,&reg,&r)==BW_NRC_RELEASE_UNCONFIRMED&&r.state==BW_NRC_UNKNOWN);
    released_fault();CHECK(t.owner&&bw_nrl_destroy(&t.owner)<0&&!t.owner&&open_count==close_count&&!t.removes&&!t.uninstall_calls);
    cached_absent(&t,"final-ticket-close-is-unknown");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));arm_release_close(t.f.data);
    CHECK(bw_nrl_destroy(&t.owner)<0&&!t.owner);released_fault();no_add_or_owner(&t);
    cached_absent(&t,"stale-empty-cache-new-intent");
    int opened=bw_nrl_begin(&t.request,&t.owner,&r);
    if(argc==3){
        if(opened>=0||t.owner||!r.stage||strcmp(r.stage,"ownership_intent_current_sources")||!r.detail||strcmp(r.detail,"cleanup_game_snapshot_conflict_or_unknown"))
            fprintf(stderr,"baseline rc=%d state=%d stage=%s detail=%s reason=%s\n",opened,r.state,r.stage?r.stage:"none",r.detail?r.detail:"none",r.reason?r.reason:"none");
        CHECK(!strcmp(argv[2],"--baseline-refusal")&&opened<0&&!t.owner&&!strcmp(r.stage,"ownership_intent_current_sources")&&
              !strcmp(r.detail,"cleanup_game_snapshot_conflict_or_unknown"));no_add_or_owner(&t);
        printf("{\"passed\":true,\"baselineReproduced\":true,\"cases\":%u,\"checks\":%u}\n",cases,checks);return 0;
    }
    CHECK(!opened&&t.owner&&r.state==BW_NRC_INTENT);
    normal_register(&t,&reg);CHECK(!bw_nrl_registered(t.owner,0,&reg,&r)&&r.state==BW_NRC_OWNED);
    struct bw_native_reader_release release=release_of(&t);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.unmount_calls==1&&t.removes==1&&t.uninstall_calls==1);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT);finish(&t);
    /* Retain the old exact absent/tombstone ledger and receipt, then use a
     * different Play nonce. Nothing is reset, deleted or adopted. */
    t.f.selected.run[0]='f';t.adds=t.removes=t.uninstall_calls=t.unmount_calls=0;t.release_allowed=0;t.tick=0;
    CHECK(t.source_empty&&!t.manual&&bw_native_registration_state(t.f.data,t.f.apps,t.f.db,&t.f.selected)==BW_REG_ABSENT);
    CHECK(!bw_nrl_begin(&t.request,&t.owner,&r)&&r.state==BW_NRC_INTENT);
    normal_register(&t,&reg);CHECK(!bw_nrl_registered(t.owner,0,&reg,&r)&&r.state==BW_NRC_OWNED);
    release=release_of(&t);CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT);finish(&t);
    for(unsigned k=1;k<=10;k++){
        char name[64];snprintf(name,sizeof name,"stale-cache-negative-%u",k);cached_absent(&t,name);t.cache_fault=k;
        CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0);no_add_or_owner(&t);
    }
    for(unsigned k=1;k<=4;k++){
        char name[64];snprintf(name,sizeof name,"late-independent-absence-loss-%u",k);cached_absent(&t,name);t.late_fault=k;
        CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0);no_add_or_owner(&t);
    }
    cached_absent(&t,"occupied-app-is-not-absence");make(t.f.app,0755);CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0);no_add_or_owner(&t);
    cached_absent(&t,"occupied-db-is-not-absence");row(&t.f,t.f.content);CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0);no_add_or_owner(&t);
    cached_absent(&t,"unknown-db-is-not-absence");replace(t.f.db,"invalid",7,0644);CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0);no_add_or_owner(&t);
    cached_absent(&t,"preexisting-exact-registration-remains-unowned");registered(&t.f);
    CHECK(!bw_nrl_begin(&t.request,&t.owner,&r)&&r.state==BW_NRC_UNOWNED&&!t.adds);
    reg=(struct bw_register_result){.registered=1,.message="preexisting preserved"};CHECK(!bw_nrl_registered(t.owner,0,&reg,&r)&&r.state==BW_NRC_UNOWNED);
    release=release_of(&t);CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_UNOWNED&&!t.removes&&!t.uninstall_calls);finish(&t);
    cached_absent(&t,"commit-empty-cache-remains-strict");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));normal_register(&t,&reg);t.source_empty=1;
    CHECK(bw_nrl_registered(t.owner,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_current_sources")&&
          !strcmp(r.detail,"cleanup_game_snapshot_conflict_or_unknown"));finish(&t);
    cached_absent(&t,"commit-foreign-cache-remains-strict");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));normal_register(&t,&reg);t.source_conflict=1;
    CHECK(bw_nrl_registered(t.owner,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_current_sources"));finish(&t);
    CHECK(open_count==close_count);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualReadonlySqlite\":true,\"actualNormalRegisterJob\":true,\"oldAbsenceLedgerPreserved\":true,\"normalTransportAndReleaseModeled\":true,\"consoleActions\":0}\n",cases,checks);return 0;
}
