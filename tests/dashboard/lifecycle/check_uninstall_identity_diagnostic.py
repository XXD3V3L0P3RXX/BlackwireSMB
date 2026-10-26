#!/usr/bin/env python3
"""Actual uninstall identity/refusal diagnostics, host sanitizers and SDK compile."""
from pathlib import Path
import argparse, hashlib, json, os, re, shlex, subprocess, tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
LIB=ROOT/'src/blackwire/library'
def pin(p):
    b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--host-only',action='store_true');args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh absent build output')
    out.mkdir(parents=True);commands=[];before={p.resolve():pin(p) for d in [LIB,ROOT/'src/blackwire/streamreader',HERE] for p in d.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py')}
    def run(label,argv):
        r=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,timeout=120,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'ERROR: AddressSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; see commands.json')
        return r.stdout
    flags=['-std=gnu11','-DBW_SEND_DIAGNOSTIC=1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-I',LIB,'-I',ROOT/'src/blackwire/streamreader','-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
    log=out/'identity.log';binary=out/'identity';dep=out/'host.d'
    run('host-build',['clang-18',*flags,'-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_SEND_LOG_PATH="'+str(log)+'"','-MD','-MF',dep,HERE/'test_uninstall_identity_diagnostic.c','-Wl,--gc-sections','-pthread','-o',binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-uninstall-identity-') as tmp:result=json.loads(run('host-cases',[binary,tmp]).splitlines()[-1])
    sdk=out/'uninstall.sdk.o';sdkdep=out/'sdk.d';dependencies=[dep]
    if not args.host_only:
        run('actual-sdk-compile',['/opt/ps5-payload-sdk/bin/prospero-clang',*flags,'-MD','-MF',sdkdep,'-c',LIB/'native_game_uninstall.c','-o',sdk]);dependencies.append(sdkdep)
    used={Path(__file__).resolve(),HERE/'test_uninstall_identity_diagnostic.c'};external={}
    for d in dependencies:
        for name in shlex.split(d.read_text().replace('\\\n',' ').split(':',1)[1]):
            p=Path(name).resolve()
            if p.is_relative_to(out):continue
            if p in before:used.add(p)
            elif p.is_relative_to(ROOT):external[str(p.relative_to(ROOT))]=pin(p)
            else:external[str(p)]=pin(p)
    if any(pin(p)!=before[p.resolve()] for p in used):raise ValueError('Source changed during verification')
    proof={'passed':True,'host':result,'sourceBeforeAfterMatch':True,'source':{str(p.relative_to(ROOT)):before[p.resolve()] for p in sorted(used)},'externalCompilerInputs':external,'sdkObject':pin(sdk) if not args.host_only else None,'sdkUnits':0 if args.host_only else 1,'hostOnly':args.host_only,'hostBinary':pin(binary),'consoleActions':0,'fullBuilds':0}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,**result,'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
