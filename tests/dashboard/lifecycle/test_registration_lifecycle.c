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
#ifdef BW_NRL_UNINSTALL_BOUNDARY_TEST
/* Keep this focused branch test in the real lifecycle translation unit. */
#include "test_registration_cleanup_io.h"
#include "native_registration_lifecycle.c"
#undef open
#undef openat
#undef close
#undef fstatat
#undef write
#undef pread
#undef fsync
#endif

static int root_close_error;
int nrc_test_close(int fd){int fail=root_close_error&&fd>=0&&fd<4096&&!strcmp(fd_path[fd],"/");int rc=nrl_legacy_close(fd);if(fail){errno=EIO;return -1;}return rc;}
struct lifecycle_fixture {
    struct fixture f;
    struct bw_nrl_owner *owner;
    struct bw_nrl_request request;
    char scan[P];
    unsigned manual,removes,uninstall_calls,unmount_calls,observations,register_guards,retire_guards;
    int unmount_error,unmount_no_effect,unmount_cap_missing,unmount_current_failure,uninstall_known_refusal;
    int current_error,source_conflict,settings_error,remove_error,remove_effect,uninstall_error,uninstall_effect,release_allowed,source_mounted,source_empty,drop_mount;
    unsigned direct_uninstall,boundary_kind,boundary_changed;
};
static struct lifecycle_fixture *api;
static int control(void *opaque,enum bw_nrl_control point){
    struct lifecycle_fixture *t=opaque;CHECK(t==api);
    if(point==BW_NRL_REGISTER)t->register_guards++;
    else{CHECK(point==BW_NRL_RETIRE);t->retire_guards++;if(!t->release_allowed)return -1;}
    if(t->direct_uninstall&&t->boundary_kind&&!t->boundary_changed){
        char saved[P+32];const char *target=t->boundary_kind==1?t->f.app:t->f.source;
        snprintf(saved,sizeof saved,"%s.replaced",target);CHECK(!rename(target,saved)&&!mkdir(target,0755));t->boundary_changed=1;
    }
    return t->current_error;
}
static void document(struct bw_sm_document *out,const char *text){out->size=strlen(text);out->bytes=malloc(out->size+1);CHECK(out->bytes);memcpy(out->bytes,text,out->size+1);}
void bw_sm_document_clear(struct bw_sm_document *p){free(p->bytes);memset(p,0,sizeof *p);}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *out){
    CHECK(api&&c&&c->current&&!c->current(c->opaque));memset(out,0,sizeof *out);
    if(kind==BW_SM_VERSION){out->capabilities=7;out->remove_supported=out->uninstall_supported=out->settings_supported=1;out->unmount_supported=!api->unmount_cap_missing;return 0;}
    CHECK(!strcmp(root,api->f.source));
    if(kind==BW_SM_MANUAL_LIST){out->count=out->present=api->manual;return 0;}
    CHECK(kind==BW_SM_MANUAL_REMOVE&&++api->removes==1);
    CHECK(api->unmount_calls==1&&!api->source_mounted);
    char pending[P];marker_path(&api->f,"pending",pending);struct stat st;CHECK(!lstat(pending,&st)&&st.st_size==128&&pending_synced&&pending_dir_synced);
    marker_path(&api->f,"retire",pending);CHECK(!lstat(pending,&st)&&st.st_size==128);
    if(api->remove_effect){api->manual=0;if(api->drop_mount){char link[P];path(link,api->f.app,"mount.lnk");CHECK(!unlink(link));}}
    out->present=api->manual;out->changed=api->remove_effect;return api->remove_error;
}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    CHECK(api&&c&&c->current&&!c->current(c->opaque));api->observations++;char text[8192];int n=-1;
    if(kind==BW_SM_OBSERVE_GAMES){
        n=snprintf(text,sizeof text,"{\"status\":0,\"count\":1,\"games\":[{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"%s\",\"content_id\":\"%s\",\"installed\":%s,\"installed_pkg\":%s,\"managed\":false,\"source_available\":true,\"image_backed\":false,\"mounted\":%s}]}",api->f.source,api->f.source,api->f.selected.title,api->source_empty?"":api->f.content,api->source_empty?"false":"true",api->source_conflict?"true":"false",api->source_mounted?"true":"false");
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
    if(api->direct_uninstall){memset(out,0,sizeof *out);return -1;}
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
#ifdef BW_NRL_UNINSTALL_BOUNDARY_TEST
static void uninstall_boundary_cases(void){
    for(unsigned kind=0;kind<3;kind++){
        struct lifecycle_fixture t;init_lifecycle(&t,kind==0?"released-uninstall-exact-witness-admitted":kind==1?"released-uninstall-control-app-replacement":"released-uninstall-control-source-replacement");start(&t);
        struct bw_native_reader_release released=release_of(&t);t.owner->release=released;t.owner->phase=BW_NRL_RETIRE;
        t.owner->runtime_release_done=t.owner->manual_requested=1;t.manual=0;t.direct_uninstall=1;t.boundary_kind=kind;
        /* CONTROL runs after the core's last witness. The already-released
         * branch must check that retained witness again before title-only I/O. */
        CHECK(nrl_uninstall(t.owner,t.f.selected.title)<0);
        CHECK(kind?(t.boundary_changed&&!t.uninstall_calls):t.uninstall_calls==1);
        CHECK(!t.removes&&!t.unmount_calls);finish(&t);
    }
}
#endif
int main(int argc,char **argv){
    program=argv[0];case_name="startup";CHECK(argc==2&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);settings_cases();
    struct lifecycle_fixture t;struct bw_nrc_result r;struct bw_nrc_retirement_result rr;struct bw_native_reader_release release;
    init_lifecycle(&t,"container-registration-normal-retirement");t.f.selected.source_kind=BW_PROVIDER_CONTAINER;
    start(&t);release=release_of(&t);CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.uninstall_calls==1);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT&&t.uninstall_calls==1);finish(&t);
    init_lifecycle(&t,"new-registration-normal-retirement");start(&t);release=release_of(&t);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.removes==1&&t.uninstall_calls==1);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT&&t.removes==1&&t.uninstall_calls==1);finish(&t);
    init_lifecycle(&t,"mounted-generic-collector-stays-strict");start(&t);release=release_of(&t);t.source_mounted=1;
    struct bw_nrc_subject subject=owner_subject(&t);struct bw_nrc_remote remote={.connection={.current=retirement_guard,.opaque=&t}};
    CHECK(bw_nrc_remote_sources(&remote,&subject)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"scanner-link-removal-after-consumed-manual");start(&t);release=release_of(&t);t.source_mounted=1;t.drop_mount=1;
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&rr.registration.missing_mount_link&&t.removes==1&&t.uninstall_calls==1);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT&&t.removes==1&&t.uninstall_calls==1);finish(&t);
    init_lifecycle(&t,"link-missing-before-manual-refused");start(&t);release=release_of(&t);
    char missing_link[P];path(missing_link,t.f.app,"mount.lnk");CHECK(!unlink(missing_link));
    CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"captured-app-param-inode-substitution-refused");start(&t);release=release_of(&t);char changed[P],saved[P];
    path(changed,t.f.app,"sce_sys/param.json");path(saved,t.f.root,"saved-app-param.json");CHECK(!rename(changed,saved));file(changed,t.f.param,strlen(t.f.param),0644);
    CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"postabsence-cache-empty-is-not-app-absence");start(&t);release=release_of(&t);t.source_empty=1;
    CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"preexisting-match-never-adopted");registered(&t.f);CHECK(!bw_nrl_begin(&t.request,&t.owner,&r)&&r.state==BW_NRC_UNOWNED);
    struct bw_register_result reg={0,0,0,1,"preexisting",0,0};CHECK(!bw_nrl_registered(t.owner,0,&reg,&r));release=release_of(&t);CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_UNOWNED&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"registration-failed-no-retirement");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));registered(&t.f);reg=(struct bw_register_result){1,0,0,0,"unknown",0,0};CHECK(bw_nrl_registered(t.owner,-1,&reg,&r)<0);release=release_of(&t);CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"no-real-mutation-no-new-owner");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));registered(&t.f);reg=(struct bw_register_result){0,0,0,1,"race",0,0};CHECK(bw_nrl_registered(t.owner,0,&reg,&r)<0);finish(&t);
    init_lifecycle(&t,"commit-control-refusal-diagnostic");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));registered(&t.f);reg=(struct bw_register_result){1,1,0,1,"actual",0,0};t.current_error=-1;
    CHECK(bw_nrl_registered(t.owner,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_current_sources")&&!strcmp(r.detail,"registration_control_guard_refused"));finish(&t);
    init_lifecycle(&t,"commit-collector-refusal-diagnostic");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));registered(&t.f);t.source_conflict=1;
    CHECK(bw_nrl_registered(t.owner,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_current_sources")&&!strcmp(r.detail,"cleanup_game_snapshot_conflict_or_unknown"));finish(&t);
    init_lifecycle(&t,"mounted-registration-is-not-a-live-game");CHECK(!bw_nrl_begin(&t.request,&t.owner,&r));registered(&t.f);t.source_mounted=1;
    CHECK(!bw_nrl_registered(t.owner,0,&reg,&r)&&r.state==BW_NRC_OWNED&&!r.stage);release=release_of(&t);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.removes==1&&t.uninstall_calls==1);
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_ABSENT&&t.removes==1&&t.uninstall_calls==1);finish(&t);
    for(unsigned i=0;i<4;i++){
        init_lifecycle(&t,i==0?"runtime-release-unknown":i==1?"io-release-unknown":i==2?"private-release-blocked":"foreign-run-release");start(&t);release=release_of(&t);
        t.source_mounted=1;if(i==0)release.runtime_released=0;else if(i==1)release.io_released=0;else if(i==2)release.private_released=0;else release.run[0]^=1;
        CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);no_pending(&t.f);finish(&t);
    }
    init_lifecycle(&t,"new-game-or-settings-reservation-refused");start(&t);release=release_of(&t);t.current_error=-1;CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);no_pending(&t.f);finish(&t);
    init_lifecycle(&t,"same-title-foreign-source-veto");start(&t);release=release_of(&t);t.source_conflict=1;CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);no_pending(&t.f);finish(&t);
    init_lifecycle(&t,"effective-scanner-root-veto");start(&t);release=release_of(&t);strcpy(t.scan,t.f.games);CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);no_pending(&t.f);finish(&t);
    init_lifecycle(&t,"settings-unreadable-veto");start(&t);release=release_of(&t);t.settings_error=1;CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);no_pending(&t.f);finish(&t);
    init_lifecycle(&t,"source-root-namespace-change-refused");start(&t);release=release_of(&t);char pathbuf[P];path(pathbuf,t.f.source,"unexpected");file(pathbuf,"x",1,0644);CHECK(retire(&t,&release,&rr)<0&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"manual-unknown-never-replayed");start(&t);release=release_of(&t);t.remove_effect=0;t.remove_error=-1;CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.removes==1&&!t.uninstall_calls);CHECK(!retire(&t,&release,&rr)&&t.removes==1&&!t.uninstall_calls);finish(&t);
    CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0&&!t.owner&&r.state==BW_NRC_PENDING);
    /* Restart can observe an externally completed result, never advance the
     * unattempted uninstall phase after merely seeing manual absence. */
    t.manual=0;unsigned prior=t.removes+t.uninstall_calls;
    CHECK(bw_nrl_observe(&t.f.env,t.f.selected.title,t.f.selected.run,0,control,&t,&rr)<0&&rr.registration.state==BW_NRC_UNKNOWN&&prior==t.removes+t.uninstall_calls);
    remove_registration(&t.f);t.source_empty=1;CHECK(!bw_nrl_observe(&t.f.env,t.f.selected.title,t.f.selected.run,0,control,&t,&rr)&&rr.registration.state==BW_NRC_ABSENT&&prior==t.removes+t.uninstall_calls);
    init_lifecycle(&t,"uninstall-lost-response-no-retry");start(&t);release=release_of(&t);t.uninstall_effect=0;t.uninstall_error=-1;
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&!rr.registration.uninstall_accepted&&t.uninstall_calls==1);CHECK(!retire(&t,&release,&rr)&&t.uninstall_calls==1);finish(&t);
    remove_registration(&t.f);t.source_empty=1;CHECK(!bw_nrl_observe(&t.f.env,t.f.selected.title,t.f.selected.run,0,control,&t,&rr)&&rr.registration.state==BW_NRC_ABSENT&&t.uninstall_calls==1);
    init_lifecycle(&t,"retained-owner-new-play-release");owned(&t.f,0);char original[33];strcpy(original,t.f.selected.run);t.f.selected.run[0]='f';
    CHECK(!bw_nrl_begin(&t.request,&t.owner,&r)&&r.state==BW_NRC_OWNED);reg=(struct bw_register_result){0,0,0,1,"retained owner",0,0};CHECK(!bw_nrl_registered(t.owner,0,&reg,&r));
    release=release_of(&t);/* Marker probes use the historical registration run. */strcpy(t.f.selected.run,original);memcpy(release.run,t.request.selection->run,33);
    /* The public selection pointer is mutable only in this test; correct it
     * back to the captured Play run for the release, retaining marker paths. */
    release.run[0]='f';CHECK(!retire(&t,&release,&rr)&&t.removes==1&&t.uninstall_calls==1);finish(&t);
    init_lifecycle(&t,"normal-runtime-release-busy-consumed-no-next-call");start(&t);release=release_of(&t);t.source_mounted=1;t.unmount_error=1;
    CHECK(!retire(&t,&release,&rr)&&rr.registration.state==BW_NRC_PENDING&&t.unmount_calls==1&&!t.removes&&!t.uninstall_calls);
    CHECK(rr.registration.detail&&!strcmp(rr.registration.detail,"cleanup_runtime_release_busy_no_retry"));
    CHECK(!retire(&t,&release,&rr)&&t.unmount_calls==1&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"normal-runtime-release-capability-required");start(&t);release=release_of(&t);t.source_mounted=1;t.unmount_cap_missing=1;
    CHECK(!retire(&t,&release,&rr)&&!t.unmount_calls&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"normal-runtime-release-ack-remount-refused");start(&t);release=release_of(&t);t.source_mounted=1;t.unmount_no_effect=1;
    CHECK(!retire(&t,&release,&rr)&&t.unmount_calls==1&&!t.removes&&!t.uninstall_calls);finish(&t);
    init_lifecycle(&t,"normal-runtime-release-current-lost");start(&t);release=release_of(&t);t.source_mounted=1;t.unmount_current_failure=1;
    CHECK(!retire(&t,&release,&rr)&&t.unmount_calls==1&&!t.removes&&!t.uninstall_calls);t.current_error=0;finish(&t);
    init_lifecycle(&t,"normal-uninstall-complete-known-preplatform-refusal");start(&t);release=release_of(&t);t.uninstall_known_refusal=1;
    CHECK(!retire(&t,&release,&rr)&&t.unmount_calls==1&&t.removes==1&&t.uninstall_calls==1&&!rr.registration.uninstall_accepted);
    CHECK(rr.registration.detail&&!strcmp(rr.registration.detail,"cleanup_uninstall_refused_title_unmount_busy_before_platform"));
    CHECK(!retire(&t,&release,&rr)&&t.uninstall_calls==1);finish(&t);
#ifdef BW_NRL_UNINSTALL_BOUNDARY_TEST
    uninstall_boundary_cases();
#endif
    CHECK(open_count==close_count);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"opens\":%u,\"closes\":%u,\"realFilesystem\":true,\"readonlySqlite\":true,\"normalApiModeled\":true,\"consoleActions\":0}\n",cases,checks,open_count,close_count);return 0;
}
