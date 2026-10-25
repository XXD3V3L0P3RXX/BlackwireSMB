#!/usr/bin/env python3
"""Build the standalone Blackwire web resident and folder/PFS/PFSC reader, offline."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, json, re, sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/build'))
from common import Runner, pin, save, tree, verify_inputs
import app, deps, reader, provenance, sdk_runtime

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--build',action='store_true')
    parser.add_argument('--host-tests',action='store_true')
    parser.add_argument('--sdk',default='/opt/ps5-payload-sdk')
    parser.add_argument('--name')
    parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--allow-source-changes',action='store_true')
    args=parser.parse_args()
    if not 1<=args.jobs<=16:raise ValueError('Use 1 through 16 build jobs')
    checked=verify_inputs(ROOT,args.allow_source_changes)
    source_manifest=json.loads((ROOT/'metadata/source-manifest.json').read_text())
    build_manifest=dict(source_manifest.get('buildSource',{}),**source_manifest.get('testSource',{}))
    if not build_manifest:raise ValueError('Pinned standalone build source is missing')
    changed_build=[]
    for name,expected in build_manifest.items():
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid build source path')
        path=ROOT/relative
        if not path.is_file() or path.is_symlink() or any(p.is_symlink() for p in path.parents):raise ValueError('Missing or linked build source')
        if pin(path)!=expected:
            if not args.allow_source_changes:raise ValueError('Pinned standalone build source differs: '+name)
            changed_build.append(name)
    checked.update(checkedBuildSource=len(build_manifest),changedBuildSource=changed_build)
    if not args.build and not args.host_tests:
        print(json.dumps(dict(checked,compiled=False,standaloneWeb=True,nativeFrontend=False)))
        return
    if sys.platform!='linux':raise ValueError('Build/test through Linux or WSL; --check also works on Windows')
    if not re.fullmatch(r'[A-Za-z0-9_./-]+',str(ROOT)):raise ValueError('Use a checkout path without whitespace or shell metacharacters')
    name=args.name or datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}',name):raise ValueError('Use a simple new output name')
    sdk=Path(args.sdk).resolve(strict=True) if args.build else None
    if sdk and not re.fullmatch(r'[A-Za-z0-9_./-]+',str(sdk)):raise ValueError('Use an SDK path without whitespace or shell metacharacters')
    out=ROOT/'build'/name
    out.mkdir(parents=True,exist_ok=False)
    before={part:tree(ROOT/part) for part in ('src','vendor','tools','metadata','tests')}
    run=Runner(ROOT,out,sdk)
    save(out/'intent.json',{'atUtc':datetime.now(timezone.utc).isoformat(),'source':before,'checked':checked,
        'standaloneWeb':True,'nativeFrontend':False,'folderOnly':False,'hostTestsRequested':args.host_tests,
        'sdkBuildRequested':args.build,'consoleContacted':False,'deployed':False})
    try:
        source,slices=reader.prepare(ROOT,out)
        host=reader.host_tests(ROOT,out,source,run) if args.host_tests else None
        result={'passed':True,'standaloneWeb':True,'nativeFrontend':False,'hostTests':host,'sdkBuild':args.build,
                'folderOnly':False,'compressedFormats':['unsigned-contiguous-d32-pfs','pfsc-pfs','one-file-pfs-wrapper','contiguous-exfat','pfsc-exfat'],
                'consoleContacted':False,'deployed':False,'nativeRuntimeProved':False}
        if args.build:
            run.run([sdk/'bin/prospero-clang','--version'],'sdk-compiler-version')
            dependency=deps.build(ROOT,out,sdk,run,args.jobs)
            runtime=sdk_runtime.build(ROOT,out,sdk,run)
            image,built_reader=reader.build(ROOT,out,sdk,source,dependency,run,args.jobs,runtime)
            result['sourceRuntime']=sdk_runtime.products(ROOT,out,runtime)
            result['dependencies']=dependency['products']
            result['reader']=pin(image)
            result['application']=app.build(ROOT,out,sdk,dependency,image,run,args.jobs,args.host_tests,args.allow_source_changes,runtime)
            result['actualSdkInputs']=provenance.sdk_inputs(ROOT,out,sdk)
        if before!={part:tree(ROOT/part) for part in ('src','vendor','tools','metadata','tests')}:
            raise ValueError('Source changed during build; outputs retained but refused')
        result.update(sourceBeforeAfterMatch=True,commands=len(run.records),dataSlices=slices)
        save(out/'result.json',result)
        print(json.dumps({'passed':True,'application':result.get('application'),'consoleContacted':False}))
    except BaseException as error:
        save(out/'result.json',{'passed':False,'error':type(error).__name__+': '+str(error),'partialOutputsRetained':True,
            'commands':len(run.records),'consoleContacted':False,'deployed':False})
        raise

if __name__=='__main__':main()
