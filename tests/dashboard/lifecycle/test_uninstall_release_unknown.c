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
static unsigned release_enabled,release_injected,release_ordinary;
int bw_test_uninstall_reserved_worker(const struct bw_game_uninstall_request *,struct bw_game_uninstall_result *,unsigned);
void bw_test_uninstall_release_final_close(void);
unsigned bw_test_uninstall_release_checks(void);
static int release_expected(void){return !release_ordinary;}
int reserved_chain_uninstall(const struct bw_game_uninstall_request *q,uint16_t port,struct bw_game_uninstall_result *out){
    CHECK(!port);release_enabled=1;int rc=bw_test_uninstall_reserved_worker(q,out,release_expected());release_enabled=0;
    CHECK(rc&&!out->mutation_started&&!out->files_removed);CHECK(out->release_unknown==(unsigned)release_expected());return rc;
}
int gu_test_close(int fd){int inject=release_enabled&&!release_injected&&(is_case("release-core-before-intent")||is_case("release-final-core-close-before-intent"));int rc=retained_gu_test_close(fd);if(inject){release_injected=1;errno=EIO;return -1;}return rc;}
int gur_test_close(int fd){int inject=release_enabled&&!release_injected&&is_case("release-adapter-before-intent");int rc=retained_gur_test_close(fd);if(inject){release_injected=1;errno=EIO;return -1;}return rc;}
int __wrap_close(int fd){
    int inject=0;if(release_enabled&&!release_injected&&is_case("release-remote-status-before-intent")){
        char link[64],name[4096];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,name,sizeof name-1);
        if(n>0){name[n]=0;inject=!strcmp(name,BW_NRC_HOST_MANUAL_STATUS_PATH);}}
    int rc=retained_global_close(fd);if(inject){release_injected=1;errno=EIO;return -1;}return rc;
}
static int release_report(int rc){
    CHECK(release_injected==(unsigned)release_expected());
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualAdapterCoreWorker\":true,\"releaseUnknown\":%s,\"injectedCloseFailures\":%u,\"noDestructiveIntent\":true,\"consoleActions\":0}\n",cases,checks+bw_test_uninstall_release_checks(),release_expected()?"true":"false",release_injected);return rc;
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
    if(!strcmp(argv[2],"release-final-core-close-before-intent"))bw_test_uninstall_release_final_close();
    return release_report(retained_release_main(argc,argv));
}
