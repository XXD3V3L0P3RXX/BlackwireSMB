#!/usr/bin/env python3
"""Actual Send/core/normal adapter tests. Fixed SMP effects modeled; no console."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
LIB=ROOT/'src/blackwire/library'; STREAM=ROOT/'src/blackwire/streamreader'
spec=importlib.util.spec_from_file_location('retained_game_uninstall_fixture',HERE/'check_game_uninstall.py')
fixtures=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixtures)
pin=fixtures.pin

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--host-only',action='store_true',help='Run host fixtures without SDK compilation or native artifacts')
    parser.add_argument('--scenario',action='append',help='Run only the named host scenario; repeat for multiple scenarios')
    args=parser.parse_args()
    out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build/native-dashboard'):raise ValueError('New native-dashboard output required')
    out.mkdir(parents=True,exist_ok=False); commands=[]; deps=[]
    names=['native_game_uninstall.c','native_removed_sources.c','native_game_uninstall_remote.c','native_registration_lifecycle.c','native_registration_cleanup.c','native_cleanup_remote.c','native_registration.c','registration_db.c','native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_profile.c','source_plan.c','source_param.c','source_directory.c']
    sources=[HERE/'test_game_uninstall_remote.c']+[LIB/n for n in names]+[STREAM/'manifest.c',STREAM/'share_config.c']
    sources += [STREAM/n for n in ('container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]
    owned=[Path(__file__).resolve(),HERE/'test_game_uninstall_remote.c',HERE/'test_game_uninstall_remote_io.h']+[LIB/n for n in ['native_game_uninstall_remote.c','native_game_uninstall_remote.h','native_registration_lifecycle.c','native_registration_lifecycle.h']]
    possible=set(owned+sources)|set(LIB.glob('*.h'))|set(STREAM.rglob('*.h'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))|set((ROOT/'vendor/sqlite').glob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').rglob('*.h'))
    possible.update((STREAM/'vendor/miniz').glob('*.c'))
    possible|={LIB/n for n in ['native_send.c','native_prepare_ops.c','native_startup_upgrade.c','native_launch_admission.c']}
    possible|={HERE/n for n in ['test_game_uninstall.c','test_game_uninstall_io.h','test_fast_ready_play.c','check_game_uninstall.py']}
    possible|={STREAM/'prepare_copy.c',ROOT/'vendor/sqlite/sqlite3.c'}
    before={p.resolve():pin(p) for p in possible}
    def run(label,argv,timeout=180):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=timeout,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; evidence retained')
        return r.stdout
    common=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-g','-ffunction-sections','-fdata-sections','-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
    host=['clang-18',*common,'-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_LAUNCH_TIMING=1']
    objects=[]
    for index,p in enumerate(sources):
        obj=out/f'host-{index}.o';dep=obj.with_suffix('.d');deps.append(dep);extra=[]
        if p.name=='native_game_uninstall.c':extra=['-include',HERE/'test_game_uninstall_io.h']
        if p.name=='native_game_uninstall_remote.c':extra=['-include',HERE/'test_game_uninstall_remote_io.h']
        run('host-'+p.stem,[*host,*extra,'-MD','-MF',dep,'-c',p,'-o',obj]);objects.append(obj)
    defined=set();needed=set()
    for p in objects:
        for line in run('symbols-'+p.stem,['nm','-g',p]).splitlines():
            fields=line.split()
            if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
    missing=sorted(n for n in needed-defined if n.startswith('bw_'))
    headers=[p for d in (LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate') for p in d.glob('*.h') if p.name!='native_json.h']
    declarations='\n'.join(p.read_text() for p in headers)
    stubs=['#include "reader_preflight.h"','#include "native_register.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include <stdlib.h>']
    for name in missing:
        matches=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
        if not matches:raise ValueError('Missing abort-stub declaration '+name)
        stubs.append(matches[0]+' { abort(); }')
    stub=out/'unsupported-branches.c';stub.write_text('\n'.join(stubs)+'\n');obj=out/'unsupported.o';run('unsupported-abort-stubs',[*host,'-Wno-unused-parameter','-Wno-c2x-extensions','-c',stub,'-o',obj]);objects.append(obj)
    sqlite=out/'sqlite.o';run('source-sqlite',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite]);objects.append(sqlite)
    binary=out/'normal-uninstall-host';run('host-link',['clang-18','-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,'-Wl,--gc-sections','-Wl,--wrap=openat','-Wl,--wrap=readdir','-Wl,--wrap=pread','-Wl,--wrap=close','-pthread','-lm','-o',binary])
    scenarios=['send-only','registered','registered-no-manual','manual-lost-reply','uninstall-lost-reply','remove-mount-link','active','unknown-idle','capability-unknown','alternate-source','pkg-source','image-source','mounted-source','unknown-manual','malformed-games','settings-error','changing-settings','scan-root','foreign-registration','pkg-registration','foreign-row','manual-no-effect','uninstall-no-effect','db-row-retained','late-app-replacement','late-source-replacement','cancel-after-manual','receipt-mutated','local-mutated','owned-registration','owner-old-receipt','adapter-close-uncertain','send-only-manual-param-change','receipt-during-manual','normal-empty-cache','continue-empty-cache','continue-recreated-app','continue-recreated-db','continue-receipt-drift','continue-current-refused','continue-missing-registration','continue-foreign-cache','continue-close-uncertain']
    scenarios+=['runtime-busy','unmount-lost-reply','unmount-no-effect','unmount-capability']
    scenarios+=['final-release-app-replacement','final-release-source-replacement','maintenance-registered','maintenance-final-release-app-replacement','maintenance-final-release-source-replacement']
    if args.scenario:
        if any(name not in scenarios for name in args.scenario):raise ValueError('Unknown host scenario')
        scenarios=args.scenario
    observations=[]
    for scenario in scenarios:
        with tempfile.TemporaryDirectory(prefix='blackwire-normal-uninstall-') as tmp:
            area=Path(tmp);fixture_kind=scenario if scenario in ('foreign-registration','pkg-registration','foreign-row') else 'normal' if scenario in ('send-only','owned-registration','owner-old-receipt','send-only-manual-param-change') else 'registered'
            fixtures.fixture(area,fixture_kind);observations.append(json.loads(run('case-'+scenario,[binary,area,scenario]).splitlines()[-1]))
    native=[];native_symbols={};files_adapter=None;files_symbols=[]
    if not args.host_only:
        sdk=Path('/opt/ps5-payload-sdk')
        for name in ['native_game_uninstall_remote.c','native_registration_lifecycle.c','native_cleanup_remote.c','native_game_uninstall.c','native_removed_sources.c']:
            p=LIB/name;obj=out/(p.stem+'.sdk.o');dep=obj.with_suffix('.d');deps.append(dep);run('sdk-'+p.stem,[sdk/'bin/prospero-clang',*common,'-MD','-MF',dep,'-c',p,'-o',obj]);native.append(obj)
            native_symbols[name]=run('sdk-imports-'+p.stem,['nm','-u',obj]).splitlines()
            if any(line.split()[-1] in ('openat','fstatat') for line in native_symbols[name] if line.split()):raise RuntimeError('Raw SDK at-stub import in '+name)
        files_adapter=out/'files-adapter-root.sdk.o'
        run('sdk-files-only-root',[sdk/'bin/ld.lld','-r','--gc-sections','--undefined=bw_native_game_uninstall_files_normal',out/'native_game_uninstall_remote.sdk.o','-o',files_adapter])
        files_symbols=run('sdk-files-only-symbols',['nm',files_adapter]).splitlines()
        forbidden={'bw_native_game_uninstall','bw_sm_uninstall','bw_sm_unmount','gur_registration','gur_safe','bw_nrc_remote_sources'}
        if any(line.split()[-1] in forbidden for line in files_symbols if line.split()):raise RuntimeError('Normal registration/mutation path retained in files-only adapter root')
    actual=set(owned+sources)
    for dep in deps:
        for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/name).resolve())
    local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
    for p in local:
        if p not in before or pin(p)!=before[p]:raise RuntimeError('Actual input drift '+str(p))
    for p in local:
        target=out/'frozen-source'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.read_bytes())
    proof=dict(passed=True,atUtc=datetime.now(timezone.utc).isoformat(),cases=sum(x['cases'] for x in observations),checks=sum(x['checks'] for x in observations),observations=observations,source={str(p.relative_to(ROOT)):before[p] for p in sorted(local)},sourceBeforeAfterMatch=True,hostBinary=pin(binary),hostOnly=args.host_only,sdkObjects={p.name:pin(p) for p in native},sdkUndefinedSymbols=native_symbols,rawSdkAtStubImportsAbsent=None if args.host_only else True,filesOnlyAdapter=pin(files_adapter) if files_adapter else None,filesOnlySymbols=files_symbols,filesOnlyNoNormalMutationPath=None if args.host_only else True,sdkInputs={str(p):pin(p) for p in sorted(actual-local) if p.is_file()},unsupportedAbortStubs=missing,actualSendProducedGenerations=True,actualDeletionCore=True,actualReadonlySqlite=True,normalSmpEffectsModeled=True,readbackClockModeled=True,sourceOriginalAndSavePreserved=True,consoleActions=0,fullBuilds=0,commands=commands)
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=proof['cases'],checks=proof['checks'],proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
