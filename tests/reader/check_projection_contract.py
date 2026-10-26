"""v8 framing and distinct DATA-only container mapping; no console or network."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/compressed'))
sys.path.insert(0,str(ROOT/'tests/dashboard/lifecycle'))
from fixtures import make_pfs,pfsc
from fixtures_exfat import make_exfat
from manifest_fixture import File,encode,LOCAL,STREAM
def pin(p):
    raw=p.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    src=ROOT/'src/blackwire';sources=[src/'library/reader_selection.c']+[src/'streamreader'/n for n in ('manifest.c','container_source.c','container_manifest.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c')]+[Path(__file__).with_name('projection_contract_test.c')]
    owned=sources+[src/'library/reader_selection.h',src/'streamreader/container_manifest.h',Path(__file__)]
    before={str(p.relative_to(ROOT)):pin(p) for p in owned};commands=[]
    argv=['clang-18','-D_DEFAULT_SOURCE','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(src),*[str(p)for p in sources],'-o',str(out/'test')]
    def run(label,argv):
        r=subprocess.run(argv,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        (out/(label+'.log')).write_text(r.stdout+r.stderr);commands.append({'label':label,'argv':list(map(str,argv)),'exitCode':r.returncode});(out/'commands.json').write_text(json.dumps(commands,indent=2))
        if r.returncode:raise RuntimeError(label+' failed; inspect private build log')
        return r.stdout
    run('compile',argv);reports=[]
    contents={'eboot.bin':b'FAKE-BOOT-BYTES','packs/data.bin':bytes(range(256))*32,'fakelib/libtest.sprx':b'NOT-EXECUTED-SYNTHETIC'}
    rows=[File('blackwire.companions',777,0,LOCAL),File('eboot.bin',len(contents['eboot.bin']),0,LOCAL),File('fakelib2/libtest.sprx',len(contents['fakelib/libtest.sprx']),0,LOCAL),File('packs/data.bin',8192,0,STREAM)]
    with tempfile.TemporaryDirectory(prefix='blackwire-projection-data-')as work:
        d=Path(work);good=d/'projected.map';good.write_bytes(encode(rows));bad=d/'wrong-stream.map';bad.write_bytes(encode(rows[:-1]+[File('packs/data.bin',8193,0,STREAM)]))
        none=d/'local-only.map';none.write_bytes(encode(rows[:-1]));pfs,_=make_pfs(contents);exfat,_=make_exfat(contents)
        for label,raw in [('pfs',pfs),('pfsc',pfsc(pfs)),('exfat',exfat),('pfsc-exfat',pfsc(exfat))]:
            image=d/(label+'.image');image.write_bytes(raw)
            reports.append(json.loads(run(label,[out/'test',image,good,bad,none])))
    after={str(p.relative_to(ROOT)):pin(p)for p in owned};assert before==after,'Inputs changed during qualification'
    proof={'passed':True,'results':reports,'source':after,'sourceBeforeAfterMatch':True,'selectionParsingGrantsNoAuthority':True,'consumerIntegration':False,'sanitizers':['address','undefined'],'consoleActions':0}
    (out/'proof.json').write_text(json.dumps(proof,indent=2));print(json.dumps({'passed':True,'formats':len(reports),'cases':sum(r['cases']for r in reports),'checks':sum(r['checks']for r in reports)}))
if __name__=='__main__':main()
