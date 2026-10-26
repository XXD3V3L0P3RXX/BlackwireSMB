#!/usr/bin/env python3
"""Actual compiled old/current reader selection and ordinary Uninstall.

Offline host fixtures and SDK objects only. Fixed material tables are synthetic;
receipt, namespace, deletion and read-only SQLite logic are actual source.
No historical reader, private build proof, network or console is required.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, struct, subprocess, tempfile
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=HERE.parents[2];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
SDK=Path('/opt/ps5-payload-sdk')
def fixture_image(path,current,previous,public07):
 array=lambda b:','.join(str(v) for v in b)
 path.write_text('#include "preload_bundle.h"\nstatic const uint8_t image_bytes[]={'+array(current)+'};\n'
  +'const uint8_t fixture_previous[]={'+array(previous)+'};\nconst size_t fixture_previous_size=sizeof fixture_previous;\n'
  +'const uint8_t fixture_public07[]={'+array(public07)+'};\nconst size_t fixture_public07_size=sizeof fixture_public07;\n'
  +'const struct bw_preload_image bw_preload_embedded_image={image_bytes,sizeof image_bytes,{'+array(hashlib.sha256(current).digest())+'},"'+'6'*64+'",10000};\n')

def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def add_companion_fixture(area,bodies,playgo):
 # Extend the actual Send input manifest and digests, rather than adding
 # unowned original files after preparation. All payloads are synthetic data.
 meta=area/'metadata';source=area/'source';raw=(meta/'map.bin').read_bytes()
 count=struct.unpack_from('<I',raw,16)[0];pool=raw[40+count*32:];factory=load('companion_manifest',HERE/'manifest_fixture.py');rows=[]
 for i in range(count):
  at,length,flags,_,size,mtime=struct.unpack_from('<IIIIQQ',raw,40+i*32)
  rows.append(factory.File(pool[at:at+length].decode(),size,mtime,flags))
 names=['libSceAppContent.sprx','libSceGameUpdate.sprx','libSceNpEntitlementAccess.sprx','libSceAmpr.sprx.orig','libScePlayGo.sprx']
 (source/'fakelib').mkdir()
 for name,body in zip(names,[*bodies[1:],playgo]):
  path=source/'fakelib'/name;path.write_bytes(body);os.utime(path,(1700000000,1700000000));rows.append(factory.File('fakelib/'+name,len(body),1700000000,factory.LOCAL))
 rows.sort(key=lambda row:factory.ascii_key(row.path));raw=factory.encode(rows);(meta/'map.bin').write_bytes(raw);checksum=struct.unpack_from('<Q',raw,32)[0]
 digest=b'BWDIG001'+struct.pack('<IIQ',1,len(rows),checksum)+hashlib.sha256(raw).digest()
 digest+=b''.join(hashlib.sha256((source/row.path).read_bytes()).digest() if row.flags==factory.LOCAL else bytes(32) for row in rows)
 (meta/'digests.bin').write_bytes(digest);selection=(meta/'selection.txt').read_text()
 selection=re.sub(r'manifest_count=\d+',f'manifest_count={len(rows)}',selection);selection=re.sub(r'manifest_hash=[0-9a-f]+',f'manifest_hash={checksum:016x}',selection);(meta/'selection.txt').write_text(selection)
 return {str(path.relative_to(source)):pin(path) for path in (source/'fakelib').iterdir()}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--published-reader',type=Path,help='Optional exact published beta reader as host data only');args=parser.parse_args();published_reader=args.published_reader;OUT=args.output.resolve()
 if not OUT.is_relative_to(ROOT/'build') or OUT.exists():raise ValueError('Use a fresh absent output below build/')
 OUT.mkdir(parents=True);commands=[];deps=[];used=set();external={}
 candidates={p.resolve() for d in [LIB,STREAM,HERE,ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate',ROOT/'src/blackwire'] for p in d.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py')}
 candidates.update([Path(__file__),HERE/'test_preload_reader_selector.c',HERE/'test_preload_bundle_upgrade.c',HERE/'fixture_sdk.py',HERE/'test_preload_reader_uninstall.c',HERE/'test_preload_reader_uninstall_wrapper.c',ROOT/'tools/build/app-recipe.json'])
 before={p:pin(p) for p in candidates};(OUT/'input-before.json').write_text(json.dumps({str(p):v for p,v in before.items()},indent=2)+'\n')
 def run(label,args,timeout=180):
  args=list(map(str,args));r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=timeout,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
  commands.append(dict(label=label,argv=args,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode or re.search(r'runtime error:|ERROR: AddressSanitizer',r.stderr):raise RuntimeError(label+' failed; see commands.json')
  return r.stdout
 unit=OUT/'unit';(unit/'library').mkdir(parents=True);(unit/'streamreader').mkdir()
 for n in ['preload_bundle.h','native_backend.c','native_backend_removal.h']:(unit/'library'/n).write_bytes((LIB/n).read_bytes())
 for n in ['copy_fs.h','copy_sha256.h','manifest.h']:(unit/'streamreader'/n).write_bytes((STREAM/n).read_bytes())
 globals_dir=OUT/'globals';globals_dir.mkdir()
 names=['libSceAmpr.sprx','libSceAppContent.sprx','libSceGameUpdate.sprx','libSceNpEntitlementAccess.sprx','libSceAmpr.sprx.orig']
 bodies=[bytes((i*13+11)%251 for i in range(2048)),b'fixture-app-content'*91,b'fixture-game-update'*81,b'fixture-entitlement'*79];bodies.append(bodies[0])
 for n,b in zip(names,bodies):(globals_dir/n).write_bytes(b)
 playgo=[bytes((i*(v+7)+v+19)%251 for i in range(1103+v*137)) for v in range(4)]
 for v,body in enumerate(playgo):(globals_dir/('playgo-%d.sprx'%v)).write_bytes(body)
 header=(LIB/'preload_bundle.h').read_text()
 playgo_table='static const struct bw_preload_playgo_pin pins[]={\n'+',\n'.join('        {%d,"%s"}'%(len(b),hashlib.sha256(b).hexdigest()) for b in playgo)+'\n    };'
 header,count=re.subn(r'static const struct bw_preload_playgo_pin pins\[\]=\{.*?\n    \};',lambda _:playgo_table,header,count=1,flags=re.S)
 if count!=1:raise ValueError('Fixed PlayGo pin seam changed')
 (unit/'library/preload_bundle.h').write_text(header)
 synthetic_previous=bytes((i*17+3)%251 for i in range(3072));previous=synthetic_previous
 if published_reader:
  previous=published_reader.read_bytes()
  if len(previous)!=843696 or hashlib.sha256(previous).hexdigest()!='39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441':raise ValueError('Exact published beta reader required')
 public07=bytes((i*19+5)%251 for i in range(4608));current=bytes((i*31+7)%251 for i in range(4096))
 c=(LIB/'preload_bundle.c').read_text()
 table='static const struct pb_pin pb_globals[5]={\n'+',\n'.join('    {"%s",%d,"%s"}'%(n,len(b),hashlib.sha256(b).hexdigest()) for n,b in zip(names,bodies))+'\n};'
 c,n=re.subn(r'static const struct pb_pin pb_globals\[5\]=\{.*?\n\};',lambda _:table,c,count=1,flags=re.S);assert n==1
 c,n=re.subn(r'static const struct pb_pin pu_previous=\{.*?\};',lambda _:'static const struct pb_pin pu_previous={"libSceAmpr.sprx",3072,"'+hashlib.sha256(synthetic_previous).hexdigest()+'"};',c,count=1,flags=re.S);assert n==1
 c,n=re.subn(r'static const struct pb_pin pu_public07=\{.*?\};',lambda _:'static const struct pb_pin pu_public07={"libSceAmpr.sprx",4608,"'+hashlib.sha256(public07).hexdigest()+'"};',c,count=1,flags=re.S);assert n==1
 bundle=unit/'library/under-test.c';bundle.write_text(c)
 image=OUT/'fixture-image.c';fixture_image(image,current,previous,public07)
 fixture=(HERE/'test_preload_bundle_upgrade.c').read_text().replace('ssize_t __wrap_write(int fd,const void *p,size_t n){','ssize_t __wrap_write(int fd,const void *p,size_t n){if(selecting){selection_writes++;CHECK(0);}')
 fixture=fixture.replace('fault==CLOSE_DIR&&strstr(name,BW_PRELOAD_GLOBAL_ROOT)','fault==CLOSE_DIR&&(strstr(name,BW_PRELOAD_GLOBAL_ROOT)||strstr(name,"/fakelib2"))')
 fixture=fixture.replace('guard_calls++;struct stat st;','guard_calls++;if(selecting&&select_cancel_at&&guard_calls>=select_cancel_at)return -1;struct stat st;')
 (unit/'library/test_upgrade_fixture.c').write_text(fixture);used.update([HERE/'test_preload_bundle_upgrade.c',HERE/'fixture_sdk.py'])
 flags=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
  '-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-DBW_PRELOAD_GLOBAL_ROOT="'+str(globals_dir)+'"']
 for d in [unit/'library',LIB,STREAM,STREAM/'diagnostics',HERE,ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate']:flags+=['-I',d]
 def compile_one(label,p,extra=()):
  obj=OUT/(label+'.o');dep=obj.with_suffix('.d');run(label,flags+list(extra)+['-MD','-MF',dep,'-c',p,'-o',obj]);deps.append(dep);return obj
 binary=OUT/'selector';run('selector-build',flags+[HERE/'test_preload_reader_selector.c',image,LIB/'source_companions.c',STREAM/'manifest.c','-pthread',*['-Wl,--wrap='+x for x in ['close','write','fsync','renameat','closedir','clock_gettime']],'-o',binary]);used.update([HERE/'test_preload_reader_selector.c',LIB/'source_companions.c',STREAM/'manifest.c'])
 with tempfile.TemporaryDirectory(prefix='blackwire-reader-select-') as tmp:selector=json.loads(run('selector-filesystem-faults',[binary,tmp]).splitlines()[-1])
 # Reuse the actual Send/core fixture. Only fixed control pin data is synthetic;
 # receipt, SQLite, namespace, hashes, deletion and Removed publication are real.
 fixture_test=HERE/'test_preload_reader_uninstall.c';used.add(fixture_test)
 core=(LIB/'native_game_uninstall.c').read_text();used.add(LIB/'native_game_uninstall.c')
 for name,body in list(zip(names,bodies))[1:]:
  pattern=r'\{"'+re.escape(name)+r'",\d+,"[0-9a-f]{64}"\}'
  core,count=re.subn(pattern,lambda _:'{"%s",%d,"%s"}'%(name,len(body),hashlib.sha256(body).hexdigest()),core,count=1)
  if count!=1:raise ValueError('fixed core control pin seam '+name)
 core_path=unit/'library/tested-uninstall.c';core_path.write_text(core)
 sources=[fixture_test,core_path,bundle,image]+[LIB/n for n in ['native_removed_sources.c','native_registration.c','registration_db.c','native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_namespace.c','source_companions.c','source_profile.c','source_plan.c','source_param.c','source_directory.c']]+[STREAM/'manifest.c',STREAM/'share_config.c']
 objects=[]
 for i,p in enumerate(sources):
  extra=['-include',HERE/'test_game_uninstall_io.h'] if p==core_path else []
  objects.append(compile_one('core-'+str(i),p,extra))
 defined=set();needed=set()
 for p in objects:
  for line in run('symbols-'+p.stem,['nm','-g',p]).splitlines():
   fields=line.split()
   if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
 missing=sorted(x for x in needed-defined if x.startswith('bw_'))
 declarations='\n'.join(p.read_text() for d in [LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate'] for p in d.glob('*.h') if p.name!='native_json.h')
 stub=['#include "reader_preflight.h"','#include "native_register.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include "source_namespace.h"','#include <stdlib.h>']
 for name in missing:
  matches=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
  if not matches:raise ValueError('unmodeled unused branch '+name)
  stub.append(matches[0]+' { abort(); }')
 stub_path=OUT/'unsupported-branches.c';stub_path.write_text('\n'.join(stub)+'\n');objects.append(compile_one('abort-stubs',stub_path,['-Wno-unused-parameter','-Wno-c2x-extensions']))
 sql=OUT/'sqlite.o';run('sqlite-compile',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sql]);objects.append(sql);used.add(ROOT/'vendor/sqlite/sqlite3.c')
 core_binary=OUT/'core';run('core-link',['clang-18','-fsanitize=address,undefined',*objects,*['-Wl,--wrap='+n for n in ['openat','readdir','pread','close']],'-Wl,--gc-sections','-pthread','-lm','-o',core_binary])
 factory=load('game_uninstall_fixture',HERE/'check_game_uninstall.py');used.add(HERE/'check_game_uninstall.py');results=[]
 for mode in ['reader-old','reader-current','reader-legacy-current','reader-absent','reader-old-wrong-pin','reader-changed-after','reader-public07','reader-public07-wrong-pin','reader-public07-changed-after',*[f'reader-playgo-{v}' for v in range(4)],'reader-playgo-changed-after','reader-playgo-source-changed-after','reader-playgo-linked-after','reader-playgo-foreign-after']:
  with tempfile.TemporaryDirectory(prefix='blackwire-reader-remove-') as tmp:
   area=Path(tmp);factory.fixture(area,'normal',8);original={}
   if mode.startswith('reader-playgo-'):
    variant=int(mode[-1]) if mode[-1].isdigit() else 0;original=add_companion_fixture(area,bodies,playgo[variant])
   results.append(json.loads(run('actual-send-delete-'+mode,[core_binary,tmp,mode]).splitlines()[-1]))
   if original!={name:pin(area/'source'/name) for name in original}:raise ValueError('Original companions changed')
 # Actual default wrapper and joined removal worker: only helper/remote effects
 # are narrow spies in this binary; actual selector and actual core run above.
 backend_test=HERE/'test_preload_reader_uninstall_wrapper.c';used.add(backend_test)
 backend_objects=[compile_one('wrapper-'+str(i),p) for i,p in enumerate([backend_test,LIB/'startup_runtime_idle.c',STREAM/'diagnostics/game_stop_identity.c',LIB/'native_job.c',STREAM/'share_config.c'])]
 backend_objects.append(compile_one('wrapper-image',image))
 wrapper=OUT/'wrapper';run('wrapper-link',['clang-18','-fsanitize=address,undefined',*backend_objects,*['-Wl,--wrap='+n for n in ['open','dup','close','fclose','fgets','clock_gettime']],'-Wl,--gc-sections','-pthread','-lm','-o',wrapper]);wrapper_result=json.loads(run('actual-wrapper-release',[wrapper]).splitlines()[-1])
 sdk=OUT/'sdk';sdk.mkdir();sdk_objects=[]
 rows=production_rows(ROOT,OUT/'sdk-template',SDK)
 for row in rows:
  args=row['argv']
  if '-c' not in args:continue
  p=Path(args[args.index('-c')+1])
  if p.name not in {'preload_bundle.c','native_backend.c'}:continue
  args=args.copy();args[args.index('-c')+1]=str(p);obj=sdk/(p.stem+'.o');args[args.index('-o')+1]=str(obj)
  if '-MF' in args:args[args.index('-MF')+1]=str(sdk/(p.stem+'.d'))
  args[1:1]=['-I',str(LIB)];run('sdk-'+p.stem,args);sdk_objects.append(obj)
 if len(sdk_objects)!=2:raise ValueError('SDK coverage incomplete')
 run('sdk-image',['/opt/ps5-payload-sdk/bin/prospero-clang','-std=gnu11','-Wall','-Wextra','-Werror','-I',LIB,'-c',image,'-o',sdk/'image.o'])
 run('sdk-bundle-link',['ld.lld-18','-r',*sdk_objects,sdk/'image.o','-o',sdk/'backend-bundle.o'])
 if 'bw_preload_bundle_select_for_removal' in run('sdk-symbol-resolution',['nm','-u',sdk/'backend-bundle.o']):raise ValueError('selector not resolved in actual backend')
 for dep in deps:
  for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
   p=Path(name).resolve()
   if p.is_relative_to(OUT):continue
   if p in before:used.add(p)
   elif p.is_relative_to(ROOT):raise ValueError('unpinned project input '+str(p))
   else:external[str(p)]=pin(p)
 used.update([Path(__file__),HERE/'fixture_sdk.py',ROOT/'tools/build/app-recipe.json',LIB/'preload_bundle.c',LIB/'preload_bundle.h',LIB/'native_backend.c',LIB/'native_backend_removal.h'])
 if any(pin(p)!=before[p.resolve()] for p in used):raise ValueError('input changed')
 proof=dict(passed=True,selector=selector,actualSendCore=results,defaultWrapper=wrapper_result,source={str(p.relative_to(ROOT)):before[p.resolve()] for p in sorted(used)},sourceBeforeAfterMatch=True,
  dataOnlyFixturePins=True,foreignReaderRefusedByUnchangedCore=True,pcOriginalAndSavesPreserved=True,externalCompilerInputsAfter=external,sdkUnits=3,sdkRelocatableLinks=1,
  consoleActions=0,fullBuilds=0,hardwareQualified=False,sdkProducts={p.name:pin(p) for p in sdk.glob('*.o')})
 if published_reader:
  if pin(published_reader)!={'bytes':843696,'sha256':'39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441'}:raise ValueError('Published fixture changed')
  proof['publishedPredecessor']={'exactPublishedBytes':True,**pin(published_reader),'executed':False}
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,selector=selector,actualSendCore=results,defaultWrapper=wrapper_result,proof=pin(OUT/'proof.json'))))
if __name__=='__main__':main()
