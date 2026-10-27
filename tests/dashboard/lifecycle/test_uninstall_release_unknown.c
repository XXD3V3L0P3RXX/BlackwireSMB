/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse the actual Send/SQLite/SMP adapter fixture, adding pre-intent close
 * faults and routing its normal invocation through the real removal worker. */
#define BW_UNINSTALL_REMOTE_MAIN retained_release_main
#define gu_test_close retained_gu_test_close
#define gur_test_close retained_gur_test_close
#define __wrap_close retained_global_close
#define bw_native_game_uninstall_normal reserved_chain_uninstall
#include "test_game_uninstall_remote.c"
#undef bw_native_game_uninstall_normal
#undef __wrap_close
#undef gur_test_close
#undef gu_test_close
#undef BW_UNINSTALL_REMOTE_MAIN
static unsigned release_enabled,release_injected,release_ordinary,release_candidate_matches,release_pending_unknown;
int bw_test_uninstall_reserved_worker(const struct bw_game_uninstall_request *,struct bw_game_uninstall_result *,unsigned);
void bw_test_uninstall_release_final_close(void);
void bw_test_uninstall_expect_pending(void);
unsigned bw_test_uninstall_release_checks(void);
void bw_test_send_owner_admission(const struct bw_game_uninstall_request *);
unsigned bw_test_send_owner_admission_cases(void);
unsigned bw_test_send_owner_admission_retry_cases(void);
unsigned bw_test_send_owner_reserved_release(void);
int bw_test_send_owner_close_fault(int);
static int release_expected(void){return !release_ordinary;}
int reserved_chain_uninstall(const struct bw_game_uninstall_request *q,uint16_t port,struct bw_game_uninstall_result *out){
    CHECK(!port);
    if(is_case("malformed-games")&&!bw_test_send_owner_admission_cases())bw_test_send_owner_admission(q);
    if(!strncmp(remote_case,"release-candidate-",18)||is_case("release-ordinary-candidate-metadata")){
        char directory[2048],marker[2048];path(directory,test_data,"registration-owners");
        if(mkdir(directory,0700))CHECK(errno==EEXIST);
        path(marker,directory,".blackwire-registration-owners");struct stat st;
        if(lstat(marker,&st)){CHECK(errno==ENOENT);const char *body=is_case("release-ordinary-candidate-metadata")?"wrong-marker":"Blackwire registration ownership ledger v1\n";file_write(marker,body,strlen(body),0600);}
    }
    release_enabled=1;int rc=bw_test_uninstall_reserved_worker(q,out,release_expected());release_enabled=0;
    CHECK(rc&&out->mutation_started==release_pending_unknown&&!out->files_removed);CHECK(out->release_unknown==(unsigned)release_expected());return rc;
}
int gu_test_close(int fd){int inject=release_enabled&&!release_injected&&(is_case("release-core-before-intent")||is_case("release-final-core-close-before-intent"));int rc=retained_gu_test_close(fd);if(inject){release_injected=1;errno=EIO;return -1;}return rc;}
int gur_test_close(int fd){int inject=release_enabled&&!release_injected&&is_case("release-adapter-before-intent");int rc=retained_gur_test_close(fd);if(inject){release_injected=1;errno=EIO;return -1;}return rc;}
int __wrap_close(int fd){
    int admission_inject=bw_test_send_owner_close_fault(fd);
    int inject=0;if(release_enabled&&!release_injected&&is_case("release-remote-status-before-intent")){
        char link[64],name[4096];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,name,sizeof name-1);
        if(n>0){name[n]=0;inject=!strcmp(name,BW_NRC_HOST_MANUAL_STATUS_PATH);}}
    if(release_enabled&&!release_injected&&(is_case("release-candidate-file-close-before-intent")||is_case("release-candidate-context-close-before-intent")||is_case("release-candidate-direct-context-close-before-intent"))){
        char link[64],name[4096],expected[2048];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,name,sizeof name-1);
        if(n>0){name[n]=0;path(expected,test_data,is_case("release-candidate-file-close-before-intent")?"registration-owners/.blackwire-registration-owners":"registration-owners");if(!strcmp(name,expected)){release_candidate_matches++;inject=!is_case("release-candidate-direct-context-close-before-intent")||release_candidate_matches==2;}}}
    int rc=retained_global_close(fd);if(inject||admission_inject){if(inject)release_injected=1;errno=EIO;return -1;}return rc;
}
int __real_closedir(DIR *);
int __wrap_closedir(DIR *dir){
    int inject=0;if(release_enabled&&!release_injected&&is_case("release-candidate-dir-close-before-intent")){
        char link[64],name[4096],expected[2048];snprintf(link,sizeof link,"/proc/self/fd/%d",dirfd(dir));ssize_t n=readlink(link,name,sizeof name-1);
        if(n>0){name[n]=0;path(expected,test_data,"registration-owners");inject=!strcmp(name,expected);}}
    int rc=__real_closedir(dir);if(inject){release_injected=1;errno=EIO;return -1;}return rc;
}
static int release_report(int rc){
    CHECK(release_injected==(unsigned)release_expected());
    if(is_case("release-candidate-direct-context-close-before-intent"))CHECK(release_candidate_matches==2);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualAdapterCoreWorker\":true,\"releaseUnknown\":%s,\"injectedCloseFailures\":%u,\"sendOwnerAdmissionCases\":%u,\"sendOwnerAdmissionRetries\":%u,\"sendOwnerReservedRelease\":%u,\"noDestructiveIntent\":%s,\"consoleActions\":0}\n",cases,checks+bw_test_uninstall_release_checks(),release_expected()?"true":"false",release_injected,bw_test_send_owner_admission_cases(),bw_test_send_owner_admission_retry_cases(),bw_test_send_owner_reserved_release(),release_pending_unknown?"false":"true");return rc;
}
int main(int argc,char **argv){
    CHECK(argc==3);
    /* Ordinary malformed remote metadata must remain a precise refusal, not
     * be promoted to descriptor-release uncertainty. */
    if(!strcmp(argv[2],"release-ordinary-metadata")){
        release_ordinary=1;argv[2]="malformed-games";return release_report(retained_release_main(argc,argv));
    }
    if(!strcmp(argv[2],"release-ordinary-same-title")){
        release_ordinary=1;argv[2]="image-source";return release_report(retained_release_main(argc,argv));
    }
    if(!strcmp(argv[2],"release-ordinary-candidate-metadata"))release_ordinary=1;
    if(!strcmp(argv[2],"release-removal-pending-no-release")){
        release_ordinary=release_pending_unknown=1;bw_test_uninstall_expect_pending();argv[2]="runtime-busy";return release_report(retained_release_main(argc,argv));
    }
    if(!strcmp(argv[2],"release-final-core-close-before-intent"))bw_test_uninstall_release_final_close();
    return release_report(retained_release_main(argc,argv));
}
