"""Actual native LOCAL copy/publication I/O and bounded syscall-fault checks."""
from pathlib import Path
import argparse, hashlib, json, os, shlex, subprocess, sys, tempfile
sys.dont_write_bytecode=True
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--expected-writes',type=int,default=2)
parser.add_argument('--expected-reads',type=int,default=3)
args=parser.parse_args();root=Path(__file__).resolve().parents[3];out=args.output.resolve();fixture=Path(__file__).with_name('test_native_local_copy.c')
if not out.is_relative_to(root/'build') or out.exists():raise ValueError('Use a fresh output under source build/')
out.mkdir(parents=False)
sys.path.insert(0,str(root/'tests/compressed'))
from fixtures import make_pfs,pfsc
lib=root/'src/blackwire'
names=['library/source_namespace.c','library/source_companions.c','library/source_provider.c','library/source_prepare.c','library/source_smb.c','library/source_directory.c','library/source_snapshot.c','library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_identity.c','library/source_param.c','library/reader_selection.c','library/native_launch.c','streamreader/filemap.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c','streamreader/share_config.c']
sources=[fixture,*[lib/n for n in names]]
flags=['-D_DEFAULT_SOURCE','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-I'+str(lib/'library'),'-I'+str(lib),'-I'+str(root/'vendor/libsmb2/include'),'-I'+str(root/'tests/compressed')]
commands=[]
def run(label,argv,expected=0):
    row={'label':label,'argv':list(map(str,argv))}
    p=subprocess.run(row['argv'],cwd=root,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
    row['exitCode']=p.returncode;commands.append(row);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/(label+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode!=expected:raise RuntimeError(label+' failed; inspect retained log')
    return p.stdout
pin=lambda p:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
tracked={Path(__file__).resolve(),fixture,root/'tests/compressed/fixtures.py'}
for i,source in enumerate(sources):
    dep=run('dependencies-'+str(i),['clang-18',*flags,'-MM',source])
    for token in shlex.split(dep.replace('\\\n',' ').split(':',1)[1]):
        p=Path(token);tracked.add((p if p.is_absolute() else root/p).resolve())
before={str(p.relative_to(root)):pin(p) for p in sorted(tracked)}
binary=out/'actual-native-local-copy'
run('compile-actual-native-send',['clang-18',*flags,'-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*sources,'-Wl,--gc-sections',*['-Wl,--wrap='+n for n in ('read','pread','write','fsync','openat','close','unlinkat','bw_source_provider_read')],'-pthread','-o',binary])
results=[]
with tempfile.TemporaryDirectory(prefix='blackwire-native-local-copy-') as temporary:
    temp=Path(temporary)
    for length in (65537,1049307):
        folder=temp/('source-'+str(length));game=folder/'fixture';game.mkdir(parents=True)
        body=bytes((i*13+17)%251 for i in range(length))
        files={'eboot.bin':body,'startup/boot.sprx':body,'sce_sys/param.json':b'{"titleId":"PPSA99001"}','fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'LOCAL COMPANION METADATA','assets/stream.pak':b'asset'*200}
        for name,data in files.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        pfs=make_pfs(files)[0]
        for kind,image in (('folder',None),('pfs',pfs),('pfsc',pfsc(pfs))):
            path=temp/(str(length)+'-'+kind+'.image')
            if image is not None:path.write_bytes(image)
            modes=['none','write-short','write-eintr','read-short','read-eintr','retained-no-unmapped']
            if length>262144:modes+=['write-error','write-zero','write-overlong','write-short-current','write-eintr-current','write-short-idle','write-eintr-idle','callback-current-root','callback-idle-root','callback-current-parent','callback-idle-parent','read-error','read-corrupt','file-fsync','consume-corrupt','source-generation','cancel','temp-collision','final-collision','temp-symlink','final-symlink','final-hardlink','mode-change','abrupt-exit','file-substitution','file-hardlink','root-substitution','final-close','cleanup-denied','cleanup-uncertain']
            for mode in modes:
                destination=temp/(str(length)+'-'+kind+'-'+mode+'-model');destination.mkdir()
                arguments=[binary,kind,folder,path,destination,length,hashlib.sha256(body).hexdigest(),mode]
                if mode=='abrupt-exit':
                    interrupted=json.loads(run(str(length)+'-'+kind+'-'+mode,arguments,77).splitlines()[-1]);assert interrupted['interrupted']
                    partial=Path(interrupted['temporary']);final=Path(interrupted['final'])
                    assert partial.is_relative_to(destination) and final.is_relative_to(destination)
                    assert partial.is_file() and 0<partial.stat().st_size<length and not final.exists()
                    arguments[-1]='inspect-interrupted';report=json.loads(run(str(length)+'-'+kind+'-inspect-interrupted',arguments).splitlines()[-1])
                else:report=json.loads(run(str(length)+'-'+kind+'-'+mode,arguments).splitlines()[-1])
                results.append(report);assert report['passed'] and report['bodyBytes']==length
                if report['accepted'] and mode!='retained-no-unmapped':
                    assert report['localWriteBytes']==args.expected_writes*length and report['localReadBytes']==args.expected_reads*length
                    assert report['consumerFullCurrentCalls']==report['consumerNativeWriteAttempts'] and report['consumerIdleCalls']==report['consumerNativeWriteAttempts']
                if mode=='retained-no-unmapped':assert report['localWriteBytes']==0
                assert all((game/name).read_bytes()==data for name,data in files.items())
                if image is not None:assert path.read_bytes()==image

after={str(p.relative_to(root)):pin(p) for p in sorted(tracked)}
proof={'passed':before==after,'cases':len(results),'checks':sum(r['checks'] for r in results),'sourceBeforeAfterMatch':before==after,'source':before,'results':results,'actualNativeSendProviderNpCopyAndFilesystem':True,'expectedAcceptedWritePasses':args.expected_writes,'expectedAcceptedReadPasses':args.expected_reads,'freshFullCurrentAndParentGuardOnEveryNativeWriteAttempt':True,'combinedRetryRevocationAndCallbackParentReplacementRefused':True,'syntheticSourceUnchanged':True,'sanitizers':'ASan+UBSan','consoleActions':0,'networkActions':0,'testDoesNotEditProduction':True,'elapsedGainMeasured':False,'limits':['Only source transport/image bytes and outer authority/idle are modeled; actual provider, digest, np_copy, fsync, local publication, final readback, index and preparation receipt execute','Boot body FDs are identified from the actual manifest row/open and held FD identities; small control/other LOCAL files are excluded from body-I/O totals','The reported byte counters describe local filesystem body I/O, not elapsed time, SMB wire speed or cold source-cache performance','Current/idle counters count the supplied authority callback boundary inside actual ns_current; backend settings-query cost and production process-enumeration timing are not modeled','Consumer counters include only the selected LOCAL np_write invocation; whole-Send and source DATA authority counts are reported separately','Explicit successful full local verification runs after counters are frozen','Faults are one-shot wrappers or declared fixture namespace/source-observation mutations; real source files remain byte-identical','Consumer corruption models changed delivered bytes; source-generation faults change the modeled metadata observation, never original PC game bytes','Abrupt producer exit is a host process termination before publication, followed by real read-only inspect; no power-cycle or console filesystem durability is claimed','The retained-no-unmapped refusal preserves the earlier valid receipt and body; no new completion is published']}
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');assert proof['passed'],'Source changed during test'
print(json.dumps({k:proof[k] for k in ('passed','cases','checks','sourceBeforeAfterMatch','consoleActions','networkActions','elapsedGainMeasured')}))
