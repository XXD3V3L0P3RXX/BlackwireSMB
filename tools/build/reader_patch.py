"""Source-only integration into exact upstream virtual FD/AIO ownership."""
from pathlib import Path
def replace(text,old,new,count=1):
    assert text.count(old)==count,(old,text.count(old),count)
    return text.replace(old,new)
def function(text,start):
    b=text.index(start);e=text.index('{',b)+1;n=1
    while n:
        n+=(text[e]=='{')-(text[e]=='}');e+=1
    return text[b:e]
def apply(root:Path):
    exports=root/'src/sceampr_exports.cpp';s=exports.read_text()
    s=replace(s,'#include <cstdint>','#include "runtime.h"\n#include <cstdint>')
    s=replace(s,'    return amprInstallLibkernelHooks();',
        '    int result=bp_prepare_originals_and_metadata();\n    if(result==0){result=amprInstallLibkernelHooks();bp_init_report(BP_INIT_INSTALL,result);}\n    if(result!=0){int released=bp_release_unpatched_metadata();bp_init_report(BP_INIT_RELEASE,released);(void)bp_home_startup_failure(released);}\n    return result;')
    s=replace(s,'int module_stop(size_t args, const void* argp) {','int module_stop(size_t args, const void* argp) {\n    bp_runtime_cancel();')
    s=replace(s,'    return amprUninstallLibkernelHooks();','    if (bp_runtime_release() != 0) return -1;\n    return amprUninstallLibkernelHooks();')
    exports.write_text(s)
    crt=root/'ps5/crt/crtbeginS.c';s=crt.read_text();s=replace(s,'    if (__cxa_finalize) {','    if (result != 0) return result;\n    if (__cxa_finalize) {');crt.write_text(s)
    pack=root/'src/ampr_emu_pack.cpp';s=pack.read_text();at=s.index('#include ');s=s[:at]+'#include "runtime.h"\n'+s[at:]
    s=replace(s,'struct VirtualFdSlot {\n','struct VirtualFdSlot {\n    bool bpRemote{}; uint32_t bpId{}; uint64_t bpSize{}; int64_t bpMtime{};\n')
    s=replace(s,'struct VirtualAioSlot {\n','struct VirtualAioSlot {\n    bool bpRemote{}; uint32_t bpId{};\n')
    s=replace(s,'    slot.request = {};\n    slot.fileId = 0;','    slot.request = {};\n    slot.fileId = 0; slot.bpRemote=false; slot.bpId=0;')
    s=replace(s,'            slot.fileId = fileId;\n','            slot.fileId = fileId; slot.bpRemote=false; slot.bpId=0; slot.bpSize=0; slot.bpMtime=0;\n')
    open_at='''    if (!handled || fileId == 0 || !entry.path) {
        return SCE_KERNEL_ERROR_ENOENT;
    }
'''
    s=replace(s,open_at,open_at+'''    uint32_t bpId=0;
    const int bpMatch=bp_runtime_match(entry.path,entry.pathLength,entry.size,&bpId);
    if (bpMatch<0) { *handled=true; return SCE_KERNEL_ERROR_EIO; }
    if (bpMatch==0) return SCE_KERNEL_ERROR_ENOENT; // admitted LOCAL or unrelated: unchanged physical path
    *handled=true;
    if (!pack_open_flags_supported(flags) || (processOrigin && !amprPackProcessOpenHooksReady())) return SCE_KERNEL_ERROR_EACCES;
    PackState& remoteState=pack_state(); AmprLockGuard remoteLock(remoteState.m);
    if (remoteState.workerStopRequested.load()) return SCE_KERNEL_ERROR_ECANCELED;
    uint32_t remoteSlot=allocate_virtual_fd_locked(remoteState,fileId);
    if(remoteSlot==kInvalidIndex)return SCE_KERNEL_ERROR_EMFILE;
    VirtualFdSlot& remote=remoteState.virtualFds[remoteSlot];
    remote.bpRemote=true;remote.bpId=bpId;remote.bpSize=entry.size;remote.bpMtime=entry.mtime;remote.processOpen=processOrigin;
    return encode_virtual_fd(remoteSlot,remote.generation);
''')
    # The unmodified old pack path remains in the TU for original non-remote
    # public helpers, but this explicit reader's indexed opens return above.
    s=replace(s,'!slot.active || slot.generation != generation || !state.header ||\n            slot.fileId == 0 || slot.fileId > state.header->fileCount',
        '!slot.active || slot.generation != generation || (!slot.bpRemote && (!state.header ||\n            slot.fileId == 0 || slot.fileId > state.header->fileCount))',6)
    s=replace(s,'logicalSize = state.files[slot.fileId - 1u].logicalSize;',
        'logicalSize = slot.bpRemote ? slot.bpSize : state.files[slot.fileId - 1u].logicalSize;',5)
    stat_at='''        const AmprPackFileRecord& file = state.files[slot.fileId - 1u];
        fill_synthetic_stat(stat, false, file.logicalSize, file.mtime,'''
    s=replace(s,stat_at,'''        if(slot.bpRemote){fill_synthetic_stat(stat,false,slot.bpSize,slot.bpMtime,(uint64_t)slot.bpId+1);return 0;}
'''+stat_at)
    s=replace(s,'            aio.fileId = fdSlot.fileId;','            aio.fileId = fdSlot.fileId; aio.bpRemote=fdSlot.bpRemote; aio.bpId=fdSlot.bpId;')
    s=replace(s,'            if (!state.header || !state.files || aio.fileId == 0 ||\n                aio.fileId > state.header->fileCount) {',
        '            if (!aio.bpRemote && (!state.header || !state.files || aio.fileId == 0 ||\n                aio.fileId > state.header->fileCount)) {')
    s=replace(s,'classify_work(state.files[aio.fileId - 1u], aio.request);','aio.bpRemote ? PackWorkClass::Balanced : classify_work(state.files[aio.fileId - 1u], aio.request);')
    name='static bool try_process_virtual_read_cache_only('
    old=function(s,name);pos=old.index('{')+1;new=old[:pos]+'\n    if(job.bpRemote)return false;\n'+old[pos:];s=replace(s,old,new)
    name='static void* pack_worker_entry('
    old=function(s,name);new=replace(old,'    PackState& state = pack_state();','    (void)bp_worker_initialize();\n    PackState& state = pack_state();')
    new=replace(new,'    (void)scePthreadSetprio(scePthreadSelf(), AMPR_EMU_PACK_WORKER_PRIORITY);',
        '    const int bpPriority=bp_worker_priority(AMPR_EMU_PACK_WORKER_PRIORITY);\n    const int bpPriorityRc=scePthreadSetprio(scePthreadSelf(), bpPriority);\n    bp_worker_priority_report(AMPR_EMU_PACK_WORKER_PRIORITY,bpPriority,bpPriorityRc);')
    new=replace(new,'                return nullptr;','                lock.unlock(); bp_worker_finish(); return nullptr;')
    new=replace(new,'        int64_t returnValue = 0;','        bp_read_witness bpWitness{}; bool bpPublished=false;\n        int64_t returnValue = 0;')
    new=replace(new,'        const PipelineStep step = advance_virtual_read_aio(\n            job, *worker, *pipeline, &returnValue, &pipelineError);',
        '''        PipelineStep step;
        if(job.bpRemote){
            if(job.cancelRequested.load() || state.workerStopRequested.load()){
                pipelineError=SCE_KERNEL_ERROR_ECANCELED;step=PipelineStep::Failed;
            }else if(job.request.offset<0 || !job.request.result){
                pipelineError=SCE_KERNEL_ERROR_EINVAL;step=PipelineStep::Failed;
            }else{
                int64_t result=bp_runtime_read(job.bpId,(uint64_t)job.request.offset,job.request.buf,job.request.nbyte,&job.cancelRequested,&bpWitness);
                if(job.cancelRequested.load() || state.workerStopRequested.load())result=-ECANCELED;
                if(result<0){pipelineError=ampr_sce_errno_from_posix((int)-result);step=PipelineStep::Failed;}
                else{returnValue=result;step=PipelineStep::Complete;}
            }
        }else step=advance_virtual_read_aio(job,*worker,*pipeline,&returnValue,&pipelineError);''')
    new=replace(new,'''                    complete_virtual_job(
                        state, index, returnValue,
                        SCE_KERNEL_AIO_STATE_COMPLETED);
                    break;''','''                    complete_virtual_job(
                        state, index, returnValue,
                        SCE_KERNEL_AIO_STATE_COMPLETED);
                    // BWREAD_COMPLETION_WITNESS_BEGIN
                    bpPublished=job.bpRemote && returnValue>0 &&
                        !job.cancelRequested.load() && !state.workerStopRequested.load();
                    // BWREAD_COMPLETION_WITNESS_END
                    break;''')
    new=replace(new,'        if (notifyWorkers) state.queueCv.notify_all();','        if (notifyWorkers) state.queueCv.notify_all();\n        if(bpPublished)bp_runtime_report_read(&bpWitness,returnValue);')
    s=replace(s,old,new)
    pack.write_text(s)
    index=root/'src/ampr_emu_index.cpp';s=index.read_text();at=s.index('#include ');s=s[:at]+'#include "runtime.h"\n'+s[at:]
    s=replace(s,'            (void)ampr_pack_ensure_manifest_ready_safe();','            if (!bp_runtime_metadata_ready()) work.available = false;')
    index.write_text(s)
    hooks=root/'src/ampr_libkernel_hook.cpp';s=hooks.read_text();at=s.index('#include ');s=s[:at]+'#include "runtime.h"\n#include "ampr_emu_index.h"\n'+s[at:]
    line='constexpr size_t kHookCount = sizeof(g_hooks) / sizeof(g_hooks[0]);'
    s=replace(s,line,line+'\nvoid* g_bpPreparedTargets[kHookCount]{};\nbool g_bpPrepared{};')
    prepare='''int bp_prepare_originals_and_metadata() {
    // Reuse the existing identity logger/symbol lookup, outside HookLock.
    // No alternate sink: a missing marker still proves no failure cause.
    emit_module_identity_system_log(kKnownLibkernelHandle);
    bp_init_report(BP_INIT_ENTER,0);
    // Resolve only the SAME configured target table; no patch, dependency load,
    // worker, network or filesystem work while HookLock is held.
    bool reused=false;
    int result=[&]() {
        HookLock lock;
        if (hooks_installed()) return -1;
        if (g_bpPrepared) {reused=true;return bp_runtime_metadata_ready() && ampr_index_is_resident_ready() ? 0 : -1;}
        for (size_t i=0;i<kHookCount;i++) if(g_hooks[i].detour.installed) return -1;
        for (size_t i=0;i<kHookCount;i++) {
            g_bpPreparedTargets[i]=resolve_libkernel_symbol(kKnownLibkernelHandle,g_hooks[i].symbol);
            if(!g_bpPreparedTargets[i] && g_hooks[i].mandatory) return -1;
        }
        for (size_t i=0;i<kHookCount;i++)
            store_original_slot(&g_amprOriginalLibkernelById[i],g_bpPreparedTargets[i]);
        return 0;
    }();
    bp_init_report(BP_INIT_PREPASS,result);
    if(result!=0||reused)return result;
    // AUTOBUILD=0 and DEBUG/COMMAND=0 are required by this reader build.
    // Saved-original config/map and prebuilt index reads, plus at most one
    // bounded fixed-loopback home claim, occur outside HookLock. No SMB here.
    bool ready=bp_metadata_prepare();bp_init_report(BP_INIT_METADATA,ready?0:-1);
    if(!ready)return -1;
    ready=ampr_index_ensure_ready_safe();bp_init_report(BP_INIT_INDEX,ready?0:-1);
    if(!ready)return -1;
    result=[&]() {
        HookLock lock;
        if(hooks_installed())return -1;
        for(size_t i=0;i<kHookCount;i++)
            if(g_hooks[i].detour.installed || resolve_libkernel_symbol(kKnownLibkernelHandle,g_hooks[i].symbol)!=g_bpPreparedTargets[i])return -1;
        g_bpPrepared=true;
        return 0;
    }();
    bp_init_report(BP_INIT_TARGETS,result);
    return result;
}

int bp_release_unpatched_metadata() {
    {
        HookLock lock;
        if(hooks_installed())return -1;
        for(size_t i=0;i<kHookCount;i++)
            if(g_hooks[i].detour.installed || g_hooks[i].detour.trampoline)return -1;
#if AMPR_EMU_APR_LOCAL_EQUEUE
        for(size_t i=0;i<kExternalEqueueHookCount;i++)
            if(g_externalEqueueHooks[i].detour.installed || g_externalEqueueHooks[i].detour.trampoline)return -1;
        if(g_moduleLoadObserverDetour.installed || g_moduleLoadObserverDetour.trampoline)return -1;
#endif
        // Block a fresh installer before dropping HookLock. Existing match/
        // worker ownership is still checked by runtime release outside it.
        g_bpPrepared=false;
    }
    return bp_runtime_release();
}

'''
    s=replace(s,'static int ampr_install_libkernel_hooks_for_module(int libkernelHandle) {',prepare+'static int ampr_install_libkernel_hooks_for_module(int libkernelHandle) {')
    check='''    // BP_PREPATCH_REVALIDATION_BEGIN
    if(!g_bpPrepared || !bp_runtime_metadata_ready() || !ampr_index_is_resident_ready())return -1;
    for(size_t i=0;i<kHookCount;i++)
        if(resolve_libkernel_symbol(libkernel,g_hooks[i].symbol)!=g_bpPreparedTargets[i])return -1;
    // BP_PREPATCH_REVALIDATION_END
'''
    s=replace(s,'    reset_deferred_hook_log();',check+'\n    reset_deferred_hook_log();')
    s=replace(s,'        void* target = resolve_libkernel_symbol(libkernel, hook.symbol);','        void* target = resolve_libkernel_symbol(libkernel, hook.symbol);\n        if(target != g_bpPreparedTargets[hookIndex]) target=nullptr;')
    hooks.write_text(s)
