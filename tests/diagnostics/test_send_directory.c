#define _DEFAULT_SOURCE 1
#define BW_PRELOAD_BUNDLE_HOST_TEST 1
#define BW_SEND_LOG_PATH "/tmp/blackwire-send-path-test.log"
#include "preload_bundle.c"
#include <assert.h>
static const char *fail_path;
const struct bw_preload_image bw_preload_embedded_image={0};
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *path,struct stat *st){
    if(fail_path&&!strcmp(path,fail_path)){errno=ENOENT;return -1;}
    return __real_lstat(path,st);
}
int main(void){
    int fresh=open(BW_SEND_LOG_PATH,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fresh>=0);close(fresh);
    char path[]="/tmp/blackwire-directory-test-XXXXXX";assert(mkdtemp(path));
    struct bw_preload_bundle_request request={.root=path};struct bw_preload_bundle_result result={0};
    struct pb_operation operation={.request=&request,.diagnostic=&result,.stage="reader-upgrade-material"};
    struct stat st;fail_path=path;
    assert(pb_open_directory(&operation,path,&st)==-1);
    assert(result.system_error==ENOENT);assert(!strcmp(result.reason,"reader_directory_named_stat_failed"));
    assert(!strcmp(result.stage,"reader-upgrade-material"));
    int fd=open(BW_SEND_LOG_PATH,O_RDONLY);assert(fd>=0);char log[8192]={0};assert(read(fd,log,sizeof log-1)>0);close(fd);
    assert(strstr(log,"event=directory-failure stage=reader-upgrade-material call=lstat path="));
    assert(strstr(log,path));assert(strstr(log,"reason=reader_directory_named_stat_failed errno=2"));
    fail_path=NULL;memset(&result,0,sizeof result);fd=pb_open_directory(&operation,path,&st);assert(fd>=0);assert(!result.reason);close(fd);
    memset(&result,0,sizeof result);assert(bw_preload_bundle_install_checked(NULL,&result)==-1);
    assert(!strcmp(result.reason,"invalid_reader_bundle_request"));
    assert(!rmdir(path));assert(!unlink(BW_SEND_LOG_PATH));
    puts("Actual bundle directory failure retains step-7 error and logs exact path; successful open also passes.");return 0;
}
