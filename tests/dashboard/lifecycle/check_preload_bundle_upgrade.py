#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual owned-reader migration with synthetic library bytes; offline only.

Run in a public source checkout using Linux clang. No prior build, reader
artifact, console data or network is consulted. SDK products are compile/link
checks, never executed. Only the fixed size/hash tables are substituted
for host fixtures; the migration and refusal logic is actual maintained C.
"""
import argparse
from datetime import datetime,timezone
import hashlib,json,os,re,shlex,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=HERE.parents[2];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(path):
    b=path.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sdk',type=Path,default=Path('/opt/ps5-payload-sdk'))
    parser.add_argument('--cc',default=os.environ.get('CC','clang-18'))
    parser.add_argument('--published-reader',type=Path,help='Optional exact published beta.1/beta.2 reader bytes, treated only as host fixture data')
    args=parser.parse_args();out=args.output.resolve();sdk=args.sdk.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh absent output below build/')
    out.mkdir(parents=True);commands=[];dependencies=[]
    own={Path(__file__).resolve(),HERE/'fixture_sdk.py',HERE/'test_preload_bundle_upgrade.c',HERE/'test_preload_bundle.c',ROOT/'tools/build/app-recipe.json'}
    for directory in (ROOT/'src/blackwire',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'vendor/ampr/third_party/lz4/lib'):
        own.update(p.resolve() for p in directory.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.S'))
    before={p:pin(p) for p in own};used={Path(__file__).resolve(),HERE/'fixture_sdk.py',HERE/'test_preload_bundle_upgrade.c',HERE/'test_preload_bundle.c',ROOT/'tools/build/app-recipe.json'}
    def run(label,argv):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'ERROR: AddressSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; commands retained')
        return r.stdout
    names=['libSceAmpr.sprx','libSceAppContent.sprx','libSceGameUpdate.sprx','libSceNpEntitlementAccess.sprx','libSceAmpr.sprx.orig']
    bodies=[bytes((i*13+11)%251 for i in range(2048)),b'fixture-app-content'*91,b'fixture-game-update'*81,b'fixture-entitlement'*79];bodies.append(bodies[0])
    synthetic_previous=bytes((i*17+3)%251 for i in range(3072));previous=synthetic_previous
    if args.published_reader:
        previous=args.published_reader.read_bytes()
        if len(previous)!=843696 or hashlib.sha256(previous).hexdigest()!='39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441':raise ValueError('Exact published beta reader required')
    public07=bytes((i*19+5)%251 for i in range(4608));current=bytes((i*31+7)%251 for i in range(4096))
    globals_dir=out/'globals';globals_dir.mkdir()
    for name,body in zip(names,bodies):(globals_dir/name).write_bytes(body)
    unit=out/'unit';(unit/'library').mkdir(parents=True);(unit/'streamreader').mkdir()
    for name in ('copy_fs.h','copy_sha256.h','manifest.h'):
        p=STREAM/name;(unit/'streamreader'/name).write_bytes(p.read_bytes());used.add(p)
    playgo=[bytes((i*(v+7)+v+19)%251 for i in range(1103+v*137)) for v in range(4)]
    for v,body in enumerate(playgo):(globals_dir/('playgo-%d.sprx'%v)).write_bytes(body)
    header=(LIB/'preload_bundle.h').read_text();used.add(LIB/'preload_bundle.h')
    playgo_table='static const struct bw_preload_playgo_pin pins[]={\n'+',\n'.join('        {%d,"%s"}'%(len(b),hashlib.sha256(b).hexdigest()) for b in playgo)+'\n    };'
    header,count=re.subn(r'static const struct bw_preload_playgo_pin pins\[\]=\{.*?\n    \};',lambda _:playgo_table,header,count=1,flags=re.S)
    if count!=1:raise ValueError('Fixed PlayGo pin seam changed')
    (unit/'library/preload_bundle.h').write_text(header)
    production=(LIB/'preload_bundle.c').read_text();used.add(LIB/'preload_bundle.c')
    table='static const struct pb_pin pb_globals[5]={\n'+',\n'.join('    {"%s",%d,"%s"}'%(n,len(b),hashlib.sha256(b).hexdigest()) for n,b in zip(names,bodies))+'\n};'
    tested,count=re.subn(r'static const struct pb_pin pb_globals\[5\]=\{.*?\n\};',lambda _:table,production,count=1,flags=re.S)
    if count!=1:raise ValueError('Fixed global data table seam changed')
    predecessor='static const struct pb_pin pu_previous={"libSceAmpr.sprx",3072,"'+hashlib.sha256(synthetic_previous).hexdigest()+'"};'
    tested,count=re.subn(r'static const struct pb_pin pu_previous=\{.*?\};',lambda _:predecessor,tested,count=1,flags=re.S)
    if count!=1:raise ValueError('Fixed predecessor data table seam changed')
    public07_pin='static const struct pb_pin pu_public07={"libSceAmpr.sprx",4608,"'+hashlib.sha256(public07).hexdigest()+'"};'
    tested,count=re.subn(r'static const struct pb_pin pu_public07=\{.*?\};',lambda _:public07_pin,tested,count=1,flags=re.S)
    if count!=1:raise ValueError('Fixed public07 data table seam changed')
    (unit/'library/under-test.c').write_text(tested)
    (unit/'library/preload_bundle.c').write_text(tested)
    array=lambda b:','.join(str(v) for v in b)
    image=out/'fixture-image.c';image.write_text('#include "preload_bundle.h"\nstatic const uint8_t image_bytes[]={'+array(current)+'};\n'
        +'const uint8_t fixture_previous[]={'+array(previous)+'};\nconst size_t fixture_previous_size=sizeof fixture_previous;\n'
  +'const uint8_t fixture_public07[]={'+array(public07)+'};\nconst size_t fixture_public07_size=sizeof fixture_public07;\n'
        +'const struct bw_preload_image bw_preload_embedded_image={image_bytes,sizeof image_bytes,{'+array(hashlib.sha256(current).digest())+'},"'+'6'*64+'",10000};\n')
    host=out/'host-upgrade'
    run('host-build',[args.cc,'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function',
        '-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',unit/'library','-I',LIB,
        '-DBW_PRELOAD_GLOBAL_ROOT="'+str(globals_dir)+'"',HERE/'test_preload_bundle_upgrade.c',image,
        '-pthread','-Wl,--wrap=close','-Wl,--wrap=closedir','-Wl,--wrap=write','-Wl,--wrap=fsync','-Wl,--wrap=renameat','-Wl,--wrap=clock_gettime','-o',host])
    # Linux temporary filesystem supplies real POSIX modes/nlink/fine times;
    # do not pretend a Windows-mounted source directory supplies that contract.
    with tempfile.TemporaryDirectory(prefix='blackwire-reader-upgrade-') as tmp:
        result=json.loads(run('actual-filesystem-upgrade-faults',[host,tmp]).splitlines()[-1])
    if not(result['passed'] and result['actualFilesystem'] and result['sameWorker'] and result['cases']>=40):raise RuntimeError('Migration fixture scope changed')
    baseline=out/'host-bundle-diagnostics'
    run('host-retained-bundle-build',[args.cc,'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function',
        '-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',unit/'library','-I',LIB,
        '-DBW_PRELOAD_PLAYGO_FIXTURES=1','-DBW_PRELOAD_GLOBAL_ROOT="'+str(globals_dir)+'"',HERE/'test_preload_bundle.c',image,
        '-pthread','-Wl,--wrap=close','-Wl,--wrap=write','-Wl,--wrap=fsync','-o',baseline])
    with tempfile.TemporaryDirectory(prefix='blackwire-bundle-diagnostics-') as tmp:
        retained=json.loads(run('retained-bundle-close-diagnostics',[baseline,tmp]).splitlines()[-1])
    if not retained['passed']:raise RuntimeError('Retained bundle diagnostics failed')
    native=[];selected=[]
    for row in production_rows(ROOT,out/'sdk-template',sdk):
        argv=row['argv']
        if '-c' not in argv:continue
        source=Path(argv[argv.index('-c')+1])
        if source.name not in ('preload_bundle.c','native_backend.c','native-reader.c'):continue
        obj=out/(source.stem+'.sdk.o');dep=obj.with_suffix('.d');dependencies.append(dep);argv=argv.copy()
        argv[argv.index('-o')+1]=str(obj)
        if '-MF' in argv:argv[argv.index('-MF')+1]=str(dep)
        run('sdk-'+source.stem,argv);native.append(obj);selected.append(source.name)
    if set(selected)!={'preload_bundle.c','native_backend.c','native-reader.c'}:raise ValueError('SDK source coverage incomplete')
    image_obj=out/'image.sdk.o';run('sdk-synthetic-image',[sdk/'bin/prospero-clang','-std=gnu11','-Wall','-Wextra','-Werror','-I',LIB,'-c',image,'-o',image_obj]);native.append(image_obj)
    linked=out/'bundle-image.sdk.o';run('sdk-bundle-image-link',[sdk/'bin/ld.lld','-r',out/'preload_bundle.sdk.o',image_obj,'-o',linked])
    backend_imports=run('sdk-backend-imports',['nm','-u',out/'native_backend.sdk.o'])
    reader_imports=run('sdk-reader-imports',['nm','-u',out/'native-reader.sdk.o'])
    if 'bw_preload_bundle_upgrade_controlled' not in backend_imports or 'bw_preload_bundle_upgrade_workspace' not in backend_imports:raise ValueError('Backend does not call controlled upgrade/workspace guards')
    if 'bw_preload_bundle_verify_checked' not in reader_imports or 'bw_preload_bundle_install' in reader_imports:raise ValueError('Reader second check is not verify-only')
    external={}
    for dep in dependencies:
        if not dep.is_file():continue
        for value in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
            path=(Path(value) if Path(value).is_absolute() else ROOT/value).resolve()
            if path.is_relative_to(out):continue
            if path.is_relative_to(ROOT):used.add(path)
            elif path.is_file():external[str(path)]=pin(path)
    # The host TU includes generated copies of these actual inputs, whose only
    # code transformation is the fixed data table substitutions above.
    used.update((LIB/'preload_bundle.c',LIB/'preload_bundle.h'))
    for path in used:
        if path not in before or pin(path)!=before[path]:raise RuntimeError('Actual source input changed/unpinned: '+str(path))
    proof=dict(passed=True,atUtc=datetime.now(timezone.utc).isoformat(),host=result,retainedBundleDiagnostics=retained,sourceBeforeAfterMatch=True,
        source={str(p.relative_to(ROOT)):before[p] for p in sorted(used)},externalSdkInputs=external,
        sdkUnits=len(native),sdkRelocatableLinks=1,sdkProducts={p.name:pin(p) for p in [*native,linked]},
        hostBinary=pin(host),syntheticFixedHashSizeTablesOnly=True,noHistoricalReaderInput=True,
        strictReaderVerifyOnly=True,consoleActions=0,fullBuilds=0,nativeRuntimeQualified=False,commands=commands,
        limits=['Only host fixtures execute; native SDK products are not executed.',
            'Host current callbacks are modeled; separate composition tests qualify actual Send/admission/backend routing.',
            'No native timing or game compatibility claim is made.'])
    if args.published_reader:
        if pin(args.published_reader)!={'bytes':843696,'sha256':'39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441'}:raise ValueError('Published fixture changed')
        proof['publishedPredecessor']={'exactPublishedBytes':True,**pin(args.published_reader),'executed':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(passed=True,host=result,sdkUnits=len(native),proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
