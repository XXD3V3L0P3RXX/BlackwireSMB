#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline actual default-Play worker and preparation-state regressions.

Source admission, registration, native idle and reader execution are explicit
spies; the maintained backend worker, negative history and state JSON are real.
No historical binary, game data or console input is required.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shlex,subprocess,sys

sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
LIB=ROOT/'src/blackwire/library'
STREAM=ROOT/'src/blackwire/streamreader'

def pin(path):
    raw=path.read_bytes();return dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--cc',default=os.environ.get('CC','clang-18'))
    parser.add_argument('--host-only',action='store_true',help='Run all 19 host worker/state cases without SDK compilation')
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():
        raise ValueError('Use a fresh output below this repository build directory')
    out.mkdir(parents=True)
    fixture=HERE/'test_play_preparation_failure.c'
    sources=[fixture,LIB/'startup_runtime_idle.c',STREAM/'diagnostics/game_stop_identity.c']
    sources += [LIB/n for n in ('native_job.c','source_catalog.c','source_binding.c','source_identity.c','native_request.c','source_profile.c')]
    sources += [STREAM/'share_config.c',STREAM/'manifest.c']
    used={Path(__file__).resolve(),HERE/'fixture_sdk.py',HERE/'source_ampr_admission_model.h',ROOT/'tools/build/app-recipe.json',LIB/'native_backend.c',ROOT/'tools/build/reader_patch.py',*sources}
    possible=set(used)
    for base in (ROOT/'src/blackwire',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'vendor/ampr/third_party/lz4/lib'):
        possible.update(p.resolve() for p in base.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.S'))
    before={p:pin(p) for p in possible};commands=[];dependencies=[]
    def run(label,argv):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,
            env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1','ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1',
                 'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'ERROR: AddressSanitizer|runtime error:',r.stderr):
            raise RuntimeError(label+' failed; evidence retained')
        return r.stdout
    # state() compares the default prepare function's address. This suite
    # executes only real Play workers; a default Send is an aborting link spy.
    # Retain the actual Play and state implementations unchanged.
    sys.path.insert(0,str(ROOT/'tools/build'))
    from reader_patch import function
    original=(LIB/'native_backend.c').read_text()
    prepare=function(original,'static int prepare(')
    spy='{'+''.join('(void)'+p+';' for p in ('opaque','config','selection','current','guard','observe','observer','out'))+'abort();}'
    patched=original.replace(prepare,prepare.split('{',1)[0]+spy)
    if function(original,'static int play(')!=function(patched,'static int play('):
        raise RuntimeError('Actual default Play body changed in fixture')
    def state_body(text):
        start=text.index('static int state(');end=text.index('\nstatic int text(',start)
        return text[start:end]
    if state_body(original)!=state_body(patched):raise RuntimeError('Actual state serializer changed in fixture')
    (out/'native_backend.c').write_text(patched)
    includes=(out,LIB,STREAM,STREAM/'diagnostics',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate')
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable',
        '-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread',
        '-DBW_COPY_HOST_TEST=1','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1']
    flags += [str(v) for directory in includes for v in ('-I',directory)]
    objects=[]
    for i,path in enumerate(sources):
        obj=out/(str(i)+'-'+path.stem+'.o');dep=obj.with_suffix('.d');dependencies.append(dep)
        run('host-'+path.stem,[args.cc,*flags,'-MD','-MF',dep,'-c',path,'-o',obj]);objects.append(obj)
    binary=out/'play-preparation-test'
    run('host-link',[args.cc,'-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,
        '-Wl,--wrap=bw_idle_log_cache_close','-Wl,--gc-sections','-pthread','-lm','-o',binary])
    result=json.loads(run('actual-default-play-joined-worker-and-state-json',[binary]).splitlines()[-1])
    if not(result['passed'] and result['cases']==19 and result['actualDefaultPlayAndJoinedWorker'] and result['actualStateJson'] and result['controlledBundleCheckpoints']>0):
        raise RuntimeError('Fixture scope changed')
    sdk=None
    if not args.host_only:
        from fixture_sdk import production_rows
        template=next(row for row in production_rows(ROOT,out/'sdk-template') if '-c' in row['argv'] and
            Path(row['argv'][row['argv'].index('-c')+1]).name=='native_backend.c')
        argv=template['argv'].copy();sdk=out/'native_backend.sdk.o';dep=sdk.with_suffix('.d');dependencies.append(dep)
        argv[argv.index('-o')+1]=str(sdk);argv[argv.index('-MF')+1]=str(dep)
        run('production-sdk-native-backend',argv)
    for dep in dependencies:
        for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
            path=(Path(name) if Path(name).is_absolute() else ROOT/name).resolve()
            if path.is_relative_to(ROOT) and not path.is_relative_to(out):used.add(path)
    for path in used:
        if path not in before or pin(path)!=before[path]:raise RuntimeError('Actual source input changed or was unpinned')
    proof=dict(passed=True,backend=result,sourceBeforeAfterMatch=True,source={str(p.relative_to(ROOT)):before[p] for p in sorted(used)},
        sdkUnits=int(sdk is not None),hostUnits=len(sources),sdkObject=pin(sdk) if sdk else None,hostOnly=args.host_only,
        defaultPrepareAbortingLinkSpy=True,actualDefaultPlayBodyUnchanged=True,actualStateBodyUnchanged=True,
        consoleActions=0,fullBuilds=0,nativeRuntimeQualified=False,
        limits=['Actual default Play, joined worker, negative revalidation history and state JSON.',
                'Source admission, native idle, transport/registration and reader execution are documented spies.',
                'No console compatibility or performance claim.'])
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(passed=True,backend=result,sdkUnits=int(sdk is not None),hostOnly=args.host_only,proof=pin(out/'proof.json'))))

if __name__=='__main__':main()
