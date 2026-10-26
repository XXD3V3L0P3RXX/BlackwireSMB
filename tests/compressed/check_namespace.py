"""Actual typed companion namespaces and explicit preparation policy; no console."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,tempfile
from fixtures import make_pfs,pfsc,dir_blob
from fixtures_exfat import make_exfat

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    root=Path(__file__).resolve().parents[2];src=root/'src/blackwire';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    names=['library/source_namespace.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c']
    inputs=[src/n for n in names]+list(src.glob('library/source_*.h'))+[src/'streamreader/exfat_internal.h',src/'streamreader/container_source.h',src/'streamreader/pfs_source.h',root/'tests/compressed/namespace_test.c',Path(__file__),root/'tests/compressed/fixtures.py',root/'tests/compressed/fixtures_exfat.py']
    def pin(p):
        b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    before={str(p.relative_to(root)):pin(p) for p in inputs}
    command=['clang-18','-D_DEFAULT_SOURCE','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(src),'-I'+str(root/'vendor/libsmb2/include'),str(root/'tests/compressed/namespace_test.c'),*[str(src/n) for n in names],'-Wl,--gc-sections','-pthread','-o',str(out/'namespace')]
    commands=[]
    def run(label,argv):
        p=subprocess.run(argv,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1:detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append({'label':label,'argv':argv,'exitCode':p.returncode,'stdout':p.stdout,'stderr':p.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if p.returncode:raise RuntimeError(label+' failed; evidence retained')
        return p.stdout
    run('compile',command);results=[]
    base={'eboot.bin':b'BLACKWIRE NONEXECUTABLE FIXTURE','sce_sys/param.json':b'{"titleId":"PPSA99001"}','assets/stream.pak':b'asset'*200}
    variants=[('absent',{},(),0,0,True),('empty-f1',{},('fakelib',),1,0,True),('empty-f2',{},('fakelib2',),0,1,True),('both-f2-empty',{'fakelib/data.so':b'opaque shared module'},('fakelib2',),2,1,True),('both-metadata',{'fakelib/README':b'first metadata','fakelib2/README':b'preferred metadata','fakelib2/libanything.so':b'opaque module'},(),2,2,True),('file-root',{'fakelib':b'file, never directory'},(),3,0,False),('alias-empty',{},('FakeLib2',),0,0,False),('reserved',{'fakelib2/blackwire-reader.share':b'untrusted control'},(),0,2,False),('case-file-dir-collision',{'fakelib/data.so':b'directory child','FAKELIB':b'colliding file'},(),0,0,False)]
    variants += [('mixed-local-stream',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'opaque metadata'},(),0,2,True),('reserved-projection',{'fakelib2/Blackwire-reader.Projection':b'untrusted transient envelope'},(),0,2,False)]
    with tempfile.TemporaryDirectory(prefix='blackwire-namespace-') as work:
        temp=Path(work)
        for label,extra,directories,k1,k2,valid in variants:
            files={**base,**extra};folder=temp/label;folder.mkdir();game=folder/'fixture';game.mkdir()
            for directory in directories:(game/directory).mkdir(parents=True,exist_ok=True)
            for name,body in files.items():
                p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
            result=json.loads(run(label+'-folder',[str(out/'namespace'),'folder',str(folder),'-',str(k1),str(k2),str(int(valid))]));results.append({'case':label,'kind':'folder',**result})
            if any((game/name).read_bytes()!=body for name,body in files.items()):raise ValueError('Synthetic folder source changed')
            pfs,meta=make_pfs(files,directories=directories);exfat,_=make_exfat(files,directories=directories)
            for kind,image in [('pfs',pfs),('pfsc-pfs',pfsc(pfs)),('exfat',exfat),('pfsc-exfat',pfsc(exfat))]:
                path=temp/(label+'-'+kind+'.image');path.write_bytes(image)
                result=json.loads(run(label+'-'+kind,[str(out/'namespace'),'image',str(folder),str(path),str(k1),str(k2),str(int(valid))]));results.append({'case':label,'kind':kind,**result})
                if path.read_bytes()!=image:raise ValueError('Synthetic source image changed')
        # A link must never be flattened into an absent namespace.
        folder=temp/'linked';folder.mkdir();game=folder/'fixture';game.mkdir()
        for name,body in base.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
        (game/'fakelib').symlink_to('sce_sys',target_is_directory=True)
        results.append({'case':'linked','kind':'folder',**json.loads(run('linked-folder',[str(out/'namespace'),'folder',str(folder),'-','0','0','0']))})
        pfs,meta=make_pfs({**base,'fakelib/data.so':b'linked namespace'});struct.pack_into('<H',pfs,meta['inodeOffsets'][meta['ids']['fakelib']],0xa1ff)
        path=temp/'linked-pfs.image';path.write_bytes(pfs);results.append({'case':'linked','kind':'pfs',**json.loads(run('linked-pfs',[str(out/'namespace'),'image',str(folder),str(path),'0','0','0']))})
        # Same literal name declared as both file and directory in one parent.
        pfs,meta=make_pfs({**base,'fakelib/data.so':b'child','placeholder.bin':b'file'})
        entries=[('fakelib' if n=='placeholder.bin' else n,i,t) for n,i,t in meta['entries']['']]
        pfs[meta['bases'][meta['ids']['']]:meta['bases'][meta['ids']['']]+4096]=dir_blob(entries)[0]
        for kind,image in [('pfs',pfs),('pfsc-pfs',pfsc(pfs))]:
            path=temp/('file-dir-collision-'+kind+'.image');path.write_bytes(image);results.append({'case':'literal-file-dir-collision','kind':kind,**json.loads(run('literal-file-dir-collision-'+kind,[str(out/'namespace'),'image',str(folder),str(path),'0','0','0']))})
    after={str(p.relative_to(root)):pin(p) for p in inputs}
    if before!=after:raise RuntimeError('Source changed during verification')
    proof={'passed':True,'cases':len(results),'checks':sum(r['checks'] for r in results),'results':results,'source':after,'sourceBeforeAfterMatch':True,'syntheticSourceImagesAndFilesUnchanged':True,'consoleActions':0,'moduleExecution':False,'fullBuilds':0,'onlySyntheticSources':True,'projectionConsumersIntegrated':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:proof[k] for k in ('passed','cases','checks','consoleActions','projectionConsumersIntegrated')}))
if __name__=='__main__':main()
