#!/usr/bin/env python3
"""Actual private settings persistence and failure diagnostics, using disposable files."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve()
 assert out.is_relative_to(root/'build') and not out.exists();out.mkdir(parents=True)
 lib=root/'src/blackwire/library';stream=root/'src/blackwire/streamreader'
 with tempfile.TemporaryDirectory(prefix='blackwire-settings-storage-') as tmp:
  scratch=Path(tmp);log=scratch/'diagnostic.log'
  argv=['clang-18','-std=gnu11','-D_DEFAULT_SOURCE','-DBW_NATIVE_SETUP_HOST_TEST=1','-DBW_SEND_LOG_PATH="'+str(log)+'"','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',
   '-I'+str(lib),str(Path(__file__).with_name('test_settings_storage.c')),str(lib/'native_setup.c'),str(lib/'native_request.c'),str(stream/'share_config.c'),str(stream/'manifest.c'),'-o',str(out/'test')]
  r=subprocess.run(argv,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  r=subprocess.run([out/'test',scratch],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  (out/'synthetic-diagnostic.log').write_bytes(log.read_bytes());result=json.loads(r.stdout)
 (out/'proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
