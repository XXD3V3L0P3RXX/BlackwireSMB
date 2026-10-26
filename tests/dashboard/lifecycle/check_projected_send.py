"""Actual projected native Send/index/receipt/inspect; no console contact."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent;SRC=ROOT/'src/blackwire'
sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc
from fixtures_exfat import make_exfat
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sources=['library/source_namespace.c','library/source_companions.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','library/reader_selection.c','library/native_launch.c','streamreader/filemap.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c']
    inputs=[SRC/n for n in sources]+list((SRC/'library').glob('source_*.h'))+[SRC/'library'/n for n in ('native_send.c','native_send.h','native_companion_state.c','native_prepare_ops.c','native_startup_upgrade.c','native_launch_admission.c','native_launch_admission.h')]+[HERE/'test_projected_send.c',ROOT/'tests/compressed/namespace_test.c',Path(__file__)]
    def pin(p):
        b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    before={str(p.relative_to(ROOT)):pin(p) for p in inputs};commands=[]
    def run(label,argv):
        p=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':p.returncode,'stdout':p.stdout,'stderr':p.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if p.returncode:raise RuntimeError(label+' failed; see retained commands')
        return p.stdout
    command=['clang-18','-D_DEFAULT_SOURCE','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(SRC/'library'),'-I'+str(SRC),'-I'+str(ROOT/'vendor/libsmb2/include'),str(HERE/'test_projected_send.c'),*[str(SRC/n) for n in sources],'-Wl,--gc-sections','-Wl,--wrap=close','-Wl,--wrap=write','-pthread','-o',str(out/'test')]
    run('compile',command);results=[];base={'eboot.bin':b'BLACKWIRE NONEXECUTABLE FIXTURE','sce_sys/param.json':b'{"titleId":"PPSA99001"}','assets/stream.pak':b'asset'*200}
    cases=[('no-dirs',{},(),False,'normal'),('f1',{'fakelib/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib/README':b'first metadata','fakelib/helper.so':b'opaque module'},(),True,'normal'),('f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'preferred metadata','fakelib2/helper.so':b'opaque module'},(),True,'normal'),('empty-f2',{'fakelib/README':b'nonpreferred'},('fakelib2',),True,'normal'),('receipt-tamper',{'fakelib2/README':b'metadata'},(),True,'receipt-tamper'),('original-tamper',{'fakelib2/README':b'metadata'},(),True,'original-tamper'),('stray',{},(),False,'stray-receipt'),('migrate-f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'metadata'},(),True,'migrate'),('profile-limit',{f'fakelib/m{i:03}.so':b'module' for i in range(128)},(),True,'profile-limit')]
    cases += [(name,{'fakelib2/README':b'metadata'},(),True,name) for name in ('partial-state','close-state','source-change')]
    with tempfile.TemporaryDirectory(prefix='blackwire-projected-send-') as work:
        temp=Path(work)
        for label,extra,dirs,projected,scenario in cases:
            folder=temp/(label+'-source');folder.mkdir();game=folder/'fixture';game.mkdir();files={**base,**extra}
            for directory in dirs:(game/directory).mkdir(parents=True,exist_ok=True)
            for name,body in files.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
            images=[('folder',None),('pfs',make_pfs(files,directories=dirs)[0]),('pfsc',pfsc(make_pfs(files,directories=dirs)[0])),('exfat',make_exfat(files,directories=dirs)[0])]
            for kind,image in images:
                path=temp/(label+'-'+kind+'.image')
                if image is not None:path.write_bytes(image)
                data=temp/(label+'-'+kind+'-console-model');data.mkdir()
                result=json.loads(run(label+'-'+kind,[out/'test','folder' if kind=='folder' else 'image',folder,path,data,scenario,int(projected)]));results.append({'case':label,'kind':kind,**result})
                if image is not None and path.read_bytes()!=image:raise ValueError('Source image changed')
                if any((game/name).read_bytes()!=body for name,body in files.items()):raise ValueError('Folder source changed')
    after={str(p.relative_to(ROOT)):pin(p) for p in inputs}
    if before!=after:raise RuntimeError('Source changed during verification')
    proof={'passed':True,'cases':len(results),'checks':sum(r['checks'] for r in results),'source':after,'sourceBeforeAfterMatch':True,'results':results,'actualSendRecordAndIndex':True,'actualOfflineInspectAndLOCALVerification':True,'sourceUnchanged':True,'consoleActions':0,'projectionExecutionConsumersQualified':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:proof[k] for k in ('passed','cases','checks','consoleActions')}))
if __name__=='__main__':main()
