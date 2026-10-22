#!/usr/bin/env python3
"""Actual reserved async PC Uninstall/Restore route tests and strict SDK TUs."""
from pathlib import Path
import argparse,hashlib,json,os,re,shlex,subprocess
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build/native-dashboard'):raise ValueError('Use a new scoped output')
 out.mkdir(parents=True,exist_ok=False);commands=[];deps=[]
 sources=[HERE/'test_game_removal_backend.c']+[LIB/n for n in ['native_removal_request.c','native_request.c','native_job.c','native_setup.c','source_binding.c','source_identity.c','source_profile.c','source_param.c','source_catalog.c']]+[STREAM/'manifest.c',STREAM/'share_config.c']
 own=[LIB/n for n in ['native_backend.c','native_backend.h','native_backend_removal.h','native_removal_request.c','native_removal_request.h']]+[sources[0],Path(__file__).resolve()]
 possible=set(own+sources)|set(LIB.glob('*.h'))|set(LIB.glob('*.c'))|set(STREAM.rglob('*.h'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))|set((ROOT/'vendor/sqlite').glob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').glob('*.h'))
 before={p.resolve():pin(p) for p in possible}
 def run(label,argv):
  argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode or re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; evidence retained')
  return r.stdout
 common=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-g','-ffunction-sections','-fdata-sections','-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
 host=['clang-18',*common,'-O1','-DBW_NATIVE_SETUP_HOST_TEST=1','-DBW_SOURCE_DIRECTORY_HOST_TEST=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'];objects=[]
 for i,p in enumerate(sources):
  obj=out/f'host-{i}.o';dep=obj.with_suffix('.d');deps.append(dep);run('host-'+p.stem,[*host,'-MD','-MF',dep,'-c',p,'-o',obj]);objects.append(obj)
 # Only unreachable standalone preparation/source/SMB branches may abort. The
 # worker, parser, settings equality, catalogue, jobs and all state code are real.
 defined=set();needed=set()
 for obj in objects:
  for line in run('symbols-'+obj.stem,['nm','-g',obj]).splitlines():
   f=line.split()
   if len(f)>=2:(needed if f[-2]=='U' else defined).add(f[-1])
 names=sorted(n for n in needed-defined if n.startswith('bw_'))
 declarations='\n'.join(p.read_text() for d in (LIB,STREAM) for p in d.glob('*.h'))
 stub=['#include "native_backend.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include "source_directory.h"','#include "container_source.h"','#include "startup_runtime_control.h"','#include <smb2/smb2.h>','#include <smb2/libsmb2.h>','#include <stdlib.h>']
 for name in names:
  found=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
  if not found:raise ValueError('Missing explicit unexercised declaration: '+name)
  stub.append(found[0]+' { abort(); }')
 # Function-address comparisons retain native preparation in this fixture.
 # No network operation is part of the injected removal test: abort if used.
 smb_headers=(ROOT/'vendor/libsmb2/include/smb2/libsmb2.h').read_text()
 for name in sorted(n for n in needed-defined if n.startswith('smb2_')):
  found=re.findall(r'((?:int|void|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',smb_headers)
  if len(found)!=1:raise ValueError('Missing exact unexercised SMB declaration: '+name)
  stub.append(found[0]+' { abort(); }')
 p=out/'unsupported.c';p.write_text('\n'.join(stub)+'\n');obj=out/'unsupported.o';run('unsupported-abort-stubs',[*host,'-Wno-unused-parameter','-Wno-c2x-extensions','-c',p,'-o',obj]);objects.append(obj)
 binary=out/'host-game-removal-backend';run('host-link',['clang-18','-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,'-Wl,--gc-sections','-pthread','-lm','-o',binary]);observed=json.loads(run('actual-async-routes',[binary]))
 historical=production_rows(ROOT,out/'sdk-template');template=next(r['argv'] for r in historical if '-c' in r['argv'] and Path(r['argv'][r['argv'].index('-c')+1]).name=='native_backend.c');native=[]
 for name in ('native_backend.c','native_removal_request.c'):
  argv=template.copy();src=LIB/name;obj=out/(src.stem+'.sdk.o');dep=obj.with_suffix('.d');deps.append(dep);argv[argv.index('-c')+1]=str(src);argv[argv.index('-o')+1]=str(obj)
  if '-MF' in argv:argv[argv.index('-MF')+1]=str(dep)
  run('sdk-'+src.stem,argv);native.append(obj)
 actual=set(own+sources)
 for dep in deps:
  for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/name).resolve())
 local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
 for p in local:
  if p not in before or pin(p)!=before[p]:raise RuntimeError('Actual source changed during check: '+str(p))
 for p in local:
  dest=out/'frozen-source'/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
 proof=dict(passed=True,host=observed,source={str(p.relative_to(ROOT)):before[p] for p in sorted(local)},sourceBeforeAfterMatch=True,hostBinary=pin(binary),sdkObjects={p.name:pin(p) for p in native},commands=commands,unsupportedAbortStubs=names,normalTransportQualification='Separate real filesystem/SMP adapter suite required; this suite injects effects only.',consoleActions=0,fullBuilds=0)
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,host=observed,proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
