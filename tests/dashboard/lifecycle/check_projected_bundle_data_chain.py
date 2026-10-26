#!/usr/bin/env python3
"""Extend the frozen real producer/DATA fixture with actual bundle consumers."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent;SRC=ROOT/'src/blackwire'
sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc
from fixtures_exfat import make_exfat
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);out=parser.parse_args().output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Fresh build output required')
    out.mkdir(parents=True)
    sources=['library/source_namespace.c','library/source_companions.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','library/reader_selection.c','library/reader_projection.c','library/preload_bundle.c','reader/preload/source.c','streamreader/filemap.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/container_smb.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c','streamreader/cache.c']
    fixture=HERE/'test_projected_data_chain.c';inputs=[SRC/n for n in sources]+list((SRC/'library').glob('*.h'))+list((SRC/'streamreader').glob('*.h'))+list((SRC/'reader/preload').glob('*.h'))+[SRC/'library'/n for n in ('native_send.c','native_companion_state.c','native_prepare_ops.c','native_startup_upgrade.c','native_launch_admission.c')]+[fixture,ROOT/'tests/compressed/namespace_test.c',ROOT/'tests/compressed/fixtures.py',ROOT/'tests/compressed/fixtures_exfat.py',Path(__file__)]
    def pins():return {str(p.relative_to(ROOT)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs}
    before=pins();commands=[]
    def run(label,argv):
        p=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':p.returncode,'stdout':p.stdout,'stderr':p.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if p.returncode:raise RuntimeError(label+' failed; evidence retained')
        return p.stdout
    original=fixture.read_text();needle='    C(!bw_native_projection_handoff_bytes(owner,&selected,&envelope,&envelope_size)&&envelope&&envelope_size);'
    if original.count(needle)!=1:raise ValueError('Frozen chain extension point changed')
    extra='''    /* Additional real consumer calls; original fixture body is unchanged. */
    const struct bw_preload_companions *witness=NULL;C(!bw_native_launch_admission_companions(owner,&witness)&&witness);
    char prepared_root[1500];C(snprintf(prepared_root,sizeof prepared_root,"%s/games/%s",argv[3],binding.binding.game)>0);
    struct bw_preload_bundle_request bundle={prepared_root,current,&context,now()+20};struct bw_preload_bundle_result bundled={0};
    unsigned source_requests=context.requests,source_reads=sync_reads;
    C(!bw_preload_bundle_install_projection_checked(&bundle,witness,&bundled)&&bundled.mutation_started&&!bundled.release_unknown);
    C(!bw_preload_bundle_verify_projection_checked(&bundle,witness,&bundled)&&!bundled.mutation_started&&!bundled.release_unknown);
    C(source_requests==context.requests&&source_reads==sync_reads);
'''
    driver=out/'actual-chain-with-bundle.c';driver.write_text('#define _GNU_SOURCE 1\n#include "preload_bundle.h"\n'+original.replace(needle,extra+needle).replace('#include "../../compressed/namespace_test.c"','#include "'+str(ROOT/'tests/compressed/namespace_test.c')+'"'))
    image=bytes((i*23+9)%251 for i in range(2048));image_c=out/'image.c';image_c.write_text('#include "preload_bundle.h"\nstatic const unsigned char image[]={'+','.join(map(str,image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={image,sizeof image,{'+','.join(map(str,hashlib.sha256(image).digest()))+'},"'+'0'*64+'",10000};\n')
    run('compile',['clang-18','-D_DEFAULT_SOURCE','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(SRC/'library'),'-I'+str(SRC),'-I'+str(ROOT/'vendor/libsmb2/include'),driver,image_c,*[SRC/n for n in sources],'-Wl,--gc-sections','-pthread','-o',out/'test'])
    results=[];base={'eboot.bin':b'BLACKWIRE INERT EBOOT FIXTURE','sce_sys/param.json':b'{"titleId":"PPSA99001"}','assets/stream.pak':bytes((i*13+7)%251 for i in range(180017))}
    cases=[('f1',{'fakelib/libSceAmpr.sprx':b'ORIGINAL INERT AMPR','fakelib/README':b'first metadata','fakelib/helper.so':b'opaque module'},()),('f2',{'fakelib2/libSceAmpr.sprx':b'ORIGINAL INERT AMPR','fakelib2/README':b'preferred metadata','fakelib2/helper.so':b'opaque module'},()),('both',{'fakelib/README':b'first metadata','fakelib2/README':b'preferred metadata','fakelib2/helper.so':b'opaque module'},()),('empty-f2',{'fakelib/README':b'nonpreferred'},('fakelib2',))]
    with tempfile.TemporaryDirectory(prefix='blackwire-bundle-data-chain-') as work:
        temp=Path(work)
        for label,extra,dirs in cases:
            files={**base,**extra}
            for kind in ('folder','pfs','pfsc','exfat'):
                share=temp/(label+'-'+kind+'-share');game=share/'fixture';game.mkdir(parents=True)
                for directory in dirs:(game/directory).mkdir(parents=True,exist_ok=True)
                for name,body in files.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
                image=None
                if kind=='pfs':image=make_pfs(files,directories=dirs)[0]
                elif kind=='pfsc':image=pfsc(make_pfs(files,directories=dirs)[0])
                elif kind=='exfat':image=make_exfat(files,directories=dirs)[0]
                if image is not None:(share/'fixture.ffpfsc').write_bytes(image)
                output=temp/(label+'-'+kind+'-console-model');output.mkdir();result=json.loads(run(label+'-'+kind,[out/'test','folder' if kind=='folder' else 'image',share,output,label]));results.append({'case':label,'kind':kind,'actualHeldWitnessBundleInstallAndVerify':True,**result})
                if any((game/name).read_bytes()!=body for name,body in files.items()) or (image is not None and (share/'fixture.ffpfsc').read_bytes()!=image):raise ValueError('Original source changed')
    if before!=pins():raise RuntimeError('Source changed during qualification')
    proof={'passed':True,'cases':len(results),'checks':sum(r['checks'] for r in results),'results':results,'source':before,'sourceBeforeAfterMatch':True,'frozenBaselinePreserved':True,'extendedDriverSha256':hashlib.sha256(driver.read_bytes()).hexdigest(),'actualProducerSourceAdmissionWitnessBundleEnvelopeDATARead':True,'transportOnlyModeled':True,'moduleExecution':False,'consoleActions':0,'gameplayOrNativeRuntimeQualified':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:proof[k] for k in ('passed','cases','checks','consoleActions')}))
if __name__=='__main__':main()
