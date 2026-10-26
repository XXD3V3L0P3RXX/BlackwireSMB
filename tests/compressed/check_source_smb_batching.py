#!/usr/bin/env python3
"""Check real folder whole-file/range code over modeled SMB PDUs."""
from pathlib import Path
import argparse,json,os,subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);src=root/'src/blackwire';exe=out/'source-smb-batching'
    sources=['library/source_smb.c','library/source_snapshot.c','library/source_plan.c','library/source_directory.c','streamreader/manifest.c']
    command=['clang-18','-D_DEFAULT_SOURCE','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
             '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
             '-I'+str(src),'-I'+str(root/'vendor/libsmb2/include'),str(root/'tests/compressed/source_smb_batch_test.c'),
             *[str(src/name) for name in sources],'-o',str(exe)]
    result=subprocess.run(command,capture_output=True);(out/'compile.log').write_bytes(result.stdout+result.stderr);result.check_returncode()
    result=subprocess.run([str(exe)],capture_output=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
    (out/'test.log').write_bytes(result.stdout+result.stderr);result.check_returncode();proof=json.loads(result.stdout);proof.update(consoleContacted=False,syntheticOnly=True)
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps(proof))

if __name__=='__main__':main()
