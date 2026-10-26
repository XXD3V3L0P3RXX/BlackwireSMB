/* Expose the actual default removal caller; no alternative selector or core. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#include "native_backend.c"
int bw_test_default_legacy_remove(const struct bw_game_uninstall_request *request,struct bw_game_uninstall_result *out){
    backend_data_root=request->environment.data_root;registration_environment=request->environment;
    struct bw_native_removal_selection selected={.identity=request->identity,.prepared=request->prepared};
    memcpy(selected.operation,request->operation,33);memcpy(selected.confirmed_title,request->confirmed_title,10);
    return default_uninstall(NULL,&selected,request->ops.current,request->ops.opaque,NULL,NULL,out);
}
