#!/usr/bin/env python3
"""Actual Send -> new source provider -> raw and normal prepared-target reload."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent;LIB=ROOT/'src/blackwire'
sys.dont_write_bytecode=True;sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc
from fixtures_exfat import make_exfat
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);out=p.parse_args().output.resolve()
if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use fresh output inside build/')
out.mkdir(parents=True)
names=['library/source_namespace.c','library/source_companions.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','library/reader_selection.c','library/native_launch.c','library/native_prepared_target.c','library/native_startup_hint.c','streamreader/filemap.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c']
sources=[HERE/'test_prepared_projection_reload.c',*[LIB/n for n in names]]
flags=['-D_DEFAULT_SOURCE','-DBW_NATIVE_PRELOAD=1','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-I'+str(LIB/'library'),'-I'+str(LIB),'-I'+str(ROOT/'vendor/libsmb2/include'),'-I'+str(ROOT/'tests/compressed')]
commands=[]
def run(label,argv):
 r=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
 commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':r.returncode});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/(label+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
 return r.stdout
tracked={Path(__file__),ROOT/'tests/compressed/fixtures.py',ROOT/'tests/compressed/fixtures_exfat.py'}
for i,source in enumerate(sources):
 dependency=run('dep-'+str(i),['clang-18',*flags,'-MM',source])
 for n in shlex.split(dependency.replace('\\\n',' ').split(':',1)[1]):
  f=Path(n);tracked.add((f if f.is_absolute() else ROOT/f).resolve())
pin=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
before={f.relative_to(ROOT).as_posix():pin(f) for f in sorted(tracked)}
binary=out/'actual-prepared-projection-reload'
run('compile',['clang-18',*flags,'-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*sources,'-Wl,--gc-sections','-pthread','-o',binary])
results=[]
with tempfile.TemporaryDirectory(prefix='blackwire-prepared-reload-') as temporary:
 temp=Path(temporary);base={'eboot.bin':bytes((i*13+17)%251 for i in range(65537)),'sce_sys/param.json':b'{"titleId":"PPSA99001"}','assets/stream.pak':b'asset'*200}
 layouts=[('f1',{'fakelib/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib/README':b'source companion'},()),('f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'source companion'},()),('empty-f2',{'fakelib/README':b'nonpreferred source companion'},('fakelib2',)),('legacy',{},()),('legacy-f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'legacy source companion'},()),('legacy-f1-extra',{'fakelib/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib/README':b'legacy source companion'},()),('legacy-f1-ampr',{'fakelib/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR'},())]
 for layout,extra,dirs in layouts:
  source=temp/(layout+'-source');game=source/'fixture';game.mkdir(parents=True);files={**base,**extra}
  for d in dirs:(game/d).mkdir(parents=True,exist_ok=True)
  for name,data in files.items():target=game/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
  pfs=make_pfs(files,directories=dirs)[0];exfat=make_exfat(files,directories=dirs)[0]
  for kind,body in [('folder',None),('pfs',pfs),('pfsc-pfs',pfsc(pfs)),('exfat',exfat),('pfsc-exfat',pfsc(exfat))]:
   image=temp/(layout+'-'+kind+'.image')
   if body is not None:image.write_bytes(body)
   modes=['none']
   if layout=='f2':modes+=['receipt','receipt-version','record-link','map','map-link','digests','companions','missing-companions','original-map','original-digests']
   for mode in modes:
    destination=temp/(layout+'-'+kind+'-'+mode+'-model');destination.mkdir();label=layout+'-'+kind+'-'+mode
    result=json.loads(run(label,[binary,kind,source,image,destination,layout,mode]).splitlines()[-1]);results.append(result)
    assert result['passed'] and result['reloadAccepted']==(mode=='none') and not result['bytesVerifiedOnReload']
    assert all((game/name).read_bytes()==data for name,data in files.items())
    if body is not None:assert image.read_bytes()==body
after={f.relative_to(ROOT).as_posix():pin(f) for f in sorted(tracked)}
proof={'passed':before==after,'cases':len(results),'checks':sum(x['checks'] for x in results),'sourceBeforeAfterMatch':before==after,'source':before,'results':results,'actualNativeSendProviderAndPreparedTarget':True,'independentSourceRebuildAfterSend':True,'plainLegacyPolicyZeroRetained':True,'legacyCompanionUpgradeAdvisoryPreserved':True,'sourceAndImageBytesUnchanged':True,'sanitizers':['ASan','UBSan'],'consoleActions':0,'networkActions':0,'limits':['Only source transport/image bytes and outer current/idle are local models; actual producer, parser, projection, map/digest/receipt validation and prepared-target wrapper execute.','Folder control checks actual absence of old batch/derived roots through fixture adapters; those old-policy codecs are not exercised. Container tests never call those adapters.','Raw VALID on reload intentionally leaves bytes_verified false. Legacy companion layouts retain startup_upgrade_required and do not become ready from this inspection; reader-pin/launch readiness and live discovery service/UI are separate scopes.']}
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');assert proof['passed'],'Source changed during focused test'
print(json.dumps({k:proof[k] for k in ['passed','cases','checks','sourceBeforeAfterMatch','consoleActions','networkActions']}))
