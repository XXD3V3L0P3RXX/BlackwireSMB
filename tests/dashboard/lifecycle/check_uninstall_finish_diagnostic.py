#!/usr/bin/env python3
"""Actual normal-adapter callback stages/flags in bounded finish diagnostics.

Uses the existing actual Send/deletion/SQLite/SMP fixture and compiler recipe.
The fixed host log stays inside the fresh output; no console calls or source
patches, retries, timing changes or new destructive authority are introduced.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--host-only',action='store_true');a=parser.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh build output')
 out.mkdir(parents=True);base=HERE/'check_game_uninstall_remote.py';base_pin=pin(base);self_pin=pin(Path(__file__));text=base.read_text()
 def replace(old,new):
  nonlocal text
  if text.count(old)!=1:raise ValueError('Maintained diagnostic recipe seam changed: '+old[:70])
  text=text.replace(old,new,1)
 replace('HERE=Path(__file__).resolve().parent','HERE=Path('+repr(str(HERE))+')')
 replace('from fixture_sdk import production_rows','import sys\nsys.path.insert(0,'+repr(str(HERE))+')\nfrom fixture_sdk import production_rows')
 replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')')
 replace("if not out.is_relative_to(ROOT/'build/native-dashboard'):","if not out.is_relative_to(ROOT/'build'):")
 replace("    host += ['-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status_fixture))]", "    host += ['-DBW_SEND_DIAGNOSTIC=1', '-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status_fixture)), '-DBW_SEND_LOG_PATH='+json.dumps(str(out/'finish.log'))]")
 start=text.index("    scenarios=['send-only'");end=text.index('    if args.scenario:',start)
 text=text[:start]+"    scenarios=['runtime-busy','manual-no-effect','uninstall-no-effect','registered','send-only','scan-root']\n"+text[end:]
 original="            fixtures.fixture(area,fixture_kind);observations.append(json.loads(run('case-'+scenario,[binary,area,scenario]).splitlines()[-1]))"
 added="""            (out/'finish.log').write_bytes(b'')
            fixtures.fixture(area,fixture_kind);observed=json.loads(run('case-'+scenario,[binary,area,scenario]).splitlines()[-1])
            records=[line for line in (out/'finish.log').read_text().splitlines() if 'event=normal-uninstall-finish ' in line]
            if not records or any(len(line.encode())>=2048 for line in records):raise ValueError('Missing/unbounded normal finish diagnostic '+scenario)
            first=records[0]
            expected={'runtime-busy':(1,0,0,'initial_runtime_release'),'manual-no-effect':(1,1,0,'poll_limit'),'uninstall-no-effect':(1,1,1,'poll_limit'),'registered':(1,1,1,'absent_source_exclusion'),'send-only':(1,0,0,'absent_source_exclusion'),'scan-root':(0,0,0,'not_entered')}[scenario]
            for key,value in zip(('registrationCalled','manualCalled','uninstallCalled','callbackStage'),expected):
                if key+'='+str(value)+' ' not in first:raise ValueError('Incorrect observed callback flag '+scenario+' '+key)
            for key in ('coreReason=', 'adapterReason=', 'remoteReason=', 'substage=', 'pathClass=', 'uninstallHttp=', 'uninstallApi=', 'uninstallRefused=', 'releaseUnknown='):
                if key not in first:raise ValueError('Missing bounded finish field '+key)
            observed['finishDiagnostic']={'records':len(records),'first':first,'expectedCallbackFlags':expected,'loggingDidNotRepeatNormalMutations':True};observations.append(observed)
"""
 replace(original,added.rstrip())
 runner=out/'recipe.py';runner.write_text(text);argv=[sys.executable,str(runner),'--output',str(out/'actual-normal')]
 if a.host_only:argv.append('--host-only')
 result=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=360)
 (out/'runner.json').write_text(json.dumps({'argv':argv,'exitCode':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2)+'\n')
 if result.returncode:raise RuntimeError('Actual finish diagnostic qualification failed; see runner.json')
 if pin(base)!=base_pin or pin(Path(__file__))!=self_pin:raise ValueError('Checker input changed during verification')
 proof=json.loads((out/'actual-normal/proof.json').read_text());proof.update({'boundedActualFinishDiagnostic':True,'actualCallbackStageFlagsVerified':True,'maintainedRecipe':base_pin,'checker':self_pin})
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'cases':proof['cases'],'checks':proof['checks'],'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
