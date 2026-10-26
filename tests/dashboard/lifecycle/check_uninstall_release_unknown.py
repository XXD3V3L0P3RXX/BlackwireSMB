#!/usr/bin/env python3
"""Actual Send->normal adapter/core->removal worker release-unknown propagation.

Reuses the maintained normal-adapter compiler/fixture recipe with explicit
bounded seam checks. No test rewrites production bytes or performs networking.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--host-only',action='store_true');a=p.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh build output')
 out.mkdir(parents=True);base=HERE/'check_game_uninstall_remote.py';before=pin(base);self_before=pin(Path(__file__));text=base.read_text()
 def replace(old,new):
  nonlocal text
  if text.count(old)!=1:raise ValueError('Maintained recipe seam changed: '+old[:60])
  text=text.replace(old,new,1)
 replace('HERE=Path(__file__).resolve().parent','HERE=Path('+repr(str(HERE))+')')
 replace('from fixture_sdk import production_rows','import sys\nsys.path.insert(0,'+repr(str(HERE))+')\nfrom fixture_sdk import production_rows')
 replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')')
 replace("if not out.is_relative_to(ROOT/'build/native-dashboard'):","if not out.is_relative_to(ROOT/'build'):")
 replace("sources=[HERE/'test_game_uninstall_remote.c']+[LIB/n for n in names]", "sources=[HERE/'test_uninstall_release_unknown.c',HERE/'test_uninstall_release_worker.c']+[LIB/n for n in names]")
 replace("sources += [STREAM/n for n in ('container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]", "sources += [STREAM/n for n in ('container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]+[LIB/n for n in ('native_setup.c','native_request.c','native_removal_request.c','native_job.c','source_catalog.c')]")
 replace("host += ['-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status_fixture))]", "host += ['-DBW_NATIVE_SETUP_HOST_TEST=1','-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status_fixture))]")
 replace("possible=set(owned+sources)","possible=set(owned+sources)|{HERE/'test_game_uninstall_remote.c',HERE/'test_uninstall_release_unknown.c',HERE/'test_uninstall_release_worker.c',LIB/'native_backend.c',LIB/'native_backend_removal.h'}")
 replace("before={p.resolve():pin(p) for p in possible}","before={p.resolve():pin(p) for p in possible}")
 # The maintained runner generates narrow abort stubs for unused BW APIs;
 # include the actual worker's headers for those exact unused declarations.
 replace("stubs=['#include \"reader_preflight.h\"'", "stubs=['#include \"native_backend.h\"','#include \"launch_timing.h\"','#include \"source_directory.h\"','#include \"container_source.h\"','#include \"startup_runtime_control.h\"','#include <smb2/smb2.h>','#include <smb2/libsmb2.h>', '#include \"reader_preflight.h\"'")
 start=text.index("    scenarios=['send-only'");end=text.index('    if args.scenario:',start)
 text=text[:start]+"    scenarios=['release-core-before-intent','release-final-core-close-before-intent','release-adapter-before-intent','release-remote-status-before-intent','release-ordinary-metadata','release-ordinary-same-title']\n"+text[end:]
 # The worker's full backend has unused default SMB addresses: let the
 # collector emit explicit abort stubs for those unreachable branches too.
 marker="    stub=out/'unsupported-branches.c';"
 addition='''    smb_headers=(ROOT/'vendor/libsmb2/include/smb2/libsmb2.h').read_text()
    for name in sorted(n for n in needed-defined if n.startswith('smb2_')):
        matches=re.findall(r'((?:int|void|struct\\s+\\w+\\s*\\*)\\s*'+re.escape(name)+r'\\s*\\([^;{}]*\\))\\s*;',smb_headers)
        if len(matches)!=1:raise ValueError('Missing unused SMB declaration '+name)
        stubs.append(matches[0]+' { abort(); }')
'''
 replace(marker,addition+marker)
 runner=out/'recipe.py';runner.write_text(text)
 argv=[sys.executable,str(runner),'--output',str(out/'actual-chain')]
 if a.host_only:argv.append('--host-only')
 r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=360)
 (out/'runner.json').write_text(json.dumps({'argv':argv,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2)+'\n')
 if r.returncode:raise RuntimeError('Actual release chain failed; see runner.json')
 if pin(base)!=before or pin(Path(__file__))!=self_before:raise ValueError('Maintained recipe/checker changed during check')
 proof=json.loads((out/'actual-chain/proof.json').read_text());proof.update({'releaseUnknownActualWorkerChain':True,'preIntentFailuresNeverInventMutation':True,'ordinaryMetadataRefusalKeepsControlAuthority':True,'maintainedRecipe':before,'checker':self_before})
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'cases':proof['cases'],'checks':proof['checks'],'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
