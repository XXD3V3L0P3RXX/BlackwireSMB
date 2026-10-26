#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real read-only local-image and persisted-status exclusion proofs, no console."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,struct,subprocess,sys,tempfile
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc,wrapped
from fixtures_exfat import make_exfat

def pin(p):
    raw=p.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--host-only',action='store_true');args=ap.parse_args()
    out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build':raise ValueError('Fresh output below build required')
    out.mkdir(parents=True,exist_ok=False)
    sources=[LIB/'native_cleanup_remote.c',LIB/'source_param.c']+[STREAM/n for n in ('manifest.c','container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]+[HERE/'test_manual_image_sources.c']
    owned=sources+[Path(__file__),HERE/'test_registration_cleanup_io.h',HERE/'fixture_sdk.py',ROOT/'tests/compressed/fixtures.py',ROOT/'tests/compressed/fixtures_exfat.py',ROOT/'tools/build/app-recipe.json']
    possible=set(owned)|set(LIB.glob('*.h'))|set(STREAM.rglob('*.h'))|set((STREAM/'vendor/miniz').glob('*.c'))|set((ROOT/'vendor/libsmb2/include').rglob('*.h'))
    before={p.resolve():pin(p) for p in possible};commands=[];deps=[]
    def run(label,argv):
        argv=list(map(str,argv));r=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1','ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append({'label':label,'argv':argv,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode:raise RuntimeError(label+' failed; commands retained')
        return r.stdout
    flags=['clang-18','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DBW_COPY_HOST_TEST=1','-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/libsmb2/include']
    objects=[]
    for i,p in enumerate(sources):
        obj=out/f'{i}-{p.stem}.o';dep=obj.with_suffix('.d');deps.append(dep)
        seam=['-include',HERE/'test_registration_cleanup_io.h'] if p.name=='native_cleanup_remote.c' else []
        run('host-'+p.stem,[*flags,*seam,'-MD','-MF',dep,'-c',p,'-o',obj]);objects.append(obj)
    binary=out/'test-manual-image-sources';run('host-link',['clang-18','-fsanitize=address,undefined',*objects,'-Wl,--gc-sections','-o',binary])
    param=b'{"titleId":"PPSA99001","contentId":"NEUTRAL-FOREIGN"}'
    base={'sce_sys/param.json':param,'asset.bin':b'neutral synthetic data'}
    raw,layout=make_pfs(base);exfat,_=make_exfat(base)
    variants=[]
    def add(name,data=raw,status='coherent',fault='normal',refuse=False,release=False,setup=None):variants.append((name,data,status,fault,refuse,release,setup))
    for name,data in [('pfs',raw),('pfsc-pfs',pfsc(raw)),('wrapped-pfs',wrapped(raw,encode=False)),('wrapped-pfsc-pfs',wrapped(raw)),('exfat',exfat),('pfsc-exfat',pfsc(exfat)),('wrapped-exfat',wrapped(exfat,encode=False)),('wrapped-pfsc-exfat',wrapped(exfat))]:
        add('foreign-'+name,data)
        same={**base,'sce_sys/param.json':b'{"titleId":"PPSA90000","contentId":"OTHER-SAME-TITLE"}'}
        image=make_exfat(same)[0] if 'exfat' in name else make_pfs(same)[0]
        if name.startswith('wrapped-'):image=wrapped(image,encode='pfsc' in name)
        elif name.startswith('pfsc-'):image=pfsc(image)
        add('same-title-'+name,image,refuse=True)
    for label,body in [('missing',None),('zero',b''),('oversize',b'x'*65537),('bad-json',param+b'x'),('duplicate-title',b'{"titleId":"PPSA99001","titleId":"PPSA99002","contentId":"X"}'),('nested-title',b'{"nested":{"titleId":"PPSA99001"},"contentId":"X"}'),('bad-content',b'{"titleId":"PPSA99001","contentId":"has space"}'),('nul',param+b'\0')]:
        files=dict(base)
        if body is None:del files['sce_sys/param.json']
        else:files['sce_sys/param.json']=body
        add('param-'+label,make_pfs(files)[0],refuse=True)
    add('multiple-game-roots',make_pfs({**base,'nested/sce_sys/param.json':param})[0],refuse=True)
    add('root-case-alias',make_exfat({**base,'sce_sys/PARAM.JSON':param})[0],refuse=True)
    add('nested-unicode-directory-case',make_exfat({**base,'nested/\u017fce_sys/param.json':param})[0],refuse=True)
    add('nested-unicode-file-case',make_exfat({**base,'nested/sce_sys/param.j\u017fon':param})[0],refuse=True)
    add('signed-pfs',make_pfs(base,mode=0x10)[0],refuse=True)
    add('unknown-format',bytes(48),refuse=True)
    add('too-short',b'neutral',refuse=True)
    malformed=bytearray(raw);struct.pack_into('<I',malformed,layout['inodeOffsets'][layout['ids']['asset.bin']]+0x64,0)
    add('unrelated-invalid-extent',malformed,refuse=True)
    for state in ['absent','empty','deleted','selected-owned','trimmed-coherent','console-modes','unrelated-cusa','unrelated-fake','unrelated-other31']:add('status-'+state,status=state)
    for state in ['stale-selected','stale-selected-unlisted','stale-foreign','conflicting-cusa-image','empty-title','overlong-title','control-title','tab-title','overlong-path','overlong-name','duplicate','malformed','unknown','nul','oversize','symlink','directory']:add('status-'+state,status=state,refuse=True)
    for fault,release in [('image-eof',False),('image-drift',False),('status-drift',False),('image-during-status',False),('image-between-snapshots',False),('status-between-snapshots',False),('authority-lost',False),('image-close',True),('status-close',True),('status-parent-close',True)]:add(fault,fault=fault,refuse=True,release=release)
    add('image-eintr',fault='image-eintr')
    add('source-overlap',fault='source-overlap',refuse=True)
    add('image-symlink',refuse=True,setup='symlink');add('image-hardlink',refuse=True,setup='hardlink')
    reports=[]
    with tempfile.TemporaryDirectory(prefix='blackwire-manual-images-') as tmp:
        root=Path(tmp)
        for index,(name,data,state,fault,refuse,release,setup) in enumerate(variants):
            area=root/str(index);area.mkdir();image=area/'neutral.ffpfsc';image.write_bytes(data);status=area/'manual.status'
            row=f'installed\tPPSA99001\t{image}\tNeutral\n'
            if state=='empty':row=''
            elif state=='deleted':row=row.replace('installed','deleted',1)
            elif state=='selected-owned':row+='installed\tPPSA90000\t/data/blackwire/games/neutral-selected-source\tSelected\n'
            elif state=='trimmed-coherent':row=f' InStAlLeD \t PPSA99001 \t {image} \t Neutral \r\n'
            elif state=='stale-selected':row=row.replace('PPSA99001','PPSA90000')
            elif state=='stale-selected-unlisted':row+='installed\tPPSA90000\t/data/neutral-unlisted-image.ffpfsc\tNeutral\n'
            elif state=='stale-foreign':row=row.replace('PPSA99001','PPSA99002')
            elif state=='conflicting-cusa-image':row=row.replace('PPSA99001','CUSA00001')
            elif state.startswith('unrelated-'):
                other={'unrelated-cusa':'CUSA00001','unrelated-fake':'FAKE00001','unrelated-other31':'N'*31}[state]
                row+=f'installed\t{other}\t/data/neutral-unrelated-source\tUnrelated\n'
            elif state=='empty-title':row=row.replace('PPSA99001','')
            elif state=='overlong-title':row=row.replace('PPSA99001','N'*32)
            elif state=='control-title':row=row.replace('PPSA99001','NEU\x01TRAL')
            elif state=='tab-title':row=row.replace('PPSA99001','NEU\tTRAL')
            elif state=='overlong-path':row+=f'installed\tCUSA00001\t/{"n"*1023}\tNeutral\n'
            elif state=='overlong-name':row=row.replace('\tNeutral\n','\t'+'n'*256+'\n')
            elif state=='duplicate':row+=row
            elif state=='malformed':row='installed\tPPSA99001\n'
            elif state=='unknown':row=row.replace('installed','unknown',1)
            elif state=='nul':row+='\0'
            elif state=='oversize':row='x'*(1024*1024+1)
            if state!='absent':status.write_text(row)
            if state=='console-modes':area.chmod(0o777);status.chmod(0o666)
            if state=='symlink':status.rename(area/'real-status');status.symlink_to(area/'real-status')
            elif state=='directory':status.unlink();status.mkdir()
            if setup=='symlink':image.rename(area/'real-image');image.symlink_to(area/'real-image')
            elif setup=='hardlink':os.link(image,area/'second-link')
            baseline=data if not setup else None
            image_mode=image.lstat().st_mode;status_mode=status.lstat().st_mode if status.exists() or status.is_symlink() else None
            result=json.loads(run(name,[binary,image,status,fault,int(refuse),int(release)]));result['case']=name
            if image.lstat().st_mode!=image_mode or (status_mode is not None and status.lstat().st_mode!=status_mode):raise ValueError('Classification changed file permissions/type')
            if fault not in {'image-drift','status-drift','image-during-status','image-between-snapshots','status-between-snapshots'}:
                if baseline is not None and image.read_bytes()!=baseline:raise ValueError('Classification wrote source image')
                if state not in {'absent','symlink','directory'} and status.read_bytes()!=row.encode():raise ValueError('Classification wrote status')
            reports.append(result)
    sdk_objects={}
    if not args.host_only:
        from fixture_sdk import production_rows
        rows=production_rows(ROOT,out/'sdk-template')
        for name in ['native_cleanup_remote.c','container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c']:
            row=next(r for r in rows if '-c' in r['argv'] and Path(r['argv'][r['argv'].index('-c')+1]).name==name);argv=row['argv'].copy();obj=out/(Path(name).stem+'.sdk.o');dep=obj.with_suffix('.d');deps.append(dep);argv[argv.index('-o')+1]=str(obj);argv[argv.index('-MF')+1]=str(dep)
            run('sdk-'+name,argv);sdk_objects[name]=pin(obj)
    actual=set(owned)
    for dep in deps:
        for n in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1].split('\n\n',1)[0]):
            p=(Path(n) if Path(n).is_absolute() else ROOT/n).resolve()
            if p.is_relative_to(ROOT) and not p.is_relative_to(out):actual.add(p)
    for p in actual:
        if p not in before or before[p]!=pin(p):raise ValueError('Source changed or actual dependency unpinned: '+str(p))
    proof={'passed':True,'sourceBeforeAfterMatch':True,'cases':len(reports),'checks':sum(r['checks'] for r in reports),'observations':reports,'source':{p.relative_to(ROOT).as_posix():pin(p) for p in sorted(actual)},'sdkUnits':len(sdk_objects),'productionSdkObjects':sdk_objects,'consoleActions':0,'nativeRuntimeExecuted':False,'normalHttpSnapshotsModeled':True,'hostOnly':args.host_only}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'cases':len(reports),'checks':proof['checks'],'sdkUnits':len(sdk_objects),'proof':pin(out/'proof.json')}))

if __name__=='__main__':main()
