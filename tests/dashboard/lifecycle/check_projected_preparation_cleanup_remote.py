#!/usr/bin/env python3
"""Actual projected Send + normal read-only SMP exclusion + preparation cleanup."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];S=ROOT/'src/blackwire';sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc
from fixtures_exfat import make_exfat
pin=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--advisory-close-only',action='store_true');parser.add_argument('--expect-old-close-unknown',action='store_true')
args=parser.parse_args();out=args.output.resolve()
if args.expect_old_close_unknown and not args.advisory_close_only:raise ValueError('Old expected-red mode is limited to the focused close cases')
if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use fresh output under build/')
out.mkdir(parents=True)
status=out/'manual.status';status.write_bytes(b'');status.chmod(0o600)
names=['library/source_namespace.c','library/source_companions.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','library/reader_selection.c','library/native_game_uninstall.c','library/native_removed_sources.c','library/registration_db.c','library/native_registration_cleanup.c','library/native_registration.c','library/native_prepared_target.c','library/preload_bundle.c','library/native_game_uninstall_remote.c','library/native_cleanup_remote.c','library/native_registration_lifecycle.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c']
sources=[HERE/'test_projected_preparation_cleanup_remote.c',*[S/n for n in names]]
flags=['-D_DEFAULT_SOURCE','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(status)),'-I'+str(S/'library'),'-I'+str(S),'-I'+str(ROOT/'vendor/libsmb2/include'),'-I'+str(ROOT/'vendor/sqlite')]
commands=[]
def run(label,args):
 r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
 commands.append({'label':label,'argv':list(map(str,args)),'exitCode':r.returncode});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/(label+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(label+' failed; see retained log')
 return r.stdout
tracked={Path(__file__),*sources,ROOT/'vendor/sqlite/sqlite3.c',ROOT/'tests/compressed/fixtures.py',ROOT/'tests/compressed/fixtures_exfat.py'}
for i,source in enumerate(sources):
 dep=run('dep-'+str(i),['clang-18',*flags,'-MM',source])
 for name in shlex.split(dep.replace('\\\n',' ').split(':',1)[1]):
  p=Path(name);tracked.add((p if p.is_absolute() else ROOT/p).resolve())
before={str(p.relative_to(ROOT)):pin(p) for p in sorted(tracked)}
image=out/'reader-image.c';body=b'CURRENT NONEXECUTABLE READER FIXTURE'*64
image.write_text('#include "preload_bundle.h"\nstatic const uint8_t bytes[]={'+','.join(map(str,body))+'};\nconst struct bw_preload_image bw_preload_embedded_image={bytes,sizeof bytes,{'+','.join(map(str,hashlib.sha256(body).digest()))+'},"'+'6'*64+'",1000};\n')
sql=out/'sqlite.o';run('sqlite',['clang-18','-std=gnu11','-O0','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sql])
binary=out/'preparation-cleanup-remote';run('host-build',['clang-18',*flags,'-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*sources,image,sql,'-Wl,--gc-sections','-Wl,--wrap=close','-Wl,--wrap=bw_nrc_candidates','-pthread','-lm','-o',binary])
base={'eboot.bin':b'BLACKWIRE NONEXECUTABLE FIXTURE','sce_sys/param.json':b'{"titleId":"PPSA99001","contentId":"UP0000-PPSA99001_00-BLACKWIREFIXTURE0"}','assets/stream.pak':b'asset'*200}
layouts=[('f1',{'fakelib/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib/README.txt':b'metadata'},()),('f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README.txt':b'metadata'},()),('empty-f2',{'fakelib/README.txt':b'nonpreferred'},('fakelib2',))]
results=[]
with tempfile.TemporaryDirectory(prefix='blackwire-preparation-remote-') as temporary:
 temp=Path(temporary)
 for layout,extra,dirs in layouts:
  if args.advisory_close_only and layout!='f1':continue
  parent=temp/(layout+'-source');game=parent/'fixture';game.mkdir(parents=True);files={**base,**extra}
  for d in dirs:(game/d).mkdir(parents=True,exist_ok=True)
  for name,data in files.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  pfs=make_pfs(files,directories=dirs)[0];exfat=make_exfat(files,directories=dirs)[0]
  formats=[('folder',None),('pfs',pfs),('pfsc-pfs',pfsc(pfs)),('exfat',exfat),('pfsc-exfat',pfsc(exfat))]
  for kind,original in formats:
   if args.advisory_close_only and kind!='folder':continue
   source_image=temp/(layout+'-'+kind+'.image')
   if original is not None:source_image.write_bytes(original)
   modes=['missing-link','foreign-db','app-absent','link-foreign']
   if layout=='f1' and kind=='folder':modes+=['owned-other-binding','owned-same-binding','owned-other-corrupt','owned-other-linked','owned-other-close-unknown']
   if args.advisory_close_only:modes=['owned-other-corrupt','owned-other-close-unknown-old' if args.expect_old_close_unknown else 'owned-other-close-unknown']
   if layout=='f2':modes+=['other-app-root','other-app-state','other-app-outside','other-app-no-link','other-app-linked-directory','other-app-linked-mount','other-app-nonregular-mount','other-app-malformed-mount','other-app-oversized-mount','other-app-changing-inventory','ordinary-consumed','ordinary-other-binding','ordinary-completed','ordinary-intent-malformed','ordinary-intent-linked','ordinary-registration-missing','ordinary-registration-modified','link-state','link-state-parent','link-state-alias','link-state-child','link-root','link-parent','link-child','link-alias','link-malformed','link-unreadable','late-link','app-replacement','root-rename','scan-root','scan-alias','changing-settings','malformed-settings','settings-error','read-error','malformed-games','changing-games','cached-mounted','cached-installed','cached-managed','cached-pkg','cached-foreign','malformed-manual','malformed-status','manual-present','unknown-manual','manual-post-present','manual-post-changing','manual-post-error','capability-unknown','active','unknown-idle','foreign-reader']
   if layout=='f2' and kind=='folder':modes+=['ordinary-unrelated']
   for mode in modes:
    console=temp/(layout+'-'+kind+'-'+mode);console.mkdir();label=layout+'-'+kind+'-'+mode
    status_bytes=b'not-valid-status\n' if mode=='malformed-status' else b'';status.write_bytes(status_bytes)
    arguments=[binary,'folder' if kind=='folder' else 'image',parent,source_image,console,mode]
    alternate_files={}
    if mode=='ordinary-unrelated':
     alternate=temp/'unrelated-source';alternate_game=alternate/'fixture';alternate_game.mkdir(parents=True)
     alternate_files={name:data.replace(b'PPSA99001',b'PPSA99002') if name=='sce_sys/param.json' else data for name,data in files.items()}
     for name,data in alternate_files.items():p=alternate_game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
     arguments[2]=alternate;arguments.append(parent)
    result=json.loads(run(label,arguments).splitlines()[-1]);results.append({'layout':layout,'format':kind,**result});assert status.read_bytes()==status_bytes
    if alternate_files:assert all((alternate_game/name).read_bytes()==data for name,data in alternate_files.items())
    if original is not None:assert source_image.read_bytes()==original
    assert all((game/name).read_bytes()==data for name,data in files.items())
after={str(p.relative_to(ROOT)):pin(p) for p in sorted(tracked)}
owner_cases=[r for r in results if r['mode'].startswith('owned-')]
assert len(owner_cases)==(2 if args.advisory_close_only else 5) and all(r['actualNrcOwnerProducer'] and r['sameTitleOwnerAdvisoryRefused'] and not r['advisoryCanRemoveStartup'] and r['ownerAndBothCopiesPreserved'] for r in owner_cases)
close_case=next(r for r in owner_cases if r['mode'].startswith('owned-other-close-unknown'))
assert close_case['actualInventoryCloseUnconfirmed'] and close_case['advisoryNoStickyLatch'] and close_case['closeFaultInjections']==2 and close_case['actualNrcResult']==-2 and close_case['advisoryCloseUnconfirmedDistinct']==(not args.expect_old_close_unknown)
proof={'passed':before==after and not args.expect_old_close_unknown,'expectedRedReproduced':before==after and args.expect_old_close_unknown,'focusedAdvisoryCloseOnly':args.advisory_close_only,'cases':len(results),'checks':sum(r['checks'] for r in results),'actualProjectedProducerWitnessAndNormalCleanup':True,'actualNrcCandidateAndRemoteCollectors':True,'actualEffectiveRootExclusion':True,'actualOrdinaryNormalConsumedHistoryAndCompletedRestore':True,'sameTitleOwnerAdvisoryMatchesAction':True,'sameTitleOwnerAdvisoryCases':len(owner_cases),'actualInventoryCloseUnconfirmed':True,'advisoryCloseUnconfirmedDistinct':not args.expect_old_close_unknown,'privatePreparationStateMountExclusion':True,'actualCrossPurposeMalformedLinkedAndPhaseFaults':True,'unrelatedPendingTitleDoesNotBlock':True,'differentTitleApplicationMountExclusionAndInventoryFaults':True,'sourceBeforeAfterMatch':before==after,'source':before,'results':results,'sourceAndSavesPreserved':True,'externalAppAndDbUnchanged':True,'mutationTransportCalls':sum(r['mutationTransportCalls'] for r in results),'consoleActions':0,'networkActions':0,'limits':['Only fixed stock-SMP transport replies and outer full-idle callback are modeled; actual JSON parsing, persisted-status reading, typed filesystem exclusion and cleanup core run','Synthetic non-executable source and reader bytes; no platform or network execution','Same-title owner records are emitted by actual NRC begin/finish; platform app/SQLite registration effects and manual membership are modeled','Close uncertainty is injected only after real close succeeds on the exact NRC inventory marker FD; actual collector result is observed, not replaced','Late-link, app-replacement, other-app inventory addition and source-rename are deliberate fixture mutations; no file deletion by core is accepted in those cases','Other-app unreadable ambiguity is exercised by nofollow symlink and nonregular/oversized mount records; host EACCES permissions are not modeled as a runtime syscall fault']}
if args.advisory_close_only:
 for key in ('actualProjectedProducerWitnessAndNormalCleanup','actualEffectiveRootExclusion','actualOrdinaryNormalConsumedHistoryAndCompletedRestore','privatePreparationStateMountExclusion','actualCrossPurposeMalformedLinkedAndPhaseFaults','unrelatedPendingTitleDoesNotBlock','differentTitleApplicationMountExclusionAndInventoryFaults'):proof[key]=False
 proof['limits'].append('Focused mode executes only the malformed-owner control and actual inventory-close advisory/action case; the complete cleanup matrix is not run')
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');assert before==after,'Source changed during test; rerun frozen'
print(json.dumps({k:proof[k] for k in ('passed','cases','checks','sourceBeforeAfterMatch','mutationTransportCalls','consoleActions')}))
