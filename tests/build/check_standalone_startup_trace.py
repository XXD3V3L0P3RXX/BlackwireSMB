#!/usr/bin/env python3
"""Qualify actual standalone trace helpers; no payload load or console access."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
def pin(path):
    raw=path.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--host-only',action='store_true');a=p.parse_args();out=a.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh build output')
    out.mkdir(parents=True);source=ROOT/'src/blackwire/library/main.c';fixture=ROOT/'tests/build/test_standalone_startup_trace.c'
    inputs=[source,fixture,Path(__file__),ROOT/'tests/dashboard/lifecycle/fixture_sdk.py'];before={x.relative_to(ROOT).as_posix():pin(x) for x in inputs}
    text=source.read_text();start=text.index('/* Startup-only, best-effort diagnostic checkpoints.');end=text.index('\nstatic void notify(',start)
    section=out/'startup-trace-under-test.h';section.write_text(text[start:end]+'\n');commands=[]
    def run(label,argv,expected=0,cwd=None):
        r=subprocess.run([str(x) for x in argv],cwd=cwd or ROOT,capture_output=True,text=True,timeout=90,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1','PYTHONDONTWRITEBYTECODE':'1'})
        (out/(label+'.log')).write_text(r.stdout+r.stderr);commands.append({'label':label,'argv':[str(x) for x in argv],'exitCode':r.returncode});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if (r.returncode==0)!=(expected==0):raise RuntimeError(label+' returned unexpected status; inspect saved log')
        return r.stdout
    common=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',out,fixture,'-Wl,--wrap=send']
    stdout=out/'trace-stdout';run('compile-stdout',[*common,'-DBW_STARTUP_TRACE=1','-DBW_STARTUP_TRACE_STDOUT_ONLY=1','-o',stdout])
    observations=[]
    for name in ('stream','closed-peer','closed-fd','non-socket','backpressure','limit','long'):
        observations.append(json.loads(run('case-'+name,[stdout,name]).splitlines()[-1]))
    symbols=run('stdout-symbols',['nm','-a',stdout])
    if any(s in symbols for s in ('trace_filesystem_','startup_trace_fd')):raise RuntimeError('Stdout-only binary retained trace filesystem state or calls')
    if b'startup-core-01' in stdout.read_bytes():raise RuntimeError('Stdout-only binary retained console trace path')
    off=out/'trace-off';run('compile-off',[*common,'-o',off]);observations.append(json.loads(run('case-off',[off,'off']).splitlines()[-1]))
    bad=out/'trace-invalid';run('compile-invalid-mode',[*common,'-DBW_STARTUP_TRACE_STDOUT_ONLY=1','-o',bad],expected=1)
    normal=out/'trace-normal'
    # Use an owned native Linux temporary directory: a Windows-mounted output
    # can report emulated 0777 modes for a correctly requested 0600 creation.
    with tempfile.TemporaryDirectory(prefix='bw-trace-') as directory:
        run('compile-files',[*common,'-DBW_STARTUP_TRACE=1','-DBW_STARTUP_TRACE_STDOUT_ONLY=0','-DBW_STARTUP_TRACE_DIRECTORY='+json.dumps(directory),'-o',normal])
        for name in ('files','file-short','file-partial-enospc','file-partial-zero','file-partial-eintr','file-eintr','file-zero','file-overlong','file-close-error'):
            observations.append(json.loads(run('case-'+name,[normal,name]).splitlines()[-1]))
    sdk=[]
    if not a.host_only:
        sys.path.insert(0,str(ROOT/'tests/dashboard/lifecycle'));from fixture_sdk import production_rows
        rows=[r for r in production_rows(ROOT,out/'sdk-template') if r['label']=='sdk-main']
        if len(rows)!=1:raise RuntimeError('Actual main SDK recipe missing/ambiguous')
        base=rows[0]['argv'];base=base[:base.index('-MD')]
        for name,flags in [('stdout',['-DBW_STARTUP_TRACE=1','-DBW_STARTUP_TRACE_STDOUT_ONLY=1']),('files',['-DBW_STARTUP_TRACE=1','-DBW_STARTUP_TRACE_STDOUT_ONLY=0']),('off',[])]:
            target=out/('sdk-main-'+name+'.o');argv=[*base,'-DBW_WEB_ONLY=1','-DBW_MEDIA_PUBLIC_TRIAL=1','-DBW_HOME_STARTUP=1','-DBW_TEST_PC_BROWSER_ONLY=1',*flags,'-c',source,'-o',target]
            run('sdk-main-'+name,argv,cwd=source.parent);sdk.append({'mode':name,'object':pin(target)})
        strings=(out/'sdk-main-stdout.o').read_bytes()
        if b'BW core-isolation-01' not in strings or b'backend.create.before' not in strings or b'startup-core-01' in strings:raise RuntimeError('Actual SDK stdout object marker/file-path contract differs')
    after={x.relative_to(ROOT).as_posix():pin(x) for x in inputs}
    if before!=after:raise RuntimeError('Source changed during focused qualification')
    proof={'passed':True,'cases':len(observations),'checks':sum(x['checks'] for x in observations),'host':observations,'sdkObjects':sdk,'source':before,'actualTraceSection':pin(section),'sourceBeforeAfterMatch':True,'stdoutOnlyHasNoTraceFilesystemCallsOrPath':True,'invalidEnabledModeWithoutTraceRefused':True,'fileTraceFaultCases':8,'fileTraceFailureLatchesOffAndClosesOnce':True,'completeShortFileWriteRemainsHealthy':True,'sanitizers':['ASan','UBSan'],'evidenceOnlyNoAbsenceAuthority':True,'payloadLoads':0,'consoleActions':0,'published':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'cases':proof['cases'],'checks':proof['checks'],'sdkObjects':len(sdk),'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
