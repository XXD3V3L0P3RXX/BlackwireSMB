#!/usr/bin/env python3
"""Joined actual service/reserved native Send ownership refusal; host only."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--expect-old',action='store_true');p.add_argument('--old-service-source',type=Path)
 p.add_argument('--expect-early-old',action='store_true');a=p.parse_args()
 if a.expect_old and a.expect_early_old:raise ValueError('Choose one old reproduction')
 out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Fresh source/build output required')
 if bool(a.old_service_source)!=a.expect_old:raise ValueError('Old reproduction needs explicit immutable old source')
 old=a.old_service_source.resolve() if a.old_service_source else None
 if old and pin(old)['sha256']!='29bacf093af269aeca5cea422742958070935002921b80d5fe330ae6b224968c':raise ValueError('Old source does not match preserved pre-propagation bytes')
 if a.expect_early_old and pin(ROOT/'src/blackwire/library/native_backend.c')['sha256']!='b97f5bf2de0524b10c556b2a6cdb8e2a466ea6a9ef878f55f4a6c597bcab88d5':raise ValueError('Early old reproduction requires exact pre-fix backend')
 out.mkdir(parents=True)
 base=HERE/'check_uninstall_release_unknown.py'
 before={str(p.relative_to(ROOT)):pin(p) for p in [Path(__file__),HERE/'test_owner_refusal_service.c',base,HERE/'test_uninstall_release_worker.c',HERE/'test_uninstall_release_unknown.c']}
 data=base.read_text()
 def one(old_text,new_text):
  nonlocal data
  if data.count(old_text)!=1:raise ValueError('Maintained bridge seam changed: '+old_text[:80])
  data=data.replace(old_text,new_text,1)
 one('HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]', 'HERE=Path('+repr(str(HERE))+');ROOT=HERE.parents[2]')
 # Replace only the included worker translation unit, preserving its source pin.
 one('"sources=[HERE/\'test_uninstall_release_unknown.c\',HERE/\'test_uninstall_release_worker.c\']+[LIB/n for n in names]"',
     '"sources=[HERE/\'test_uninstall_release_unknown.c\',HERE/\'test_owner_refusal_service.c\']+[LIB/n for n in names]"')
 insertion = '''
 replace("    owned=[Path(__file__).resolve()", "    sources += [ROOT/'src/blackwire/dashboard/service'/n for n in ('dashboard_service.c','catalog_adapter.c','catalog_snapshot.c','frontend_identity.c','local_http.c')]+[ROOT/'src/blackwire/dashboard/model/dashboard_model.c',STREAM/'diagnostics/game_stop_identity.c']\\n    owned=[Path(__file__).resolve()")
 replace("'-DBW_LAUNCH_TIMING=1'", "'-DBW_LAUNCH_TIMING=1','-DBD_FRONTEND_IDENTITY_HOST_TEST=1'")
 replace("'-I',ROOT/'vendor/sqlite'", "'-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/dashboard/service','-I',ROOT/'src/blackwire/dashboard/model'")
 replace("    before={p.resolve():pin(p) for p in possible}", "    possible.update((ROOT/'src/blackwire/dashboard').rglob('*.h'));possible.add(HERE/'test_uninstall_release_worker.c')\\n    before={p.resolve():pin(p) for p in possible}")
'''
 if old:
  insertion += '\n replace("    owned=[Path(__file__).resolve()", "    sources=[Path('+repr(str(old))+') if x.name==\\\"dashboard_service.c\\\" else x for x in sources]\\n    owned=[Path(__file__).resolve()")\n'
 one(" runner=out/'recipe.py';runner.write_text(text)",insertion+"\n runner=out/'recipe.py';runner.write_text(text)")
 data,n=re.subn(r"scenarios=\['release-core-before-intent'[^\n]*?\]", "scenarios=['release-ordinary-metadata']",data)
 if n!=1:raise ValueError('Maintained case selection changed')
 driver=out/'driver.py';driver.write_text(data)
 env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}
 if a.expect_old:env['BW_OWNER_REFUSAL_EXPECT_OLD']='1'
 else:env.pop('BW_OWNER_REFUSAL_EXPECT_OLD',None)
 if a.expect_early_old:env['BW_OWNER_EARLY_REFUSAL_EXPECT_OLD']='1'
 else:env.pop('BW_OWNER_EARLY_REFUSAL_EXPECT_OLD',None)
 argv=[sys.executable,str(driver),'--host-only','--output',str(out/'focused')]
 r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=360,env=env)
 (out/'runner.json').write_text(json.dumps({'argv':argv,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2)+'\n')
 if r.returncode:raise RuntimeError('Joined actual bridge failed; runner evidence retained')
 inner=json.loads((out/'focused/proof.json').read_text())
 joined=[];unknown=[]
 for cmd in inner['commands']:
  if cmd['label']!='case-release-ordinary-metadata':continue
  for line in cmd['stdout'].splitlines():
   try:row=json.loads(line)
   except json.JSONDecodeError:continue
   if row.get('ownerServiceJoined'):joined.append(row)
   if row.get('ownerServiceUnknownControl'):unknown.append(row)
 if len(joined)!=1:raise ValueError('Missing unique joined service result')
 row=joined[0]
 if row['expectedOldRed']!=a.expect_old or row['ownershipRefusalPreserved']==a.expect_old:raise ValueError('Unexpected old/new outcome')
 if row['expectedEarlyOldRed']!=a.expect_early_old:raise ValueError('Unexpected early old/new outcome')
 if not a.expect_old and (row['fallbackResetControls']!=7 or row['additionalWhitelistControls']!=3 or row['unknownRetentionControls']!=2 or len(unknown)!=2):raise ValueError('Bounded propagation controls did not complete')
 if not a.expect_old and (row['actualEarlyCacheRefusalControls']!=2 or row['earlyCacheCleartext']==a.expect_early_old):raise ValueError('Actual early cache refusal controls did not complete')
 after={str(p.relative_to(ROOT)):pin(p) for p in [Path(__file__),HERE/'test_owner_refusal_service.c',base,HERE/'test_uninstall_release_worker.c',HERE/'test_uninstall_release_unknown.c']}
 if before!=after or not inner['sourceBeforeAfterMatch']:raise ValueError('Input changed during proof')
 proof={'schema':'blackwire-owner-refusal-service/1','passed':not(a.expect_old or a.expect_early_old),'expectedOldRedConfirmed':a.expect_old or a.expect_early_old,'expectedOldKind':'early-native-cache-message' if a.expect_early_old else 'service-owner-message' if a.expect_old else None,
  'joined':row,'unknownControls':unknown,'actualService':True,'actualReservedNativeBackend':True,'actualSendNrcProducer':True,
  'sourceBefore':before,'sourceAfter':after,'dependencies':inner['source'],'dependencyBeforeAfterMatch':True,
  'oldServiceSource':pin(old) if old else None,'innerProof':pin(out/'focused/proof.json'),
  'checks':inner['checks'],'inheritedCases':inner['cases'],'consoleActions':0,'fullBuilds':0,'sdkCompiles':0,
  'seams':['Actual setup-generation reads are fixed host values','Full idle/platform/frontend-disabled observations are host callbacks',
   'Normal SMP catalog response is modeled empty; no HTTP socket is opened','Registration effect is a synthetic app/SQLite fixture captured by real NRC begin/finish',
   'Actual service and backend authentication, catalog JSON, reservation, Send preflight, NRC listing, and outer release execute',
   'Pending/unknown resident-cache transition is injected after service catalog/guards and before the actual reserved backend request; a freshly blocked catalog may disable Send'],
  'limits':['No game bytes are copied for the refused source','Unknown modeled transport controls intentionally exit isolated child processes while retaining authority; child leak cleanup is not claimed','No compatibility, console gameplay, or release approval claim']}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
 print(json.dumps({'passed':proof['passed'],'expectedOldRedConfirmed':proof['expectedOldRedConfirmed'],'checks':proof['checks'],'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()

