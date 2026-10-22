#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual backend workspace JSON/direct-action/unknown-latch regression.

Host-only and offline. The workspace observation helper is real maintained C;
upgrade, process/admission, registration and Sony transport are documented spies.
No executable reader bytes, historical build or console input is needed.
"""
import argparse
from datetime import datetime,timezone
import json,os,re,shlex,subprocess,tempfile
from pathlib import Path
from check_preload_bundle_upgrade import pin,ROOT,LIB,STREAM
HERE=Path(__file__).resolve().parent
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--cc',default=os.environ.get('CC','clang-18'))
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh absent output below build/')
    out.mkdir(parents=True);commands=[];deps=[]
    fixture=HERE/'test_preload_bundle_upgrade_backend.c'
    used={Path(__file__).resolve(),HERE/'check_preload_bundle_upgrade.py',HERE/'fixture_sdk.py',fixture}
    possible=set(used)
    for directory in (ROOT/'src/blackwire',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'vendor/ampr/third_party/lz4/lib'):
        possible.update(p.resolve() for p in directory.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.S'))
    before={p:pin(p) for p in possible}
    def run(label,argv):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'ERROR: AddressSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; commands retained')
        return r.stdout
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable',
        '-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread',
        '-DBW_COPY_HOST_TEST=1','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1']
    for path in (LIB,STREAM,STREAM/'diagnostics',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate'):
        flags+=['-I',path]
    sources=[fixture,LIB/'startup_runtime_idle.c',STREAM/'diagnostics/game_stop_identity.c']
    sources += [LIB/n for n in ('native_job.c','source_catalog.c','source_binding.c','source_identity.c','native_request.c','source_profile.c')]
    sources += [STREAM/'share_config.c',STREAM/'manifest.c',LIB/'preload_bundle.c']
    objects=[]
    for index,path in enumerate(sources):
        obj=out/(str(index)+'-'+path.stem+'.o');dep=obj.with_suffix('.d');deps.append(dep)
        # Only the upgrade function is a transport spy. Keep the actual
        # presentation-only workspace observer, with exact no-follow checks.
        extra=['-Dbw_preload_bundle_upgrade_checked=fixture_unused_upgrade','-Dbw_preload_bundle_upgrade_controlled=fixture_unused_controlled_upgrade'] if path.name=='preload_bundle.c' else []
        run('compile-'+path.stem,[args.cc,*flags,*extra,'-MD','-MF',dep,'-c',path,'-o',obj]);objects.append(obj)
    # Backend function-address comparisons retain unreachable production
    # preparation paths. Keep the injected test independent of SMB/reader
    # installation, and fail immediately if such a path is ever executed.
    defined,needed=set(),set()
    for obj in objects:
        for line in run('symbols-'+obj.stem,['nm','-g',obj]).splitlines():
            fields=line.split()
            if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
    includes=('native_backend.h','native_prepared_target.h','native_derived_context.h','native_batch_context.h',
              'source_directory.h','container_source.h','startup_runtime_control.h','preload_bundle.h','native_removal_request.h')
    stub=['#include "'+name+'"' for name in includes]
    stub+=['#include <smb2/smb2.h>','#include <smb2/libsmb2.h>','#include <stdlib.h>']
    declarations='\n'.join(path.read_text() for directory in (LIB,STREAM) for path in directory.glob('*.h'))
    declarations+='\n'+(ROOT/'vendor/libsmb2/include/smb2/libsmb2.h').read_text()
    for name in sorted(n for n in needed-defined if n.startswith(('bw_','smb2_'))):
        if name=='bw_preload_embedded_image':
            stub.append('const struct bw_preload_image bw_preload_embedded_image={0};');continue
        found=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
        if not found:raise ValueError('Missing explicit unexercised declaration: '+name)
        stub.append(found[0]+' { abort(); }')
    unsupported=out/'unsupported.c';unsupported.write_text('\n'.join(stub)+'\n')
    obj=out/'unsupported.o';run('unsupported-abort-stubs',[args.cc,*flags,'-Wno-unused-parameter','-Wno-c2x-extensions','-c',unsupported,'-o',obj]);objects.append(obj)
    binary=out/'host-upgrade-backend'
    run('host-link',[args.cc,'-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,
        *['-Wl,--wrap='+n for n in ('open','dup','close','fclose','fgets','clock_gettime')],'-Wl,--gc-sections','-pthread','-lm','-o',binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-reader-workspace-') as tmp:
        result=json.loads(run('actual-backend-json-actions-and-joined-latch',[binary,tmp]).splitlines()[-1])
    if not(result['passed'] and result['scenarios']==5 and result['actualJoinedBackendUnknownLatch'] and result['actualCatalogAndDirectActionBlocked']):raise RuntimeError('Backend fixture scope changed')
    for dep in deps:
        for value in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
            path=(Path(value) if Path(value).is_absolute() else ROOT/value).resolve()
            if path.is_relative_to(ROOT) and not path.is_relative_to(out):used.add(path)
    for path in used:
        if path not in before or pin(path)!=before[path]:raise RuntimeError('Actual source input changed/unpinned: '+str(path))
    proof=dict(passed=True,atUtc=datetime.now(timezone.utc).isoformat(),backend=result,
        source={str(p.relative_to(ROOT)):before[p] for p in sorted(used)},sourceBeforeAfterMatch=True,
        actualWorkspaceObserver=True,actualCatalogSerialization=True,actualDirectPlayAndSendRefusal=True,
        sdkUnits=0,hostUnits=len(sources),consoleActions=0,fullBuilds=0,nativeRuntimeQualified=False,commands=commands,
        limits=['Host-only; upgrade/process/admission/registration/Sony transport are spies.',
            'Catalog fixture seeds a VALID preparation; actual strict Send/admission is separately qualified.',
            'Workspace presence withholds all launch/preparation actions and startupReady without discarding prepared/storage metadata.',
            'No native runtime, reader compatibility or performance claim.'])
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(passed=True,backend=result,proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
