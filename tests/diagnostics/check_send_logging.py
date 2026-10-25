#!/usr/bin/env python3
"""Check the bounded setup logger and actual bundle directory diagnostics."""
from pathlib import Path
import argparse,json,os,subprocess
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 root=Path(__file__).resolve().parents[2];out=a.output.resolve()
 assert out.is_relative_to(root/'build') and not out.exists();out.mkdir(parents=True)
 results=[]
 for name,extra in [('test_send_logger',[]),('test_send_directory',['-Wl,--wrap=lstat','-pthread'])]:
  argv=['clang-18','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'src/blackwire/library'),str(root/'tests/diagnostics'/ (name+'.c')),*extra,'-o',str(out/name)]
  build=subprocess.run(argv,capture_output=True,text=True);(out/(name+'-compile.log')).write_text(build.stdout+build.stderr);assert build.returncode==0
  run=subprocess.run([out/name],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
  (out/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0;results.append({'case':name,'passed':True})
 proof={'passed':True,'cases':results,'errnoPreserved':True,'boundedLog':True,'symlinkAndHardlinkRefused':True,'directoryFailurePathLogged':True,'consoleContacted':False}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
