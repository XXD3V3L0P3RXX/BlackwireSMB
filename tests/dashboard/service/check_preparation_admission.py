#!/usr/bin/env python3
"""Actual initial Send admission and same-call idle diagnostics, offline only."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,sqlite3,struct,subprocess,tempfile,zlib

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
LIB=ROOT/'src/blackwire/library'
SERVICE=ROOT/'src/blackwire/dashboard/service'
MODEL=ROOT/'src/blackwire/dashboard/model'
STREAM=ROOT/'src/blackwire/streamreader'

def pin(path):
    body=path.read_bytes()
    return {'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()}

def fixture(base):
    source=base/'full-local';apps=base/'apps';app=apps/'PPSA99911'
    for path in (source/'sce_sys',app/'sce_sys'):path.mkdir(parents=True)
    param=json.dumps({'titleId':'PPSA99911','contentId':'EP0000-PPSA99911_00-BLACKWIRETEST0001',
                      'titleName':'Local fixture','applicationDrmType':'standard'}).encode()
    (source/'sce_sys/param.json').write_bytes(param)
    (app/'sce_sys/param.json').write_bytes(param)
    (source/'eboot.bin').write_bytes(b'fixture-not-a-console-game'*3)
    (source/'eboot.bin').chmod(0o755)
    (app/'mount.lnk').write_text(str(source),encoding='utf-8')
    def chunk(kind,body):
        return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    icon=app/'sce_sys/icon0.png'
    icon.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))
                     +chunk(b'IDAT',zlib.compress(b'\0\0\0\0\xff'))+chunk(b'IEND',b''))
    database=base/'app.db'
    with sqlite3.connect(database) as connection:
        connection.execute('CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)')
        connection.execute('INSERT INTO tbl_contentinfo VALUES(?,?)',
                           ('PPSA99911','EP0000-PPSA99911_00-BLACKWIRETEST0001'))
    document=base/'games.json'
    document.write_text(json.dumps({'status':0,'count':1,'games':[{
        'path':str(source),'runtime_path':str(source),'source_type':'folder',
        'title_id':'PPSA99911','title_name':'Local fixture',
        'content_id':'EP0000-PPSA99911_00-BLACKWIRETEST0001','installed':True,
        'installed_pkg':False,'managed':True,'source_available':True,'image_backed':False}]}),encoding='utf-8')
    return [document,apps,database,icon]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():
        raise ValueError('Use a fresh output under build/')
    out.mkdir(parents=True)
    sources=[SERVICE/name for name in ('dashboard_service.c','catalog_adapter.c','catalog_snapshot.c',
                                     'frontend_identity.c','local_http.c')]
    sources += [MODEL/'dashboard_model.c',LIB/'source_param.c',LIB/'registration_db.c',
                LIB/'native_request.c',LIB/'native_removal_request.c',LIB/'native_setup.c',
                STREAM/'manifest.c',STREAM/'diagnostics/game_stop_identity.c',HERE/'test_preparation_admission.c']
    inputs=set(sources+[Path(__file__),ROOT/'vendor/sqlite/sqlite3.c',SERVICE/'resident.c'])
    commands=[]
    proof={'schema':'blackwire-preparation-admission-proof/1','passed':False,'commands':commands,
           'consoleActions':0,'networkActions':0,'fullBuilds':0}
    flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
           '-Wno-unused-function','-Wno-unused-parameter','-ffunction-sections','-fdata-sections',
           '-DBD_FRONTEND_IDENTITY_HOST_TEST=1','-DBW_NATIVE_SETUP_HOST_TEST=1']
    includes=[]
    for path in (LIB,SERVICE,STREAM,ROOT/'vendor/sqlite',ROOT/'vendor/libsmb2/include',HERE.parent/'lifecycle'):
        includes += ['-I',str(path)]
    def run(label,argv):
        argv=list(map(str,argv))
        result=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=180,env={**os.environ,
            'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1',
            'PYTHONDONTWRITEBYTECODE':'1'})
        stdout=label+'.stdout';stderr=label+'.stderr'
        (out/stdout).write_bytes(result.stdout);(out/stderr).write_bytes(result.stderr)
        commands.append({'label':label,'argv':argv,'cwd':str(ROOT),'exitCode':result.returncode,
                         'stdout':stdout,'stderr':stderr,'stdoutSha256':hashlib.sha256(result.stdout).hexdigest(),
                         'stderrSha256':hashlib.sha256(result.stderr).hexdigest()})
        (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
        if result.returncode:raise RuntimeError(label+' failed; outputs retained')
        return result.stdout
    try:
        proof['compiler']=run('compiler-version',['clang-18','--version']).decode().splitlines()[0]
        for index,source in enumerate(sources):
            dependencies=run('dependencies-'+str(index),['clang-18',*flags,*includes,'-MM',source]).decode()
            for name in shlex.split(dependencies.replace('\\\n',' ').split(':',1)[1]):
                path=Path(name);inputs.add((path if path.is_absolute() else ROOT/path).resolve())
        before={str(path.relative_to(ROOT)):pin(path) for path in sorted(inputs)}
        proof['sourceBefore']=before
        run('sqlite-source',['clang-18','-std=gnu11','-O0','-DSQLITE_THREADSAFE=1',
                            '-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',
                            ROOT/'vendor/sqlite/sqlite3.c','-o',out/'sqlite.o'])
        binary=out/'preparation-admission'
        run('actual-admission-build',['clang-18',*flags,*includes,'-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer',*sources,out/'sqlite.o',
            '-Wl,--gc-sections','-Wl,--wrap=fopen','-Wl,--wrap=fclose','-pthread','-lm','-o',binary])
        start='[GAME] started: PPSA99001 pid=24\n'
        stop='[GAME] finalizing stopped application: PPSA99001 pid=24\n'
        cases={
            'readable-empty':'','matching-stopped':start+stop,'stale-errno-empty':'',
            'missing':None,'open-noerrno':'','open-denied':'','unmatched-started':start,
            'malformed-pid':'[GAME] started: PPSA99001 pid=invalid\n',
            'capacity':''.join('[GAME] finalizing stopped application: PPSA%05d pid=24\n'%i for i in range(129)),
            'overlong-line':'x'*1100+'\n',
            'short-eof':'[GAME] started: PPSA99001 pid=77\n[GAME] finalizing stopped application: PPSA99001 pid=77',
            'read-error':'','read-noerrno':'','close-error':'','close-noerrno':'','read-close-error':'',
            'second-unmatched':'','second-read-error':'','second-close-error':'',
            'unmatched-close-error':'','short-close-error':'','legacy-unmatched':start,'legacy-empty':''}
        # Stock 1.7beta7 emits no later START after a successful process
        # handoff. FINAL controls include the replacement's own awaiting event;
        # they qualify only the inherited logical log endpoint, not cleanup.
        stamp='[2026-10-27 00:00:00]   '
        def started(title='PPSA99001',pid=24,app=0x41):
            return stamp+'[GAME] started: %s pid=%d app_id=0x%08X\n'%(title,pid,app)
        def awaiting(title='PPSA99001',pid=24,app=0x41):
            return stamp+'[GAME] process exited, awaiting same-app handoff: %s pid=%d app_id=0x%08X\n'%(title,pid,app)
        def handoff(title='PPSA99001',old=24,new=25,app=0x41):
            return stamp+'[GAME] process handoff: %s app_id=0x%08X pid=%d -> pid=%d\n'%(title,app,old,new)
        def final(title='PPSA99001',pid=24):
            return stamp+'[GAME] finalizing stopped application: %s pid=%d\n'%(title,pid)
        stock_start=started();stock_await=awaiting();stock_handoff=handoff()
        handoff_cases={
            'handoff-awaiting':stock_start+stock_await,
            'handoff-active':stock_start+stock_await+stock_handoff,
            'handoff-final-new':stock_start+stock_await+stock_handoff+awaiting(pid=25)+final(pid=25),
            'handoff-stale-final-old':stock_start+stock_await+stock_handoff+final(),
            'handoff-direct-active':stock_start+stock_handoff,
            'handoff-direct-final-new':stock_start+stock_handoff+awaiting(pid=25)+final(pid=25),
            'handoff-pending-same-pid-active':stock_start+stock_await+handoff(new=24),
            'handoff-pending-same-pid-final':stock_start+stock_await+handoff(new=24)+stock_await+final(),
            'handoff-direct-same-pid-invalid':stock_start+handoff(new=24),
            'handoff-zero-app-active':started(app=0)+awaiting(app=0)+handoff(app=0),
            'handoff-zero-known-final':started(app=0)+awaiting(app=0)+stock_handoff+awaiting(pid=25)+final(pid=25),
            'handoff-known-app-mismatch':stock_start+stock_await+handoff(app=0x42),
            'handoff-orphan':stock_handoff,
            'handoff-wrong-old-pid':stock_start+stock_await+handoff(old=23),
            'handoff-malformed':stock_start+stock_await+stock_handoff.replace(' -> ',' => '),
            'handoff-trailing':stock_start+stock_await+stock_handoff[:-1]+' extra\n',
            'cusa-start':started('CUSA99001'),
            'lapy-start':started('LAPY99001'),
            'fake-start':started('FAKE99001'),
            'stock-families-final':''.join(started(title)+awaiting(title)+final(title)
                for title in ('CUSA99001','LAPY99001','FAKE99001'))}
        assert len(cases)==23 and len(handoff_cases)==20 and not cases.keys()&handoff_cases.keys()
        cases.update(handoff_cases)
        results=[]
        for name,body in cases.items():
            with tempfile.TemporaryDirectory(prefix='blackwire-initial-admission-') as temporary:
                base=Path(temporary);paths=fixture(base);log=base/'lifecycle.log'
                if body is not None:log.write_text(body,encoding='utf-8')
                result=json.loads(run(name,[binary,name,*paths,log]));results.append(result)
                if name in handoff_cases:assert log.read_text(encoding='utf-8')==body,'Static stock-event fixture was changed'
        after={str(path.relative_to(ROOT)):pin(path) for path in sorted(inputs)}
        refused=[row for row in results if row['httpStatus']==409]
        proof.update(sourceAfter=after,sourceBeforeAfterMatch=before==after,cases=len(results),
                     checks=sum(row['checks'] for row in results),results=results,
                     actualServicePreparationBegin=True,actualNativeSetupIdleParser=True,
                     sameCallFirstAndSecondFailure=True,legacyCallbackGenericPreserved=True,
                     partialEofAndCloseFailureRefuse=True,readAndCloseFirstFailurePreserved=True,
                     knownMissingErrnoAndStaleErrnoReset=True,noExtraIdleObservation=True,
                     originalIdleIoAndLegacyCasesRetained=23,stockHandoffAndFamilyCases=20,
                     stockHandoffPidAndAppChains=True,stockSupportedFamiliesExcludedWhenActive=True,
                     finalizingEndpointLogicalOnly=True,
                     refusedBeforeReservationAndBackendDispatch=all(row['reserveCallsAtResponse']==0
                         and row['backendDispatchCallsAtResponse']==0 for row in refused),
                     sanitizers=['ASan','UBSan'],hostBinary=pin(binary),
                     residentWiringSourcePinnedNotExecuted=True)
        proof['seams']=[
            'WEB_ONLY configured: native frontend admission and optional BF observer disabled',
            'Catalog/auth/native backend status, reservation and backend202 acceptance are inherited fixed host fixtures',
            'Only exact /data/shadowmount/debug.log fopen is redirected; private libc streams/fopencookie inject read/close faults',
            'Actual current service preparation_begin, native_setup_idle(_observe), and copy_fs parser execute',
            'Accepted controls stop at modeled backend acceptance, never a real game job or Ready result']
        proof['limits']=[
            'No console, SMB, runtime firmware, gameplay, native frontend or payload compatibility qualification',
            'Historical lifecycle log is not a live process census',
            'A matched finalizing record is only the inherited logical endpoint; it does not certify completed cleanup or runtime release',
            'The old-PID finalization after handoff is a synthetic stale-completion guard case, not an asserted complete stock event ordering',
            'This establishes reason ownership and fail-closed admission, not the cause of the external Reddit report',
            'Resident creation/ticker is not executed; its callback wiring source is pinned']
        proof['passed']=before==after and len(results)==43 and all(row['passed'] for row in results) and proof['refusedBeforeReservationAndBackendDispatch']
    except Exception as error:
        proof['error']=str(error)
        if 'sourceBefore' in proof:
            proof['sourceAfter']={str(path.relative_to(ROOT)):pin(path) for path in sorted(inputs)}
            proof['sourceBeforeAfterMatch']=proof['sourceBefore']==proof['sourceAfter']
        raise
    finally:
        (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    if not proof['passed']:raise RuntimeError('Focused admission proof did not qualify the frozen inputs')
    print(json.dumps({key:proof[key] for key in ('passed','cases','checks','sourceBeforeAfterMatch','consoleActions')}))

if __name__=='__main__':main()
