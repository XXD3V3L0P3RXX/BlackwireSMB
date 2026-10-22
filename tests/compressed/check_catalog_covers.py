#!/usr/bin/env python3
"""Synthetic real-container artwork checks; no game or network inputs."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
from fixtures import make_pfs,pfsc,wrapped
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    out=parser.parse_args().output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh output below build/')
    out.mkdir(parents=True)
    names=['library/source_catalog.c','library/source_param.c','library/source_plan.c',
           'streamreader/manifest.c','streamreader/container_source.c','streamreader/container_smb.c',
           'streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c']
    files=[ROOT/'src/blackwire'/name for name in names]+[Path(__file__).with_name('catalog_cover_test.c')]
    def pins():return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    before=pins();commands=[]
    def run(label,argv):
        result=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,timeout=120,
                              env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        commands.append(dict(label=label,argv=list(map(str,argv)),exitCode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode:raise RuntimeError(label+' failed; see commands.json')
        return result.stdout
    binary=out/'catalog-cover-test'
    flags=['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror',
           '-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
           '-I',ROOT/'src/blackwire','-I',ROOT/'vendor/libsmb2/include']
    run('compile-actual-catalog-and-container-artwork',[*flags,*files,'-Wl,--gc-sections','-o',binary])
    icon=b'\x89PNG\r\n\x1a\n'+b'SYNTHETIC-ARTWORK'*21
    base={'eboot.bin':b'SYNTHETIC-STARTUP','sce_sys/param.json':b'{"titleId":"PPSA09999","titleName":"Fixture"}'}
    raw,_=make_pfs({**base,'sce_sys/icon0.png':icon})
    missing,_=make_pfs(base);oversize,_=make_pfs({**base,'sce_sys/icon0.png':bytes(4*1024*1024+1)})
    variants=[('pfs',raw,len(icon)),('pfsc',pfsc(raw),len(icon)),('wrapped',wrapped(raw),len(icon)),
              ('missing',missing,0),('oversize',pfsc(oversize),4*1024*1024+1)]
    reports=[]
    with tempfile.TemporaryDirectory(prefix='blackwire-catalog-covers-') as work:
        for index,(label,data,size) in enumerate(variants):
            image=Path(work)/(label+'.ffpfsc');image.write_bytes(data)
            reports.append(json.loads(run(label,[binary,image,size,index])))
    if before!=pins():raise ValueError('Source changed during check')
    proof=dict(passed=True,cases=sum(p['cases'] for p in reports),checks=sum(p['checks'] for p in reports),
               variants=reports,inputPins=before,sourceBeforeAfterMatch=True,syntheticOnly=True,
               consoleContacted=False,networkContacted=False)
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({k:v for k,v in proof.items() if k not in ('inputPins','variants')}))
if __name__=='__main__':main()
