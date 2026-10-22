#!/usr/bin/env python3
"""Actual Send-produced prepared-copy deletion and readonly SQLite; no console."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sqlite3
import struct
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
sys.path.insert(0,str(HERE))
import manifest_fixture as manifest
def pin(p):
    body=p.read_bytes();return dict(bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
def fixture(area,scenario,streams=32):
    shared=scenario.startswith('shared-apps-0770-')
    if shared:scenario=scenario[len('shared-apps-0770-'):]
    data=area/'data';data.mkdir();source=area/'source';source.mkdir();meta=area/'metadata';meta.mkdir();(area/'game.log').write_text('')
    (source/'sce_sys').mkdir();(source/'sce_module').mkdir();(source/'assets').mkdir();(area/'save').mkdir();(area/'save/saved-game.bin').write_bytes(b'preserved save')
    (area/'manual-full').mkdir();(area/'manual-full/eboot.bin').write_bytes(bytes(range(256))*1024)
    param=b'{"titleId":"PPSA99999","contentId":"UP0000-PPSA99999_00-BLACKWIREFIXTURE0"}'
    contents={'eboot.bin':bytes(range(256))*1024,'sce_sys/param.json':param,'sce_module/libfixture.sprx':bytes(range(255,-1,-1))*2048,'startup.txt':b'prepared non-executable LOCAL data'}
    fields=['192.0.2.1','FixtureShare','Fixture','PPSA99999'];binding=hashlib.sha256(b'BWBIND01'+b''.join(struct.pack('<I',len(s))+s.encode() for s in fields)).hexdigest();game='PPSA99999_'+binding
    rows=[]
    for name,body in contents.items():
        p=source/name;p.write_bytes(body);os.utime(p,(1700000000,1700000000));rows.append(manifest.File(name,len(body),1700000000,manifest.LOCAL))
    for i in range(streams):rows.append(manifest.File(f'assets/stream-{i:05}.pak',1024+i,1700000000,manifest.STREAM))
    rows.sort(key=lambda r:manifest.ascii_key(r.path));body=manifest.encode(rows);(meta/'map.bin').write_bytes(body)
    checksum=struct.unpack_from('<Q',body,32)[0];dig=b'BWDIG001'+struct.pack('<IIQ',1,len(rows),checksum)+hashlib.sha256(body).digest()
    dig+=b''.join(hashlib.sha256(contents[r.path]).digest() if r.flags==manifest.LOCAL else bytes(32) for r in rows);(meta/'digests.bin').write_bytes(dig)
    (meta/'selection.txt').write_text(f'version=2\nrun_id={"a"*32}\ntitle_id=PPSA99999\ngame={game}\neboot_size={len(contents["eboot.bin"])}\nmanifest_count={len(rows)}\nmanifest_hash={checksum:016x}\n')
    apps=area/'apps';apps.mkdir()
    if shared or scenario in ('late-shared-apps-mode','late-shared-apps-replacement'):apps.chmod(0o770)
    registered=scenario in ('registered','foreign-registration','pkg-registration','app-only-effect','lost-registration-reply','registration-pending')
    if registered:
        app=apps/'PPSA99999';app.mkdir();(app/'sce_sys').mkdir();(app/'sce_sys/param.json').write_bytes(param)
        root=data/'games'/game;(app/'mount.lnk').write_text(str(root if scenario!='foreign-registration' else area/'manual-full')+'\n')
        if scenario=='pkg-registration':(app/'app.pkg').write_bytes(b'foreign package')
    with sqlite3.connect(area/'app.db') as db:
        db.execute('CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)')
        if registered:db.execute('INSERT INTO tbl_contentinfo VALUES (?,?)',('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0'))
        if scenario=='foreign-row':db.execute('INSERT INTO tbl_contentinfo VALUES (?,?)',('PPSA99999','foreign-content'))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build/native-dashboard'):raise ValueError('Use a new scoped native-dashboard output')
    out.mkdir(parents=True,exist_ok=False);commands=[];deps=[]
    owned=[LIB/n for n in ['native_game_uninstall.c','native_game_uninstall.h','native_removed_sources.c','native_removed_sources.h']]+[Path(__file__).resolve(),HERE/'test_game_uninstall.c',HERE/'test_game_uninstall_io.h']
    sources=[HERE/'test_game_uninstall.c']+[LIB/n for n in ['native_game_uninstall.c','native_removed_sources.c','native_registration.c','registration_db.c','native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_profile.c','source_plan.c','source_param.c','source_directory.c']]+[STREAM/'manifest.c',STREAM/'share_config.c']
    possible=set(owned+sources)|set(LIB.glob('*.h'))|set(STREAM.rglob('*.h'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))|set((ROOT/'vendor/sqlite').glob('*.h'))|set((ROOT/'src/blackwire/tools/compressed_stream_candidate').rglob('*.h'))
    possible|={LIB/'native_send.c',LIB/'native_prepare_ops.c',LIB/'native_startup_upgrade.c',LIB/'native_launch_admission.c',HERE/'test_fast_ready_play.c',STREAM/'prepare_copy.c',ROOT/'vendor/sqlite/sqlite3.c'}
    before={p.resolve():pin(p) for p in possible}
    def run(label,argv):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode or re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:',r.stderr):raise RuntimeError(label+' failed; evidence retained')
        return r.stdout
    common=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-g','-ffunction-sections','-fdata-sections','-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include','-I',ROOT/'vendor/sqlite','-I',ROOT/'src/blackwire/tools/compressed_stream_candidate']
    host=['clang-18',*common,'-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_LAUNCH_TIMING=1']
    objects=[]
    for n,p in enumerate(sources):
        obj=out/f'host-{n}.o';dep=obj.with_suffix('.d');deps.append(dep)
        extra=['-include',HERE/'test_game_uninstall_io.h'] if p.name=='native_game_uninstall.c' else []
        run('host-'+p.stem,[*host,*extra,'-MD','-MF',dep,'-c',p,'-o',obj]);objects.append(obj)
    defined=set();needed=set()
    for p in objects:
        for line in run('symbols-'+p.stem,['nm','-g',p]).splitlines():
            fields=line.split()
            if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
    missing=sorted(n for n in needed-defined if n.startswith('bw_'))
    headers=[p for d in (LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate') for p in d.glob('*.h') if p.name!='native_json.h']
    declaration='\n'.join(p.read_text() for p in headers)
    stubs=['#include "reader_preflight.h"','#include "native_register.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include <stdlib.h>']
    for name in missing:
        matches=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declaration)
        if not matches:raise ValueError('Missing explicit abort-stub declaration: '+name)
        stubs.append(matches[0]+' { abort(); }')
    stub=out/'unsupported-branches.c';stub.write_text('\n'.join(stubs)+'\n');obj=out/'unsupported.o';run('unsupported-abort-stubs',[*host,'-Wno-unused-parameter','-Wno-c2x-extensions','-c',stub,'-o',obj]);objects.append(obj)
    sqlite=out/'sqlite.o';run('source-sqlite',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite]);objects.append(sqlite)
    binary=out/'game-uninstall-host';run('host-link',['clang-18','-fsanitize=address,undefined','-fno-sanitize-recover=all',*objects,'-Wl,--gc-sections','-Wl,--wrap=openat','-Wl,--wrap=readdir','-Wl,--wrap=pread','-Wl,--wrap=close','-pthread','-lm','-o',binary])
    scenarios=['normal','registered','repaired-generation','active','unknown-idle','scan-root','manual-source','manual-owned','source-safe-refused','wrong-confirmation','wrong-generation','missing-stream','changed-local','unknown-file','unknown-empty-dir','linked-local','hardlinked-local','allocated-stream','wrong-control','unknown-state','foreign-registration','pkg-registration','foreign-row','app-only-effect','lost-registration-reply','registration-pending','late-same-size-restored-mtime','late-inode-replacement','late-mode','late-hardlink','late-new-file','late-root-replacement','partial-intent','partial-registration','partial-files','partial-done','unlink-refused','unlink-uncertain','close-uncertain']
    scenarios+=['shared-apps-0770-'+case for case in ('normal','registered','foreign-registration','pkg-registration','foreign-row')]
    scenarios+=['owned-'+root+'-0770' for root in ('data','games','prepared','game','state')]
    scenarios+=['shared-apps-symlink','late-shared-apps-mode','late-shared-apps-replacement']
    observations=[]
    for scenario in scenarios:
        with tempfile.TemporaryDirectory(prefix='blackwire-game-uninstall-') as tmp:
            area=Path(tmp);fixture(area,scenario);observations.append(json.loads(run('case-'+scenario,[binary,area,scenario]).splitlines()[-1]))
    sdk=Path('/opt/ps5-payload-sdk');native=[]
    for p in (LIB/'native_game_uninstall.c',LIB/'native_removed_sources.c'):
        obj=out/(p.stem+'.sdk.o');dep=obj.with_suffix('.d');deps.append(dep);run('sdk-'+p.stem,[sdk/'bin/prospero-clang',*common,'-MD','-MF',dep,'-c',p,'-o',obj]);native.append(obj)
    actual=set(owned+sources)
    for dep in deps:
        for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/name).resolve())
    local={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
    for p in local:
        if p not in before or pin(p)!=before[p]:raise RuntimeError('Actual input changed during check: '+str(p))
    for p in local:
        dest=out/'frozen-source'/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
    proof=dict(passed=True,atUtc=datetime.now(timezone.utc).isoformat(),cases=sum(x['cases'] for x in observations),checks=sum(x['checks'] for x in observations),observations=observations,source={str(p.relative_to(ROOT)):before[p] for p in sorted(local)},sourceBeforeAfterMatch=True,hostBinary=pin(binary),sdkObjects={p.name:pin(p) for p in native},sdkInputs={str(p):pin(p) for p in sorted(actual-local) if p.is_file()},unsupportedAbortStubs=missing,actualSendProducedGenerations=True,actualReadonlySqlite=True,pcOriginalAndSavePreserved=True,consoleActions=0,fullBuilds=0,serviceIntegrated=False,commands=commands)
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=proof['cases'],checks=proof['checks'],proof=pin(out/'proof.json'))))
if __name__=='__main__':main()
