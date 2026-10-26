#!/usr/bin/env python3
"""Old Send/step7 refusal -> actual default offline Uninstall, host only."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shlex,struct,subprocess,sys,tempfile
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
sys.path.insert(0,str(HERE));import manifest_fixture as manifest
spec=importlib.util.spec_from_file_location('old_source_fixture',HERE/'check_game_uninstall.py');fixtures=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixtures)
def pin(p):
    b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();out=a.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Fresh build output required')
    out.mkdir(parents=True);commands=[]
    manual_status=out/'manual.status';manual_status.write_bytes(b'installed\tCUSA00001\t/data/neutral-existing-cusa\tNeutral fixture\n');manual_status.chmod(0o666)
    def run(label,argv):
        result=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':result.returncode,'stdout':result.stdout,'stderr':result.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode or re.search(r'runtime error:|ERROR: AddressSanitizer',result.stderr):raise RuntimeError(label+' failed; evidence retained')
        return result.stdout
    names=['native_game_uninstall.c','native_removed_sources.c','native_game_uninstall_remote.c','native_registration_lifecycle.c','native_registration_cleanup.c','native_cleanup_remote.c','native_registration.c','registration_db.c','native_prepared_target.c','reader_selection.c','source_binding.c','source_identity.c','source_namespace.c','source_companions.c','source_profile.c','source_plan.c','source_param.c','source_directory.c','preload_bundle.c']
    sources=[HERE/'test_legacy_source_removal.c']+[LIB/n for n in names]+[STREAM/n for n in ('manifest.c','share_config.c','container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]
    owned={Path(__file__).resolve(),HERE/'test_legacy_source_removal_wrapper.c',*sources}
    possible=set(owned)|set(LIB.glob('*.h'))|set(LIB.glob('*.c'))|set(STREAM.rglob('*.h'))|{HERE/n for n in ('test_game_uninstall_remote.c','test_game_uninstall_remote_io.h','test_fast_ready_play.c','test_game_uninstall_io.h','check_game_uninstall.py','manifest_fixture.py')}
    possible.update(STREAM.glob('*.c'));possible.update((STREAM/'vendor/miniz').glob('*.c'));possible.update((ROOT/'vendor/libsmb2/include').rglob('*.h'));possible.update((ROOT/'vendor/sqlite').glob('*.h'));possible.add(ROOT/'vendor/sqlite/sqlite3.c');possible.update((ROOT/'src/blackwire/tools/compressed_stream_candidate').glob('*.h'))
    before={p.resolve():pin(p) for p in possible}
    flags=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_COPY_HOST_TEST=','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1','-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-DBW_LAUNCH_TIMING=1','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-DBW_SEND_DIAGNOSTIC=1','-DBW_SEND_LOG_PATH='+json.dumps(str(out/'send-diagnostic.log'))]
    for directory in (LIB,STREAM,HERE,ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate'):flags+=['-I',directory]
    flags+=['-DBW_NRC_HOST_MANUAL_STATUS_PATH='+json.dumps(str(manual_status))]
    objects=[];deps=[]
    for i,source in enumerate(sources):
        target=out/('host-'+str(i)+'.o');dep=target.with_suffix('.d');deps.append(dep);extra=[]
        if source.name=='native_game_uninstall.c':extra=['-include',HERE/'test_game_uninstall_io.h']
        if source.name=='native_game_uninstall_remote.c':extra=['-include',HERE/'test_game_uninstall_remote_io.h']
        run('compile-'+source.stem,[*flags,*extra,'-MD','-MF',dep,'-c',source,'-o',target]);objects.append(target)
    bridge=out/'default-wrapper.o';run('compile-default-wrapper',[*flags,'-MD','-MF',out/'wrapper.d','-c',HERE/'test_legacy_source_removal_wrapper.c','-o',bridge]);deps.append(out/'wrapper.d')
    bridge_root=out/'default-wrapper-root.o';run('root-actual-default-caller',['ld.lld-18','-r','--gc-sections','--undefined=bw_test_default_legacy_remove',bridge,'-o',bridge_root]);objects.append(bridge_root)
    image=bytes((i*23+9)%251 for i in range(2048));image_c=out/'image.c';image_c.write_text('#include "preload_bundle.h"\nstatic const unsigned char image[]={'+','.join(map(str,image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={image,sizeof image,{'+','.join(map(str,hashlib.sha256(image).digest()))+'},"'+'0'*64+'",10000};\n');image_o=out/'image.o';run('compile-image',[*flags,'-c',image_c,'-o',image_o]);objects.append(image_o)
    defined=set();needed=set()
    for obj in objects:
        for line in run('symbols-'+obj.stem,['nm','-g',obj]).splitlines():
            fields=line.split()
            if len(fields)>=2:(needed if fields[-2]=='U' else defined).add(fields[-1])
    missing=sorted(n for n in needed-defined if n.startswith('bw_'))
    declarations='\n'.join(p.read_text() for d in (LIB,STREAM,ROOT/'src/blackwire/tools/compressed_stream_candidate') for p in d.glob('*.h') if p.name!='native_json.h')
    stubs=['#include "reader_preflight.h"','#include "native_register.h"','#include "native_prepared_target.h"','#include "native_derived_context.h"','#include "native_batch_context.h"','#include "source_namespace.h"','#include <stdlib.h>']
    for name in missing:
        matches=re.findall(r'((?:int|void|const\s+struct\s+\w+\s*\*|struct\s+\w+\s*\*)\s*'+re.escape(name)+r'\s*\([^;{}]*\))\s*;',declarations)
        if not matches:raise ValueError('Missing explicit unsupported-branch declaration '+name)
        stubs.append(matches[0]+' { abort(); }')
    stub=out/'unsupported.c';stub.write_text('\n'.join(stubs)+'\n');obj=out/'unsupported.o';run('compile-unsupported-branches',[*flags,'-Wno-unused-parameter','-Wno-c2x-extensions','-c',stub,'-o',obj]);objects.append(obj)
    sqlite=out/'sqlite.o';run('compile-source-sqlite',['clang-18','-O0','-std=gnu11','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite]);objects.append(sqlite)
    binary=out/'legacy-source-default-removal';run('link',['clang-18','-fsanitize=address,undefined',*objects,'-Wl,--gc-sections',*['-Wl,--wrap='+n for n in ('openat','readdir','pread','close')],'-pthread','-lm','-o',binary])
    results=[]
    for scenario in ('normal','unknown-extra','allocated-stream'):
        with tempfile.TemporaryDirectory(prefix='blackwire-old-source-removal-') as temp:
            area=Path(temp);fixtures.fixture(area,'normal');source=area/'source';meta=area/'metadata';(source/'fakelib2').mkdir()
            raw=(meta/'map.bin').read_bytes();count=struct.unpack_from('<I',raw,16)[0];pool=raw[40+count*32:];rows=[]
            for i in range(count):
                offset,length,flags_value,_,size,mtime=struct.unpack_from('<IIIIQQ',raw,40+i*32);rows.append(manifest.File(pool[offset:offset+length].decode(),size,mtime,flags_value))
            files={'fakelib2/libSceAmpr.sprx':b'ORIGINAL UNEXECUTED SOURCE AMPR','fakelib2/README.txt':b'Legacy STREAM metadata'}
            files.update({f'fakelib2/foreign-{i}.sprx':b'SOURCE MODULE '+bytes([i])*9 for i in range(8)})
            for name,body in files.items():
                path=source/name;path.write_bytes(body);os.utime(path,(1700000000,1700000000));rows.append(manifest.File(name,len(body),1700000000,manifest.STREAM if name.endswith('.txt') else manifest.LOCAL))
            rows.sort(key=lambda row:manifest.ascii_key(row.path));raw=manifest.encode(rows);(meta/'map.bin').write_bytes(raw);checksum=struct.unpack_from('<Q',raw,32)[0]
            dig=b'BWDIG001'+struct.pack('<IIQ',1,len(rows),checksum)+hashlib.sha256(raw).digest()+b''.join(hashlib.sha256((source/r.path).read_bytes()).digest() if r.flags==manifest.LOCAL else bytes(32) for r in rows);(meta/'digests.bin').write_bytes(dig)
            selection=(meta/'selection.txt').read_text();selection=re.sub(r'manifest_count=\d+',f'manifest_count={len(rows)}',selection);selection=re.sub(r'manifest_hash=[0-9a-f]+',f'manifest_hash={checksum:016x}',selection);(meta/'selection.txt').write_text(selection)
            tested=json.loads(run('case-'+scenario,[binary,area,scenario]).splitlines()[-1]);results.append({'scenario':scenario,**tested})
            if any((source/name).read_bytes()!=body for name,body in files.items()):raise ValueError('Original source companion changed')
    actual=set(owned)
    for dep in deps:
        for name in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):actual.add((ROOT/name).resolve())
    actual={p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
    for p in actual:
        if p not in before or before[p]!=pin(p):raise RuntimeError('Actual input changed during qualification: '+str(p))
    proof={'passed':True,'cases':sum(r['cases'] for r in results),'checks':sum(r['checks'] for r in results),'results':results,'source':{str(p.relative_to(ROOT)):pin(p) for p in sorted(actual)},'sourceBeforeAfterMatch':True,'unsupportedBranchesAbort':missing,'scope':'Actual old strict Send record/index, old bundle refusal, actual default selector wrapper, ordinary SMP adapter, core full inventory+delete and read-only SQLite. Provider-ready input tables and normal SMP replies are independent host fixtures; no transport or module execution.','consoleActions':0,'moduleExecution':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:proof[k] for k in ('passed','cases','checks')}))
if __name__=='__main__':main()
