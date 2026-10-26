#!/usr/bin/env python3
"""Actual-code fast Ready Play checks; host fixtures/SDK objects, never console."""
from pathlib import Path
import hashlib,json,os,re,shlex,sqlite3,struct,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
OUT=ROOT/'build/native-dashboard/fast-ready-play-01'/('checks-'+(sys.argv[1] if len(sys.argv)>1 else '01'))
sys.path.insert(0,str(HERE))
import manifest_fixture as manifest

def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 OUT.mkdir(parents=True,exist_ok=False);commands=[]
 def run(label,args,cwd=ROOT,required=True,timeout=180):
  args=list(map(str,args));r=subprocess.run(args,cwd=cwd,capture_output=True,text=True,timeout=timeout,
   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=label,argv=args,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
  (OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if required and r.returncode:raise RuntimeError(label+' failed; retained commands.json')
  return r
 flags=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable',
  '-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer',
  '-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_LAUNCH_TIMING=1',
  '-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'vendor/sqlite',
  '-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
 sources=[HERE/'test_fast_ready_play.c']+[LIB/n for n in [
  'reader_preflight.c','native_register.c','register_job.c','native_registration.c','registration_db.c',
  'native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_namespace.c','source_companions.c','reader_projection.c','source_profile.c','source_plan.c','source_param.c','source_directory.c']]+[STREAM/'manifest.c',STREAM/'share_config.c']
 owned=[LIB/n for n in ['native_launch_admission.c','native_launch_admission.h','preload_source_ampr_internal.h','preload_bundle.c','preload_bundle.h','native_send.c','native_companion_state.c','native_register.c','native_register.h','reader_preflight.c','reader_preflight.h','reader_runtime.h','native_backend.c','native_backend_home.h','native_backend_removal.h']]+[ROOT/'src/blackwire/reader/native-reader.c',STREAM/'prepare_copy.c',Path(__file__),HERE/'test_fast_ready_play.c',HERE/'test_registration_backend.c',HERE/'test_idle_cache_callers.c',HERE/'source_ampr_admission_model.h',LIB/'startup_runtime_idle.c',LIB/'startup_runtime_idle.h',LIB/'idle_log_cache.h',LIB/'native_job.c',STREAM/'diagnostics/game_stop_identity.c',STREAM/'diagnostics/game_stop_identity.h']
 before={str(p.relative_to(ROOT)):pin(p) for p in dict.fromkeys(owned+sources)}
 objects=[]
 for p in sources:
  obj=OUT/(p.stem+'.o');run('host-'+p.stem,flags+['-c',p,'-o',obj]);objects.append(obj)
 # Unsupported legacy derived/container paths are linked to aborting stubs.
 # Every function exercised by the new ordinary path, the actual strict Send,
 # local verifier, real SQLite registration and canonical codecs is real code.
 defined=set();needed=set()
 for p in objects:
  for line in run('symbols-'+p.stem,['nm','-g',p]).stdout.splitlines():
   fields=line.split()
   if len(fields)>=2:
    if fields[-2]=='U':needed.add(fields[-1])
    else:defined.add(fields[-1])
 missing=sorted(n for n in needed-defined if n.startswith('bw_'))
 headers=[p for d in (LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate') for p in d.glob('*.h') if p.name not in {'native_json.h'}]
 declaration='\n'.join(p.read_text() for p in headers)
 stubs=['#include "reader_preflight.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include "source_namespace.h"','#include <stdlib.h>']
 for name in missing:
  matches=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declaration)
  if not matches:raise ValueError('Missing explicit abort-stub declaration: '+name)
  stubs.append(matches[0]+' { abort(); }')
 stub=OUT/'unsupported-branches.c';stub.write_text('\n'.join(stubs)+'\n')
 obj=OUT/'unsupported-branches.o';run('unsupported-abort-stubs',flags+['-Wno-unused-parameter','-Wno-c2x-extensions','-c',stub,'-o',obj]);objects.append(obj)
 sqlite=OUT/'sqlite.o';run('source-sqlite',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION',
  '-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite]);objects.append(sqlite)
 binary=OUT/'host-fast-ready-play';run('host-link',['clang-18','-fsanitize=address,undefined',*objects,'-Wl,--gc-sections',
  '-Wl,--wrap=openat','-Wl,--wrap=readdir','-Wl,--wrap=pread','-Wl,--wrap=close','-pthread','-lm','-o',binary])
 scenarios=['normal','missing-boot','changed-boot','changed-module','mode-module','index-mismatch','map-mismatch','receipt-mismatch',
  'source-owner-mismatch','settings-before','settings-after','required-after','receipt-after','selected-mismatch','source-binding',
  'source-missing-boot','source-title','source-module-size','missing-stream','changed-nonboot-local','registration','foreign-registration',
  'linked-boot','zero-run','missing-play-epoch','close-error','source-ampr','source-ampr-local-tamper','source-ampr-mode','source-ampr-stale','source-ampr-extra-local']
 results=[]
 def fixture(area,scenario,streams):
  data=area/'data';data.mkdir();source=area/'source';source.mkdir();meta=area/'metadata';meta.mkdir();(area/'game.log').write_text('')
  (source/'sce_sys').mkdir();(source/'sce_module').mkdir();(source/'assets').mkdir()
  param=b'{"titleId":"PPSA99999","contentId":"UP0000-PPSA99999_00-BLACKWIREFIXTURE0"}'
  contents={'eboot.bin':bytes(range(256))*1024,'sce_sys/param.json':param,'sce_module/libfixture.sprx':bytes(range(255,-1,-1))*2048,'startup.txt':b'prepared non-executable LOCAL data'}
  if scenario.startswith('source-ampr') and scenario!='source-ampr-extra-local':
   (source/'fakelib').mkdir();contents['fakelib/libSceAmpr.sprx']=b'SYNTHETIC INDEPENDENT SOURCE AMPR\0'*157
  fields=['192.0.2.1','FixtureShare','Fixture','PPSA99999'];binding=hashlib.sha256(b'BWBIND01'+b''.join(struct.pack('<I',len(s))+s.encode() for s in fields)).hexdigest();game='PPSA99999_'+binding
  rows=[]
  for name,body in contents.items():
   p=source/name;p.write_bytes(body);os.utime(p,(1700000000,1700000000));rows.append(manifest.File(name,len(body),1700000000,manifest.LOCAL))
  for i in range(streams):rows.append(manifest.File(f'assets/stream-{i:05}.pak',1024+i,1700000000,manifest.STREAM))
  rows.sort(key=lambda r:manifest.ascii_key(r.path));body=manifest.encode(rows);(meta/'map.bin').write_bytes(body)
  checksum=struct.unpack_from('<Q',body,32)[0];dig=b'BWDIG001'+struct.pack('<IIQ',1,len(rows),checksum)+hashlib.sha256(body).digest()
  dig+=b''.join(hashlib.sha256(contents[r.path]).digest() if r.flags==manifest.LOCAL else bytes(32) for r in rows);(meta/'digests.bin').write_bytes(dig)
  (meta/'selection.txt').write_text(f'version=2\nrun_id={"a"*32}\ntitle_id=PPSA99999\ngame={game}\neboot_size={len(contents["eboot.bin"])}\nmanifest_count={len(rows)}\nmanifest_hash={checksum:016x}\n')
  apps=area/'apps';apps.mkdir();app=apps/'PPSA99999';app.mkdir();(app/'sce_sys').mkdir();(app/'sce_sys/param.json').write_bytes(param)
  root=data/'games'/game;(app/'mount.lnk').write_text(str(root if scenario!='foreign-registration' else area/'unrelated-full-game')+'\n')
  with sqlite3.connect(area/'app.db') as db:
   db.execute('CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)');db.execute('INSERT INTO tbl_contentinfo VALUES (?,?)',('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0'))
 for scenario in scenarios:
  with tempfile.TemporaryDirectory(prefix='blackwire-fast-play-') as tmp:
   area=Path(tmp);fixture(area,scenario,64)
   result=run('case-'+scenario,[binary,area,scenario,64]);results.append(json.loads(result.stdout.splitlines()[-1]))
 for n in (32,256,1024):
  with tempfile.TemporaryDirectory(prefix='blackwire-fast-scale-') as tmp:
   area=Path(tmp);fixture(area,'normal',n);result=run('scale-'+str(n),[binary,area,'normal',n]);results.append(json.loads(result.stdout.splitlines()[-1]))
 backend=OUT/'host-fast-backend';backend_flags=[f for f in flags if f!='-DBW_LAUNCH_TIMING=1']
 # Preserve the actual 66-case routing body, including both removed-folder
 # versus newly prepared compressed-copy ownership cases, while using the maintained
 # cache-aware fixture and real cache/identity implementation.
 baseline=(HERE/'test_registration_backend.c').read_text().split('int main(void){',1)[1].strip()
 retained=(HERE/'test_idle_cache_callers.c').read_text().split('static int retained_routing_main(void){',1)[1].split('\nstatic void *execute_joined(',1)[0].strip()
 if baseline!=retained:raise RuntimeError('Actual retained routing scenarios differ')
 backend_flags+=['-DBW_COPY_HOST_TEST=1','-I',STREAM/'diagnostics']
 caller_objects=[]
 for i,source in enumerate([HERE/'test_idle_cache_callers.c',LIB/'startup_runtime_idle.c',STREAM/'diagnostics/game_stop_identity.c',LIB/'native_job.c',STREAM/'share_config.c']):
  obj=OUT/('backend-caller-'+str(i)+'.o');run('actual-backend-route-build-'+str(i),backend_flags+['-pthread','-c',source,'-o',obj]);caller_objects.append(obj)
 run('actual-backend-route-link',['clang-18','-fsanitize=address,undefined',*caller_objects,*['-Wl,--wrap='+n for n in ('open','dup','close','fclose','fgets','clock_gettime')],'-Wl,--gc-sections','-pthread','-lm','-o',backend])
 caller_lines=run('actual-backend-route-cases',[backend]).stdout.splitlines();backend_result=json.loads(caller_lines[-2]);backend_cache=json.loads(caller_lines[-1])
 if not(backend_cache['passed'] and backend_cache['retainedRoutingCases']==66 and backend_cache['focusedCallerCases']==17 and backend_cache['allCachesClosedBeforeHandoff'] and backend_cache['releaseUnknownRetained']):raise RuntimeError('Actual caller/lifetime scenarios changed')
 # Compile the actual production commands, redirecting only outputs/deps.
 historical=ROOT/'build/native-dashboard/sdk-template-placeholder';sdk=OUT/'sdk';sdk.mkdir();sdk_objects=[]
 for row in production_rows(ROOT,OUT/'sdk-template'):
  a=row['argv']
  if '-c' not in a:continue
  p=Path(a[a.index('-c')+1])
  if p.name not in {'native_backend.c','native_send.c','native_companion_state.c','native_register.c','reader_preflight.c','reader_runtime.c','native-reader.c','native_owned_registration.c'}:continue
  a=a.copy();obj=sdk/(p.stem+'.o');dep=sdk/(p.stem+'.d');a[a.index('-o')+1]=str(obj)
  if '-MF' in a:a[a.index('-MF')+1]=str(dep)
  run('sdk-'+p.stem,a,LIB);sdk_objects.append(obj)
 if len(sdk_objects)!=7:raise ValueError('SDK coverage incomplete')
 after={str(p.relative_to(ROOT)):pin(p) for p in dict.fromkeys(owned+sources)}
 if before!=after:raise RuntimeError('A checked source changed during verification')
 for p in dict.fromkeys(owned+sources):
  destination=OUT/'frozen-source'/p.relative_to(ROOT);destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(p.read_bytes())
 proof=dict(passed=True,scenarios=results,backend=backend_result,backendCache=backend_cache,retainedRoutingBodyExact=True,cases=sum(r['cases'] for r in results)+backend_result['cases']+backend_cache['focusedCallerCases'],checks=sum(r['checks'] for r in results)+backend_cache['checks'],
  actualStrictSendAndVerifyExecuted=True,actualRegistrationAndReadonlySqlite=True,sourceTransport='read-only local fixture adapters; no network',unsupportedAbortStubs=missing,
  zeroNormalFullNamespaceWalks=all(r['treeReads']==r['streamOpens']==r['providerRescans']==0 for r in results),
  source=after,sourceBeforeAfterMatched=True,sdkObjects={str(p.relative_to(ROOT)):pin(p) for p in sdk_objects},consoleActions=0,nativeRuntimeExecuted=False,fullBuilds=0,commands=commands)
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=proof['cases'],checks=proof['checks'],proof=pin(OUT/'proof.json'))))
if __name__=='__main__':main()
