#!/usr/bin/env python3
"""Current Media core with real POSIX inventory/parser and modeled SDK/title rows."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse,hashlib,json,os,re,shlex,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build/native-dashboard'):raise ValueError('Use a fresh scoped native-dashboard output')
 out.mkdir(parents=True,exist_ok=False)
 sources=[HERE/'test_media_public_runtime.c',LIB/'media_public_trial.c',LIB/'source_param.c']
 possible={Path(__file__).resolve(),*sources,*LIB.glob('*.h'),*STREAM.glob('*.h')}
 before={p.resolve():pin(p) for p in possible};commands=[];deps=[];objects=[]
 def run(label,argv):
  argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=60,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append({'label':label,'argv':argv,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode or re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; partial evidence retained')
  return r.stdout
 flags=['-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',LIB,'-I',STREAM]
 for i,p in enumerate(sources):
  obj=out/f'host-{i}.o';dep=obj.with_suffix('.d');deps.append(dep);run('compile-'+p.stem,['clang-18',*flags,'-MMD','-MF',dep,'-c',p,'-o',obj]);objects.append(obj)
 binary=out/'media-public-runtime';run('link',['clang-18',*flags,*objects,'-o',binary]);observed=json.loads(run('actual-public-media-core',[binary]))
 if not observed.get('passed') or observed.get('cases')!=17:raise ValueError('All 17 actual-core scenarios are required')
 actual={Path(__file__).resolve(),*sources}
 for dep in deps:
  for n in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/n).resolve())
 local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
 for p in local:
  if p not in before or before[p]!=pin(p):raise ValueError('Source changed during focused check: '+str(p))
 proof={'passed':True,'schema':'blackwire-current-public-media-runtime-host/1','result':observed,'commands':commands,'source':{str(p.relative_to(ROOT)):before[p] for p in sorted(local)},'sourceBeforeAfterMatch':True,'binary':pin(binary),'sanitizers':['ASan','UBSan'],'actualCoreAndPosixInventoryAndParser':True,'sdkAndReadonlyTitleRowsModeled':True,'actualSqlite':False,'consoleActions':0,'networkActions':0,'fullBuilds':0,'limits':'No actual Sony SDK, SQLite provider, reservation scheduler, console visibility or shutdown-cause qualification.'}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'result':observed,'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
