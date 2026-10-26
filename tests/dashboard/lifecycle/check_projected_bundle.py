#!/usr/bin/env python3
"""Actual projected bundle+codec fault tests; no native module execution."""
from pathlib import Path
import argparse, hashlib, json, os, shlex, subprocess, sys, tempfile
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
LIB=ROOT/'src/blackwire/library'
STREAM=ROOT/'src/blackwire/streamreader'
def pin(path):
    raw=path.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--host-only',action='store_true');args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Fresh output below build required')
    out.mkdir(parents=True);commands=[]
    def run(label,argv):
        result=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':result.returncode});(out/(label+'.log')).write_bytes(result.stdout+result.stderr);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode or b'runtime error:' in result.stderr or b'ERROR: AddressSanitizer' in result.stderr:raise RuntimeError(label+' failed; evidence retained')
        return result.stdout
    common=['-std=gnu11','-D_DEFAULT_SOURCE','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-I',LIB]
    sources=[HERE/'test_projected_bundle.c',LIB/'preload_bundle.c',LIB/'source_companions.c',LIB/'source_plan.c',STREAM/'manifest.c']
    actual={Path(__file__).resolve(),*sources}
    for i,source in enumerate(sources):
        raw=run('dependencies-'+str(i),['clang-18',*common,'-MM',source])
        for item in shlex.split(raw.decode().replace('\\\n',' ').split(':',1)[1]):
            path=Path(item);actual.add((path if path.is_absolute() else ROOT/path).resolve())
    before={str(p.relative_to(ROOT)):pin(p) for p in sorted(actual)}
    image=bytes((i*23+9)%251 for i in range(2048));image_c=out/'image.c';image_c.write_text('#include "preload_bundle.h"\nstatic const unsigned char image[]={'+','.join(map(str,image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={image,sizeof image,{'+','.join(map(str,hashlib.sha256(image).digest()))+'},"'+'0'*64+'",10000};\n')
    binary=out/'projected-bundle-host';run('host-build',['clang-18',*common,'-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*sources,image_c,*['-Wl,--wrap='+name for name in ('malloc','calloc','write','close')],'-pthread','-o',binary])
    fixture=Path(tempfile.mkdtemp(prefix='blackwire-projected-bundle-'));tested=json.loads(run('actual-bundle-faults',[binary,fixture]))
    sdk_pin=None
    if not args.host_only:
        sdk=out/'preload_bundle.sdk.o';run('sdk-bundle',['/opt/ps5-payload-sdk/bin/prospero-clang',*[v for v in common if v!='-DBW_PRELOAD_BUNDLE_HOST_TEST=1'],'-fPIC','-c',LIB/'preload_bundle.c','-o',sdk]);sdk_pin=pin(sdk)
    if before!={str(p.relative_to(ROOT)):pin(p) for p in sorted(actual)}:raise RuntimeError('Qualified source changed during check')
    proof={'passed':True,'host':tested,'source':before,'sourceBeforeAfterMatch':True,'sdkObject':sdk_pin,'commands':commands,'syntheticFixturesRetained':str(fixture),'scope':'Actual codec, explicit planner and projected bundle installation/current-reader upgrade/LOCAL-only removal selection. Admission owner and BWNAT001 parent are synthetic unit fixtures; actual Send/admission/registration integration remains required.','consoleActions':0,'moduleExecution':False,'deployed':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'host':tested,'sdkUnits':0 if args.host_only else 1}))
if __name__=='__main__':main()
