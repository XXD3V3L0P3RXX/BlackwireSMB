#!/usr/bin/env python3
"""Production source_connect on actual Send/admission, exact deterministic SMB transport faults."""
from pathlib import Path
import argparse,hashlib,json,os,re,shlex,struct,subprocess,tempfile
import manifest_fixture as manifest
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'

def pin(path):
 b=path.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}

def fixture(area):
 (area/'data').mkdir();source=area/'source';source.mkdir();meta=area/'metadata';meta.mkdir();(area/'game.log').write_text('')
 for name in ('sce_sys','sce_module','assets','fakelib'):(source/name).mkdir()
 contents={'eboot.bin':bytes(range(256))*1024,'sce_sys/param.json':b'{"titleId":"PPSA99999","contentId":"UP0000-PPSA99999_00-BLACKWIREFIXTURE0"}',
   'sce_module/libfixture.sprx':bytes(range(255,-1,-1))*2048,'startup.txt':b'prepared non-executable LOCAL data',
   'fakelib/libSceAmpr.sprx':b'SYNTHETIC INDEPENDENT SOURCE AMPR\0'*157}
 rows=[]
 for name,body in contents.items():
  path=source/name;path.write_bytes(body);os.utime(path,(1700000000,1700000000));rows.append(manifest.File(name,len(body),1700000000,manifest.LOCAL))
 rows += [manifest.File(f'assets/stream-{i:05}.pak',1024+i,1700000000,manifest.STREAM) for i in range(4)]
 rows.sort(key=lambda row:manifest.ascii_key(row.path));raw=manifest.encode(rows);(meta/'map.bin').write_bytes(raw);checksum=struct.unpack_from('<Q',raw,32)[0]
 digest=b'BWDIG001'+struct.pack('<IIQ',1,len(rows),checksum)+hashlib.sha256(raw).digest()
 digest+=b''.join(hashlib.sha256(contents[row.path]).digest() if row.flags==manifest.LOCAL else bytes(32) for row in rows);(meta/'digests.bin').write_bytes(digest)
 fields=['192.0.2.1','FixtureShare','Fixture','PPSA99999'];binding=hashlib.sha256(b'BWBIND01'+b''.join(struct.pack('<I',len(x))+x.encode() for x in fields)).hexdigest()
 (meta/'selection.txt').write_text(f'version=2\nrun_id={"a"*32}\ntitle_id=PPSA99999\ngame=PPSA99999_{binding}\neboot_size={len(contents["eboot.bin"])}\nmanifest_count={len(rows)}\nmanifest_hash={checksum:016x}\n')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use fresh output within build')
 out.mkdir(parents=True);commands=[];deps=[]
 source_names=['reader_preflight.c','native_register.c','register_job.c','native_registration.c','registration_db.c','native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_profile.c','source_plan.c','source_param.c','source_directory.c']
 sources=[HERE/'test_source_connect_wrapper.c']+[LIB/name for name in source_names]+[STREAM/'manifest.c',STREAM/'share_config.c']
 possible=set(sources+[Path(__file__),HERE/'test_fast_ready_play.c',HERE/'manifest_fixture.py'])
 possible |= {p for folder in (ROOT/'src/blackwire',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite') for p in folder.rglob('*') if p.is_file() and p.suffix in ('.h','.c')}
 before={p.resolve():pin(p) for p in possible}
 flags=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-O1','-g','-ffunction-sections','-fdata-sections',
  '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_LAUNCH_TIMING=1']
 for folder in (LIB,STREAM,ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate'):flags+=['-I',str(folder)]
 def run(label,argv):
  r=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode or re.search(r'runtime error:|ERROR: AddressSanitizer',r.stderr):raise RuntimeError(label+' failed; inspect commands.json')
  return r.stdout
 objects=[]
 for i,source in enumerate(sources):
  obj=out/f'unit-{i}.o';dep=obj.with_suffix('.d');deps.append(dep);run('compile-'+source.stem,flags+['-MD','-MF',dep,'-c',source,'-o',obj]);objects.append(obj)
 defined=set();needed=set()
 for obj in objects:
  for line in run('symbols-'+obj.stem,['nm','-g',obj]).splitlines():
   fields=line.split()
   if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
 names=sorted(n for n in needed-defined if n.startswith('bw_'))
 headers=[p for folder in (LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate') for p in folder.glob('*.h') if p.name!='native_json.h']
 declarations='\n'.join(p.read_text() for p in headers)
 stubs=['#include "reader_preflight.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include <stdlib.h>']
 for name in names:
  found=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
  if not found:raise ValueError('Missing exact unused-branch declaration: '+name)
  stubs.append(found[0]+' { abort(); }')
 unsupported=out/'unsupported.c';unsupported.write_text('\n'.join(stubs)+'\n');obj=out/'unsupported.o';run('compile-unreached-abort-stubs',flags+['-Wno-unused-parameter','-Wno-c2x-extensions','-c',unsupported,'-o',obj]);objects.append(obj)
 sqlite=out/'sqlite.o';run('compile-sqlite',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite]);objects.append(sqlite)
 binary=out/'source-connect-test';run('link',['clang-18','-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,'-Wl,--gc-sections',*['-Wl,--wrap='+n for n in ('openat','readdir','pread','close')],'-pthread','-lm','-o',binary])
 with tempfile.TemporaryDirectory(prefix='blackwire-source-connect-') as tmp:
  fixture(Path(tmp));lines=run('actual-source-connect-cases',[binary,tmp,'source-ampr',4]).splitlines();setup=json.loads(lines[-2]);result=json.loads(lines[-1])
 actual=set(sources+[Path(__file__),HERE/'test_fast_ready_play.c',HERE/'manifest_fixture.py'])
 for dep in deps:
  for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/name).resolve())
 local={path.resolve() for path in actual if path.resolve().is_relative_to(ROOT) and not path.resolve().is_relative_to(out)}
 for path in local:
  if path not in before or pin(path)!=before[path]:raise RuntimeError('Checked source changed during test: '+str(path))
 proof={'passed':True,'setupFixture':setup,'actualWrapper':result,'sanitizers':['address','undefined'],'source':{str(path.relative_to(ROOT)):before[path] for path in sorted(local)},
  'sourceBeforeAfterMatched':True,'unsupportedAbortStubs':names,'transport':'Deterministic SMB transport, local fixture metadata/content; real strict Send, sealed admission, binding selection and connector wrapper.',
  'deadlineScope':'Admission/connector bound and timeout selection are exercised; real SMB elapsed time and20s full-game performance require separate native qualification.','nativeRuntimeExecuted':False,'consoleActions':0,'fullBuilds':0}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'actualWrapper':result,'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()

