#!/usr/bin/env python3
"""Actual source registration lifecycle/remote/guard checks, offline only."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,tempfile
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(path):
 b=path.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def retained_main(text,start):
 if text.count(start)!=1:raise RuntimeError('Retained routing entry changed')
 body=text.split(start,1)[1]
 if start.startswith('static'):
  marker='\nstatic void *execute_joined('
  if body.count(marker)!=1:raise RuntimeError('Caller fixture boundary changed')
  body=body.split(marker,1)[0]
 return body.strip()
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
 parser.add_argument('--host-only',action='store_true',help='Skip all SDK compilation and native artifacts')
 parser.add_argument('--lifecycle-only',action='store_true',help='Run only the focused lifecycle fixture; requires --host-only')
 args=parser.parse_args();out=args.output.resolve()
 if args.lifecycle_only and not args.host_only:raise ValueError('--lifecycle-only requires --host-only')
 if not out.is_relative_to(ROOT/'build/native-dashboard/registration-lifecycle-integration-01'):raise ValueError('Use a new registration-lifecycle-integration-01 child')
 out.mkdir(parents=True,exist_ok=False);commands=[];deps=[]
 status_fixture=out/'manual.status';status_fixture.write_bytes(b'installed\tCUSA00001\t/data/neutral-existing-cusa\tNeutral fixture\n');status_fixture.chmod(0o666)
 def run(label,argv,timeout=180):
  argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=timeout,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode:raise RuntimeError(label+' failed; retained commands.json')
  return r.stdout
 own=[LIB/n for n in ('native_registration_lifecycle.c','native_registration_lifecycle.h','native_backend.c','native_backend.h','reader_runtime.h','native_cleanup_remote.c','shadowmount_http.c','shadowmount_reply.c','shadowmount_client.h')]+[ROOT/'src/blackwire/reader/native-reader.c',Path(__file__),HERE/'test_registration_lifecycle.c',HERE/'test_registration_backend.c',HERE/'test_idle_cache_callers.c',HERE/'source_ampr_admission_model.h',HERE/'fixture_sdk.py',STREAM/'diagnostics/game_stop_identity.c',LIB/'startup_runtime_idle.c',LIB/'startup_runtime_idle_ps5.c',LIB/'native_job.c']
 possible=set(own)|set(LIB.glob('*.h'))|set(LIB.glob('*.c'))|set(STREAM.rglob('*.h'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))|set((ROOT/'vendor/sqlite').glob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').glob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').glob('*.c'))
 core=[LIB/n for n in ('native_registration_lifecycle.c','native_registration_cleanup.c','native_cleanup_remote.c','native_registration.c','registration_db.c','source_param.c','reader_selection.c','native_prepared_target.c')]+[STREAM/'manifest.c',HERE/'test_registration_lifecycle.c']
 core += [STREAM/n for n in ('container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]
 core += [HERE/'legacy_projection_boundary.c']
 possible.update(core+[ROOT/'vendor/sqlite/sqlite3.c',HERE/'test_registration_cleanup.c',HERE/'test_registration_cleanup_io.h',STREAM/'share_config.c'])
 possible.update((STREAM/'vendor/miniz').glob('*.c'))
 before={p.resolve():pin(p) for p in possible};includes=['-I',LIB,'-I',STREAM,'-I',STREAM/'diagnostics','-I',ROOT/'vendor/sqlite','-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
 flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-g']
 host=['clang-18',*flags,'-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-DBW_COPY_HOST_TEST=1',*includes]
 host += ['-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status_fixture))]
 objects=[]
 for index,src in enumerate(core):
  if src.name=='native_registration_lifecycle.c':continue
  obj=out/f'host-{index}-{src.stem}.o';dep=obj.with_suffix('.d');deps.append(dep)
  extra=['-include',HERE/'test_registration_cleanup_io.h'] if src.name in {'native_registration_cleanup.c','native_registration_lifecycle.c','native_cleanup_remote.c'} else []
  if src.name=='test_registration_lifecycle.c':extra=['-DBW_NRL_UNINSTALL_BOUNDARY_TEST=1']
  run('host-'+src.stem,[*host,*extra,'-MD','-MF',dep,'-c',src,'-o',obj]);objects.append(obj)
 sqlite=out/'sqlite.o';dep=sqlite.with_suffix('.d');deps.append(dep);run('source-sqlite',['clang-18','-std=gnu11','-O0','-g','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-MD','-MF',dep,'-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite])
 binary=out/'host-registration-lifecycle';run('host-link',['clang-18','-fsanitize=address,undefined',*objects,sqlite,'-Wl,--gc-sections','-pthread','-lm','-o',binary])
 with tempfile.TemporaryDirectory(prefix='blackwire-registration-lifecycle-') as tmp:fs=json.loads(run('actual-fs-ledger-sqlite',[binary,Path(tmp)/'fixtures']))
 routing=None;cache_callers=None
 if not args.lifecycle_only:
  # The maintained cache-aware fixture keeps the exact original routing body.
  # Link actual cache/identity code instead of inventing cache stubs for it.
  if retained_main((HERE/'test_registration_backend.c').read_text(),'int main(void){')!=retained_main((HERE/'test_idle_cache_callers.c').read_text(),'static int retained_routing_main(void){'):
   raise RuntimeError('Actual retained routing scenarios differ')
  backend=out/'host-registration-backend';caller_objects=[]
  for index,src in enumerate([HERE/'test_idle_cache_callers.c',LIB/'startup_runtime_idle.c',STREAM/'diagnostics/game_stop_identity.c',LIB/'native_job.c',STREAM/'share_config.c']):
   obj=out/f'caller-{index}-{src.stem}.o';dep=obj.with_suffix('.d');deps.append(dep)
   run('actual-backend-build-'+src.stem,[*host,'-pthread','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-MD','-MF',dep,'-c',src,'-o',obj]);caller_objects.append(obj)
  run('actual-backend-link',['clang-18','-fsanitize=address,undefined',*caller_objects,*['-Wl,--wrap='+name for name in ('open','dup','close','fclose','fgets','clock_gettime')],'-Wl,--gc-sections','-pthread','-lm','-o',backend])
  caller_lines=run('actual-backend-routing',[backend]).splitlines();routing=json.loads(caller_lines[-2]);cache_callers=json.loads(caller_lines[-1])
  if not(cache_callers['passed'] and cache_callers['retainedRoutingCases']==66 and cache_callers['focusedCallerCases']==17 and cache_callers['allCachesClosedBeforeHandoff'] and cache_callers['releaseUnknownRetained']):raise RuntimeError('Actual caller/lifetime scenarios changed')
 # Actual SDK production flags/paths, outputs only redirected. Newly linked
 # lifecycle/ledger/remote/transport use the same native_register compilation.
 native=[];cache_link=None
 if not args.host_only:
  historical=production_rows(ROOT,out/'sdk-template');sdk=out/'sdk';sdk.mkdir();native=[];template=None
  names={'native_backend.c','native-reader.c','native_register.c','reader_runtime.c','shadowmount_http.c','shadowmount_reply.c'}
  for row in historical:
   a=row['argv']
   if '-c' not in a:continue
   src=Path(a[a.index('-c')+1])
   if src.name=='native_register.c':template=a.copy()
   if src.name not in names:continue
   a=a.copy();obj=sdk/(src.stem+'.o');dep=obj.with_suffix('.d');deps.append(dep);a[a.index('-o')+1]=str(obj)
   if '-MF' in a:a[a.index('-MF')+1]=str(dep)
   run('sdk-'+src.stem,a);native.append(obj)
  if template is None:raise RuntimeError('Actual SDK template absent')
  for name in ('native_registration_lifecycle.c','native_registration_cleanup.c','native_cleanup_remote.c'):
   a=template.copy();src=LIB/name;obj=sdk/(src.stem+'.o');dep=obj.with_suffix('.d');deps.append(dep);a[a.index('-c')+1]=str(src);a[a.index('-o')+1]=str(obj)
   if '-MF' in a:a[a.index('-MF')+1]=str(dep)
   run('sdk-'+src.stem,a);native.append(obj)
  cache_native=[]
  for src in (LIB/'startup_runtime_idle.c',LIB/'startup_runtime_idle_ps5.c',STREAM/'diagnostics/game_stop_identity.c'):
   a=template.copy();obj=sdk/(src.stem+'.o');dep=obj.with_suffix('.d');deps.append(dep);a[a.index('-c')+1]=str(src);a[a.index('-o')+1]=str(obj)
   if '-MF' in a:a[a.index('-MF')+1]=str(dep)
   a+=['-I',str(STREAM/'diagnostics')];run('sdk-'+src.stem,a);native.append(obj);cache_native.append(obj)
  cache_link=sdk/'backend-cache-relocatable.o'
  run('sdk-backend-cache-relocatable',['/opt/ps5-payload-sdk/bin/ld.lld','-r',sdk/'native_backend.o',*cache_native,'-o',cache_link])
 actual=set(own+core+[ROOT/'vendor/sqlite/sqlite3.c'])
 for dep in deps:
  for s in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/s).resolve())
 local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
 for p in local:
  if p not in before or before[p]!=pin(p):raise RuntimeError('Actual input unpinned/changed: '+str(p))
 source={str(p.relative_to(ROOT)):before[p] for p in sorted(local)}
 for p in local:
  dst=out/'frozen-source'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(p.read_bytes())
 proof=dict(passed=True,sourceBeforeAfterMatched=True,source=source,filesystem=fs,routing=routing,cacheCallers=cache_callers,retainedRoutingBodyExact=None if args.lifecycle_only else True,hostOnly=args.host_only,lifecycleOnly=args.lifecycle_only,lifecycleSourceEmbeddedForBoundaryTest=True,backendCacheRelocatable=pin(cache_link) if cache_link else None,sdkObjects={str(p.relative_to(ROOT)):pin(p) for p in native},commands=commands,consoleActions=0,fullBuilds=0,nativeRuntimeExecuted=False,normalSmpTransportModeled=True,releaseAndFullIdleTransportModeled=True,readerImageChanged=False)
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,filesystem=fs,routing=routing,cacheCallers=cache_callers,proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
