"""Pure envelope seal/identity tests; synthetic parent records, no console."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def pin(p):
    raw=p.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=Path,required=True);args=a.parse_args();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
    lib=ROOT/'src/blackwire/library';stream=ROOT/'src/blackwire/streamreader'
    sources=[lib/n for n in ('reader_projection.c','reader_selection.c','source_companions.c','source_plan.c')]+[stream/'manifest.c',Path(__file__).with_name('reader_projection_test.c')]
    owned=sources+[Path(__file__),lib/'reader_projection.h',lib/'reader_selection.h',lib/'source_companions.h',ROOT/'tests/dashboard/lifecycle/test_source_companions.c']
    before={str(p.relative_to(ROOT)):pin(p)for p in owned};cmds=[]
    for label,argv in [('compile',['clang-18','-std=gnu11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',lib,'-I',stream,*sources,'-Wl,--wrap=malloc','-Wl,--wrap=calloc','-o',out/'test']),('run',[out/'test'])]:
        argv=list(map(str,argv));r=subprocess.run(argv,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        (out/(label+'.log')).write_text(r.stdout+r.stderr);cmds.append({'label':label,'argv':argv,'exitCode':r.returncode});(out/'commands.json').write_text(json.dumps(cmds,indent=2))
        if r.returncode:raise RuntimeError(label+' failed; inspect log')
    after={str(p.relative_to(ROOT)):pin(p)for p in owned};assert before==after,'Sources changed'
    proof={'passed':True,'result':json.loads(r.stdout),'source':after,'sourceBeforeAfterMatch':True,'consoleActions':0,'moduleExecution':False,'nativeSendProducer':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2));print(json.dumps(proof['result']))
if __name__=='__main__':main()
