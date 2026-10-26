#!/usr/bin/env python3
"""Actual two-share discovery + setup worker diagnostics with mocked SMB, ASan/UBSan."""
from pathlib import Path
import argparse,json,os,subprocess,sys,hashlib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/build'))
from reader_patch import function

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
 assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 lib=ROOT/'src/blackwire/library';stream=ROOT/'src/blackwire/streamreader';source=(lib/'native_backend.c').read_text()
 start=source.index('struct bw_native_backend {');end=source.index('\n};',start)+3
 header='#define _DEFAULT_SOURCE 1\n#include "native_backend.h"\n#include "native_json.h"\n#include "../streamreader/copy_sha256.h"\n#include <errno.h>\n#include <limits.h>\n#include <pthread.h>\n#include <time.h>\n#include <smb2/smb2.h>\n#include <smb2/libsmb2.h>\n'
 header+=source[start:end]+'\n'+'\n'.join(l for l in source.splitlines() if l.startswith('enum {'))+'\n'
 header+='static void bw_send_log(const char *,const char *,...);\nstatic int removal_cache(struct bw_native_backend *,struct bw_native_library *,const struct bw_share_config *);\n'
 header+=function((lib/'native_setup.c').read_text(),'int bw_native_setup_config_valid(')+'\n'
 names=['static double now(','static int valid_config(','static int config_equal(','static void settings_adopt(','static int same_info(','static int source_compare(','static int catalog_order(','static int valid_library(','void bw_native_library_destroy(','static void check_reason(','static void check_failure(','static void check_failure_message(','static int discover_one(','static int library_merge(','static int discover(','static int settings_idle(','static void *scan(','static const char *phase_message(','static void setup_failure_json(','static const char *setup_message(']
 for name in names:header+=function(source,name)+'\n'
 (out/'settings-check-under-test.h').write_text(header)
 flags=['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
 for folder in (out,lib,stream,ROOT/'vendor/libsmb2/include'):flags+=['-I',str(folder)]
 binary=out/'test-settings-check';argv=flags+[str(Path(__file__).with_name('test_settings_check_failure.c')),str(lib/'native_request.c'),str(stream/'share_config.c'),str(stream/'manifest.c'),'-pthread','-Wl,--gc-sections','-o',str(binary)]
 r=subprocess.run(argv,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([binary],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1:detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'});(out/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 diagnostics=[json.loads(l) for l in r.stdout.splitlines() if l];result=json.loads(r.stderr)
 assert all('PRIVATE-FIXTURE-SECRET' not in json.dumps(d) and 'project-fixture' not in json.dumps(d) for d in diagnostics)
 proof={'passed':True,'host':result,'failureDocuments':diagnostics,'actualFunctionsExtracted':names,'sanitizers':['address','undefined'],'consoleContacted':False,'networkConnected':False,'sourceSha256':hashlib.sha256(source.encode()).hexdigest()}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()

