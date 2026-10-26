#!/usr/bin/env python3
"""Actual registration retirement + retained ledger filesystem/SQLite faults; no console."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
LIB=ROOT/'src/blackwire/library'
STREAM=ROOT/'src/blackwire/streamreader'
SDK=Path('/opt/ps5-payload-sdk')

def pin(path):
    body=path.read_bytes()
    return dict(bytes=len(body),sha256=hashlib.sha256(body).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build/native-dashboard') or out==ROOT/'build/native-dashboard':
        raise ValueError('Use a new scoped build/native-dashboard child')
    out.mkdir(parents=True,exist_ok=False)
    own=[LIB/'native_registration_cleanup.c',LIB/'native_registration_cleanup.h',
         Path(__file__).resolve(),HERE/'test_registration_cleanup.c',
         HERE/'test_registration_retirement.c',LIB/'native_registration_retirement.h',
         HERE/'test_registration_cleanup_io.h']
    core=[LIB/name for name in ('native_registration_cleanup.c','native_registration.c',
        'registration_db.c','source_param.c','reader_selection.c','native_prepared_target.c')]+[STREAM/'manifest.c']
    core += [HERE/'legacy_projection_boundary.c']
    all_sources=set(core+own+[ROOT/'vendor/sqlite/sqlite3.c'])
    # Record possible local header dependencies before compilation, and include
    # only actual compiler dependency inputs in the final proof.
    possible=all_sources|set(LIB.glob('*.h'))|set(STREAM.rglob('*.h'))|set((ROOT/'vendor/sqlite').glob('*.h'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').rglob('*.h'))
    before={p.resolve():pin(p) for p in possible}
    commands=[];deps=[]
    def run(label,argv,timeout=180):
        argv=list(map(str,argv))
        result=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=timeout,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append(dict(label=label,argv=argv,exitCode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode:raise RuntimeError(label+' failed; retained commands.json')
        return result.stdout
    includes=['-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include',
              '-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-g']
    hostflags=['clang-18',*flags,'-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-DBW_COPY_HOST_TEST=1',*includes]
    objects=[]
    for index,source in enumerate(core+[HERE/'test_registration_retirement.c']):
        obj=out/f'host-{index}.o';dep=obj.with_suffix('.d');deps.append(dep)
        extra=['-include',HERE/'test_registration_cleanup_io.h'] if source.name=='native_registration_cleanup.c' else []
        run('host-'+source.stem,[*hostflags,*extra,'-MD','-MF',dep,'-c',source,'-o',obj]);objects.append(obj)
    sqlite=out/'sqlite-host.o';dep=sqlite.with_suffix('.d');deps.append(dep)
    run('source-sqlite',['clang-18','-std=gnu11','-O0','-g','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION',
        '-DSQLITE_TEMP_STORE=3','-MD','-MF',dep,'-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite])
    binary=out/'registration-retirement-host'
    run('host-link',['clang-18','-fsanitize=address,undefined','-fno-omit-frame-pointer',*objects,sqlite,'-Wl,--gc-sections','-pthread','-lm','-o',binary])
    # DrvFS here does not retain 0600/0700 POSIX modes. Exercise the actual
    # strict guards on an isolated Linux temporary filesystem; do not weaken
    # production predicates to accommodate the Windows development mount.
    with tempfile.TemporaryDirectory(prefix='blackwire-registration-cleanup-') as temporary:
        tested=json.loads(run('actual-fs-sqlite-faults',[binary,Path(temporary)/'fixtures']))
    if not tested.get('passed') or tested['moduleOpens']!=tested['moduleCloses']:raise RuntimeError('Host proof incomplete')
    native=out/'native_registration_cleanup.sdk.o';dep=native.with_suffix('.d');deps.append(dep)
    run('sdk-cleanup',[SDK/'bin/prospero-clang',*flags,*includes,'-fstack-usage','-MD','-MF',dep,'-c',LIB/'native_registration_cleanup.c','-o',native])
    actual=set(own+core+[ROOT/'vendor/sqlite/sqlite3.c'])
    for dep in deps:
        for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
            actual.add((ROOT/name).resolve())
    local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
    for path in local:
        if path not in before or pin(path)!=before[path]:raise RuntimeError('Actual source input changed: '+str(path))
    source={str(p.relative_to(ROOT)):before[p] for p in sorted(local)}
    for path in local:
        dst=out/'sources'/path.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(path.read_bytes())
    external={str(p):pin(p) for p in sorted(actual-local) if p.is_file()}
    external[str((SDK/'bin/prospero-clang').resolve())]=pin((SDK/'bin/prospero-clang').resolve())
    proof=dict(atUtc=datetime.now(timezone.utc).isoformat(),passed=True,sourceBeforeAfterMatched=True,source=source,
        actualExternalCompilerInputs=external,host=tested,hostBinary=pin(binary),sdkObject=pin(native),
        commands=commands,consoleActions=0,nativeRuntimeExecuted=False,nativeELFLinked=False,
        retainedMissingSourcePolicyUnchanged=True,sharedBackendReaderFilesEdited=False,normalUninstallTransportModeled=True,
        realRegistrationAndReadonlyDbHelpersLinked=True,pendingPersistsBeforeAtMostOneCallback=True,
        preexistingTitlesNeverAdopted=True,wholeDataResetInert=True,
        exactAfterExitRetirement=True,preparedSourceAndSavesPreserved=True,
        policy0Only=True,projectedBranchesAbortIfCalled=True,projectedAuthorityQualified=False,
        releaseAndEffectiveScanRootsModeled=True,automaticApiRetry=False,
        limitations=['No console execution or actual uninstall has been performed.',
            'Current and source-collector callbacks must own the shared resident mutation reservation.',
            'The title-only upstream API has no expected-source compare-and-swap; unrelated external mutation must be excluded cooperatively.',
            'The normal uninstall response is request acceptance only; exact app-directory and read-only SQLite absence observations establish completion.',
            'Interrupted filesystem, SQLite and normal API calls have no hard execution-time bound.',
            'Only new ordinary FOLDER/V1 policy0 registrations can acquire evidence; historical titles remain unowned.',
            'Present-source retirement additionally requires exact joined-reader release and every effective scan root to exclude the source.',
            'Those production callbacks and backend integration are not implemented or deployed by this component test.'])
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(passed=True,host=tested,proof=pin(out/'proof.json'),sdkObject=pin(native))))

if __name__=='__main__':main()
