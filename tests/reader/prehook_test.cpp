/* Executes exact generated prehook/prepatch functions. Public resolution and
 * index/FS observations are modeled; no detour or native-loader claim. */
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
static unsigned checks,cases,held,metadata_calls,index_calls,releases;
#include "init-phases-under-test.h"
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"prehook line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct HookLock{HookLock(){CHECK(held==0);held++;}~HookLock(){held--;}};
struct Detour{bool installed;void *trampoline;};
struct Hook{const char *symbol;bool mandatory;Detour detour;};
constexpr size_t kHookCount=3;constexpr int kKnownLibkernelHandle=0x2001;
static Hook g_hooks[kHookCount]={{"open",true,{}},{"pread",false,{}},{"close",true,{}}};
static int tokens[3];static void *targets[3],*g_bpPreparedTargets[3],*g_amprOriginalLibkernelById[3];
static bool g_bpPrepared,installed,metadata_ready,index_ready,fail_metadata,fail_index;
static bool change_target,late_residual,fail_release;
static unsigned install_mode;
#define AMPR_EMU_APR_LOCAL_EQUEUE 1
constexpr size_t kExternalEqueueHookCount=1;
static Hook g_externalEqueueHooks[1]={{"external",false,{}}};
static Detour g_moduleLoadObserverDetour{};
static bool hooks_installed(){return installed;}
static unsigned phase_calls;
static void emit_module_identity_system_log(int handle){CHECK(!held&&handle==kKnownLibkernelHandle);}
static void bp_init_report(unsigned phase,int64_t result){CHECK(!held&&phase>=BP_INIT_ENTER&&phase<=BP_INIT_RELEASE);(void)result;phase_calls++;}
static bool bp_runtime_metadata_ready(){return metadata_ready;}
static bool ampr_index_is_resident_ready(){return index_ready;}
static void *resolve_libkernel_symbol(int handle,const char *name){CHECK(held==1&&handle==kKnownLibkernelHandle);for(size_t i=0;i<3;i++)if(name==g_hooks[i].symbol)return targets[i];return nullptr;}
static void store_original_slot(void **slot,void *p){CHECK(held==1);*slot=p;}
static bool bp_metadata_prepare(){CHECK(!held&&!installed);metadata_calls++;for(size_t i=0;i<3;i++)CHECK(g_amprOriginalLibkernelById[i]==targets[i]);metadata_ready=!fail_metadata;return metadata_ready;}
static bool ampr_index_ensure_ready_safe(){CHECK(!held&&!installed&&metadata_ready);index_calls++;index_ready=!fail_index;if(change_target)targets[1]=nullptr;if(late_residual)g_hooks[1].detour={true,&tokens[1]};return index_ready;}
static int bp_runtime_release(){CHECK(!held&&!g_bpPrepared);releases++;if(fail_release)return -1;metadata_ready=false;return 0;}
#include "prehook-under-test.h"
#include "failure-release-under-test.h"
static int amprInstallLibkernelHooks(){CHECK(metadata_ready&&g_bpPrepared&&!held);if(install_mode==2)g_hooks[0].detour={true,&tokens[0]};if(install_mode)return -1;installed=true;return 0;}
static int bp_home_startup_failure(int result){CHECK(!held);(void)result;return 0;}
#include "module-start-under-test.h"
static int prepatch(){HookLock lock;const int libkernel=kKnownLibkernelHandle;
#include "prepatch-under-test.h"
return 0;}
static void reset(){held=metadata_calls=index_calls=releases=install_mode=0;g_bpPrepared=installed=metadata_ready=index_ready=fail_metadata=fail_index=change_target=late_residual=fail_release=false;for(size_t i=0;i<3;i++){targets[i]=&tokens[i];g_bpPreparedTargets[i]=g_amprOriginalLibkernelById[i]=nullptr;g_hooks[i].detour={};}g_externalEqueueHooks[0].detour={};g_moduleLoadObserverDetour={};}
int main(){
    reset();cases++;CHECK(bp_prepare_originals_and_metadata()==0);CHECK(g_bpPrepared&&metadata_calls==1&&index_calls==1&&releases==0);CHECK(prepatch()==0);CHECK(bp_prepare_originals_and_metadata()==0&&metadata_calls==1);
    targets[1]=nullptr;CHECK(prepatch()==-1);CHECK(!installed&&g_amprOriginalLibkernelById[1]==&tokens[1]);
    reset();cases++;targets[0]=nullptr;CHECK(bp_prepare_originals_and_metadata()==-1);CHECK(!metadata_calls&&!index_calls&&!g_bpPrepared);
    reset();cases++;fail_metadata=true;CHECK(module_start(0,nullptr)==-1);CHECK(metadata_calls==1&&!index_calls&&releases==1&&!g_bpPrepared);CHECK(prepatch()==-1);
    reset();cases++;fail_index=true;CHECK(module_start(0,nullptr)==-1);CHECK(metadata_calls==1&&index_calls==1&&releases==1&&!g_bpPrepared);CHECK(prepatch()==-1);
    reset();cases++;g_hooks[0].detour.installed=true;g_amprOriginalLibkernelById[0]=&tokens[2];CHECK(bp_prepare_originals_and_metadata()==-1);CHECK(g_amprOriginalLibkernelById[0]==&tokens[2]&&!metadata_calls);
    reset();cases++;targets[1]=nullptr;CHECK(bp_prepare_originals_and_metadata()==0);CHECK(g_amprOriginalLibkernelById[1]==nullptr&&prepatch()==0);
    reset();cases++;change_target=true;CHECK(module_start(0,nullptr)==-1);CHECK(releases==1&&!g_bpPrepared&&!metadata_ready);
    reset();cases++;late_residual=true;CHECK(module_start(0,nullptr)==-1);CHECK(releases==0&&metadata_ready&&g_hooks[1].detour.trampoline==&tokens[1]);
    reset();cases++;install_mode=1;CHECK(module_start(0,nullptr)==-1);CHECK(releases==1&&!g_bpPrepared&&!metadata_ready);
    reset();cases++;install_mode=2;CHECK(module_start(0,nullptr)==-1);CHECK(releases==0&&g_bpPrepared&&metadata_ready);
    reset();cases++;install_mode=1;fail_release=true;CHECK(module_start(0,nullptr)==-1);CHECK(releases==1&&!g_bpPrepared&&metadata_ready&&prepatch()==-1);
    reset();cases++;CHECK(bp_prepare_originals_and_metadata()==0);g_externalEqueueHooks[0].detour.trampoline=&tokens[0];CHECK(bp_release_unpatched_metadata()==-1&&releases==0&&metadata_ready);
    reset();cases++;CHECK(bp_prepare_originals_and_metadata()==0);g_moduleLoadObserverDetour.trampoline=&tokens[1];CHECK(bp_release_unpatched_metadata()==-1&&releases==0&&metadata_ready);
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualGeneratedPrepass\":true,\"targetTableModelOnly\":true,\"nativeLoaderProved\":false}\n",cases,checks);return 0;
}
