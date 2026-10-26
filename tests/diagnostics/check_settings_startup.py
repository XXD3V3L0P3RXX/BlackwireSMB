#!/usr/bin/env python3
"""Compile actual startup refresh/settings functions with a held scan worker."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/build'))
from reader_patch import function
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args();out=a.output.resolve()
 assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 lib=ROOT/'src/blackwire/library';source=(lib/'native_backend.c').read_text();start=source.index('struct bw_native_backend {');end=source.index('\n};',start)+3
 enums='\n'.join(line for line in source.splitlines() if line.startswith('enum {'))
 header='#include "native_backend.h"\n#include <pthread.h>\n#include <string.h>\n#include <assert.h>\n'+source[start:end]+'\n'+enums+'\n'
 header+='static void *scan(void *p){return p;}\nstatic int worker_start(pthread_t *,void *(*)(void *),void *);\n'
 for name in ['static void settings_adopt(','static void settings_sync(','static int refresh_unlocked(']:header+=function(source,name)+'\n'
 (out/'settings-startup-under-test.h').write_text(header)
 argv=['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(out),'-I'+str(lib),'-I'+str(ROOT/'vendor/libsmb2/include'),str(Path(__file__).with_name('test_settings_startup.c')),str(ROOT/'src/blackwire/streamreader/share_config.c'),'-pthread','-o',str(out/'test')]
 r=subprocess.run(argv,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([out/'test'],capture_output=True,text=True);(out/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 result=json.loads(r.stdout);(out/'proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
