"""Actual native LOCAL copy/publication I/O and bounded syscall-fault checks."""
from pathlib import Path
import argparse, hashlib, json, os, shlex, subprocess, sys, tempfile
sys.dont_write_bytecode=True
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--expected-writes',type=int,default=2)
parser.add_argument('--expected-reads',type=int,default=3)
parser.add_argument('--publication-write-cap',type=int,choices=(65536,131072,262144),default=262144)
parser.add_argument('--publication-baseline',action='store_true',help='Preserve declared old-producer guard failures; never a candidate correctness pass')
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
    allowed=(expected,) if isinstance(expected,int) else expected
    if p.returncode not in allowed:raise RuntimeError(label+' failed; inspect retained log')
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
results=[];guard_negatives=[]
publication_callbacks=['publish-callback-current-root','publish-callback-idle-root','publish-callback-current-parent','publish-callback-idle-parent',
 'publish-short-callback-current-parent','publish-eintr-callback-idle-parent','publish-short-callback-idle-root','publish-eintr-callback-current-root',
 'publish-current-final-name','publish-idle-final-hardlink','publish-current-final-mode','publish-idle-final-size','publish-current-final-fd','publish-current-temp-name']
publication_faults=['publish-write-short','publish-write-eintr','publish-read-short','publish-read-eintr',
 'publish-write-short-current','publish-write-eintr-current','publish-write-short-idle','publish-write-eintr-idle',
 *publication_callbacks,'publish-write-error','publish-write-zero','publish-write-overlong','publish-read-error','publish-read-corrupt',
 'publish-file-fsync','publish-directory-fsync','publish-unlink-denied','publish-unlink-uncertain','publish-close','publish-abrupt-exit']
with tempfile.TemporaryDirectory(prefix='blackwire-native-local-copy-') as temporary:
    temp=Path(temporary)
    for length in (65537,131073,262143,262144,262145,1049307):
        folder=temp/('source-'+str(length));game=folder/'fixture';game.mkdir(parents=True)
        body=bytes((i*13+17)%251 for i in range(length))
        files={'eboot.bin':body,'startup/boot.sprx':body,'startup/empty.sprx':b'','sce_sys/param.json':b'{"titleId":"PPSA99001"}','fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED AMPR','fakelib2/README':b'LOCAL COMPANION METADATA','assets/stream.pak':b'asset'*200}
        for name,data in files.items():p=game/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        pfs=make_pfs(files)[0]
        for kind,image in (('folder',None),('pfs',pfs),('pfsc',pfsc(pfs))):
            path=temp/(str(length)+'-'+kind+'.image')
            if image is not None:path.write_bytes(image)
            modes=['publish-none']
            if length in (65537,1049307):modes+=['none','write-short','write-eintr','read-short','read-eintr','retained-no-unmapped']
            if length==1049307:
                modes+=['write-error','write-zero','write-overlong','write-short-current','write-eintr-current','write-short-idle','write-eintr-idle','callback-current-root','callback-idle-root','callback-current-parent','callback-idle-parent','read-error','read-corrupt','file-fsync','consume-corrupt','source-generation','cancel','temp-collision','final-collision','temp-symlink','final-symlink','final-hardlink','mode-change','abrupt-exit','file-substitution','file-hardlink','root-substitution','final-close','cleanup-denied','cleanup-uncertain']
                if kind in ('folder','pfsc'):modes+=publication_faults
            for mode in modes:
                destination=temp/(str(length)+'-'+kind+'-'+mode+'-model');destination.mkdir()
                arguments=[binary,kind,folder,path,destination,length,hashlib.sha256(body).hexdigest(),mode]
                if mode in ('abrupt-exit','publish-abrupt-exit'):
                    interrupted=json.loads(run(str(length)+'-'+kind+'-'+mode,arguments,77).splitlines()[-1]);assert interrupted['interrupted']
                    partial=Path(interrupted['temporary']);final=Path(interrupted['final'])
                    assert partial.is_relative_to(destination) and final.is_relative_to(destination)
                    assert partial.is_file()
                    if mode=='abrupt-exit':assert 0<partial.stat().st_size<length and not final.exists()
                    else:assert interrupted['publication'] and partial.stat().st_size==length and final.is_file() and 0<final.stat().st_size<length
                    arguments[-1]='inspect-interrupted';report=json.loads(run(str(length)+'-'+kind+('-publish-inspect-interrupted' if mode=='publish-abrupt-exit' else '-inspect-interrupted'),arguments).splitlines()[-1])
                    if mode=='publish-abrupt-exit':report.update(mode=mode,publicationPartialBytes=final.stat().st_size,verifiedTemporaryBytes=partial.stat().st_size)
                else:
                    label=str(length)+'-'+kind+'-'+mode
                    text=run(label,arguments,(0,2) if args.publication_baseline and mode in publication_callbacks else 0)
                    if commands[-1]['exitCode']:
                        log=(out/(label+'.log')).read_text()
                        assert mode in publication_callbacks and 'publication-write-after-mutation' in log
                        report={'passed':False,'mode':mode,'format':kind,'bodyBytes':length,'expectedOldGuardFailure':True,
                          'postMutationWriteAttempts':log.count('publication-write-after-mutation'),'log':label+'.log','exitCode':commands[-1]['exitCode']}
                        guard_negatives.append(report)
                        assert all((game/name).read_bytes()==data for name,data in files.items())
                        if image is not None:assert path.read_bytes()==image
                        continue
                    report=json.loads(text.splitlines()[-1])
                results.append(report);assert report['passed'] and report['bodyBytes']==length
                if report['accepted'] and mode!='retained-no-unmapped':
                    assert report['localWriteBytes']==args.expected_writes*length and report['localReadBytes']==args.expected_reads*length
                    assert report['consumerFullCurrentCalls']==report['consumerNativeWriteAttempts'] and report['consumerIdleCalls']==report['consumerNativeWriteAttempts']
                    assert report['temporary']['maxWriteRequested']<=65536 and report['final']['maxWriteRequested']<=args.publication_write_cap
                    if mode=='publish-none':
                        assert report['final']['writeCalls']==(length+args.publication_write_cap-1)//args.publication_write_cap
                        assert report['final']['maxWriteRequested']==min(length,args.publication_write_cap)
                        assert report['temporary']['writeCalls']==(length+65535)//65536
                if mode=='retained-no-unmapped':assert report['localWriteBytes']==0
                assert all((game/name).read_bytes()==data for name,data in files.items())
                if image is not None:assert path.read_bytes()==image

after={str(p.relative_to(root)):pin(p) for p in sorted(tracked)}
if args.publication_baseline:
    for kind in ('folder','pfsc'):
        for mode in publication_callbacks[:4]:assert any(row['format']==kind and row['mode']==mode for row in guard_negatives),'Old callback-parent guard gap was not reproduced'
proof={'passed':before==after and not args.publication_baseline,'cases':len(results)+len(guard_negatives),'successfulContractCases':len(results),'checks':sum(r['checks'] for r in results),'sourceBeforeAfterMatch':before==after,'source':before,'results':results,'actualNativeSendProviderNpCopyAndFilesystem':True,'expectedAcceptedWritePasses':args.expected_writes,'expectedAcceptedReadPasses':args.expected_reads,'freshFullCurrentAndParentGuardOnEveryNativeWriteAttempt':True,'combinedRetryRevocationAndCallbackParentReplacementRefused':True,'publicationWriteCap':args.publication_write_cap,'publicationFaultFormats':['folder','pfsc'],'publicationGuardNegativeResults':guard_negatives,'expectedNegativeProofPassed':args.publication_baseline and before==after and bool(guard_negatives),'freshPublicationAfterCallbackGuardsPassed':not args.publication_baseline and not guard_negatives,'syntheticSourceUnchanged':True,'sanitizers':'ASan+UBSan','consoleActions':0,'networkActions':0,'testDoesNotEditProduction':True,'elapsedGainMeasured':False,'limits':['Only source transport/image bytes and outer authority/idle are modeled; actual provider, digest, np_copy, fsync, local publication, final readback, index and preparation receipt execute','Boot body FDs are identified from the actual manifest row/open and held FD identities; small control/other LOCAL files are excluded from body-I/O totals','The reported byte counters describe local filesystem body I/O, not elapsed time, SMB wire speed or cold source-cache performance','Current/idle counters count the supplied authority callback boundary inside actual ns_current; backend settings-query cost and production process-enumeration timing are not modeled','Consumer counters include only the selected LOCAL np_write invocation; whole-Send and source DATA authority counts are reported separately','Publication faults are separately armed after actual TEMP pread with the exact FINAL FD held; they do not mirror the producer write loop','Explicit successful full local verification runs after counters are frozen','Faults are one-shot wrappers or declared fixture namespace/source-observation mutations; real source files remain byte-identical','Consumer corruption models changed delivered bytes; source-generation faults change the modeled metadata observation, never original PC game bytes','Abrupt producer exit covers TEMP and partial FINAL host-process termination, followed by actual read-only inspect; no power-cycle or console filesystem durability is claimed','The retained-no-unmapped refusal preserves the earlier valid receipt and body; no new completion is published','Baseline mode preserves genuine old guard failures and is not a candidate correctness pass']}
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');assert before==after,'Source changed during test'
print(json.dumps({k:proof[k] for k in ('passed','expectedNegativeProofPassed','cases','checks','sourceBeforeAfterMatch','consoleActions','networkActions','elapsedGainMeasured')}))
