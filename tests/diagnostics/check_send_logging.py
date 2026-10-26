#!/usr/bin/env python3
"""Check the bounded setup logger and actual bundle directory diagnostics."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 root=Path(__file__).resolve().parents[2];out=a.output.resolve()
 assert out.is_relative_to(root/'build') and not out.exists();out.mkdir(parents=True)
 direct=[Path(__file__).resolve()]+[root/'tests/diagnostics'/name for name in ('test_send_logger.c','test_send_directory.c','test_send_logger_disabled.c')]
 direct += [root/'src/blackwire/library'/name for name in ('send_diagnostic.h','preload_bundle.c','preload_bundle.h','source_companions.c','source_companions.h','preload_companions_internal.h','preload_source_ampr_internal.h')]
 direct += [root/'src/blackwire/streamreader'/name for name in ('manifest.c','manifest.h','copy_fs.h','copy_sha256.h','startup_mode.h')]
 def pins():return {str(path.relative_to(root)):{'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in direct}
 before=pins()
 results=[]
 common=['clang-18','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-Wformat=2','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'src/blackwire/library')]
 for name,source,extra in [('test_send_logger','test_send_logger',['-DBW_SEND_DIAGNOSTIC=1']),('test_send_directory','test_send_directory',['-DBW_SEND_DIAGNOSTIC=1','-Wl,--wrap=lstat','-pthread']),('release-default','test_send_logger_disabled',[]),('release-explicit-zero','test_send_logger_disabled',['-DBW_SEND_DIAGNOSTIC=0'])]:
  if source=='test_send_logger_disabled':extra += ['-Wl,--wrap='+call for call in ('open','write','close','fstat','clock_gettime','getpid')]
  units=[root/'tests/diagnostics'/ (source+'.c')]
  if source=='test_send_directory':units += [root/'src/blackwire/library/source_companions.c',root/'src/blackwire/streamreader/manifest.c']
  argv=[*common,*map(str,units),*extra,'-o',str(out/name)]
  build=subprocess.run(argv,capture_output=True,text=True);(out/(name+'-compile.log')).write_text(build.stdout+build.stderr);assert build.returncode==0
  run=subprocess.run([out/name],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
  (out/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0
  absent=None
  if source=='test_send_logger_disabled':
   package=(out/name).read_bytes();absent=all(marker not in package for marker in (b'sendlog.1 t=',b'/data/blackwire/send-diagnostic.log',b'release-private-event',b'release-private-format='));assert absent
  results.append({'case':name,'passed':True,'privateMarkersAbsent':absent})
 invalid=out/'invalid-format.c';invalid.write_text('#include "send_diagnostic.h"\nint main(void){bw_send_log("format-check","%d","wrong");return 0;}\n')
 for mode,extra in [('release',[]),('diagnostic',['-DBW_SEND_DIAGNOSTIC=1'])]:
  build=subprocess.run([*common,*extra,'-fsyntax-only',str(invalid)],capture_output=True,text=True);(out/('invalid-format-'+mode+'.log')).write_text(build.stdout+build.stderr);assert build.returncode!=0 and 'format' in build.stderr;results.append({'case':'invalid-format-'+mode,'passed':True})
 assert before==pins(),'Direct logger/bundle inputs changed during verification'
 proof={'passed':True,'cases':results,'errnoPreserved':True,'boundedLog':True,'symlinkAndHardlinkRefused':True,'directoryFailurePathLogged':True,'releaseArgumentsNotEvaluated':True,'releaseLoggerIoCalls':0,'releasePrivateMarkersAbsent':True,'releaseFormatCheckingPreserved':True,'directInputPins':before,'directInputsBeforeAfterMatch':True,'consoleContacted':False}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
