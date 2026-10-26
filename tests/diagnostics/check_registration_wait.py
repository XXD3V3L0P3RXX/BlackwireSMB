"""Offline actual registration-job timing, cancellation and mutation-budget checks."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
def pin(p):
    raw=p.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',required=True,type=Path);args=ap.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    files=[root/'src/blackwire/library/register_job.c',root/'src/blackwire/library/register_job.h',Path(__file__).with_name('registration_wait_test.c')]
    before={p.relative_to(root).as_posix():pin(p) for p in files};exe=out/'registration-wait'
    argv=['clang-18','-D_DEFAULT_SOURCE','-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src/blackwire/library'),str(files[0]),str(files[2]),'-o',str(exe)]
    compile_result=subprocess.run(argv,capture_output=True,text=True);(out/'compile.log').write_text(compile_result.stdout+compile_result.stderr,encoding='utf-8');compile_result.check_returncode()
    result=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    (out/'test.log').write_text(result.stdout+result.stderr,encoding='utf-8');result.check_returncode();report=json.loads(result.stdout)
    assert before=={p.relative_to(root).as_posix():pin(p) for p in files}
    report.update(sourceBeforeAfterMatch=True,source=before,sanitized=True)
    (out/'proof.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='source'}))
if __name__=='__main__':main()
