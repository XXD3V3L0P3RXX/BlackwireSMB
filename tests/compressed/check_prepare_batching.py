#!/usr/bin/env python3
"""Exercise actual compressed startup hashing/copy with bounded batched input."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
from fixtures import make_pfs,pfsc

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    src=root/'src/blackwire';exe=out/'compressed-prepare'
    sources=['library/compressed_data.c','library/source_plan.c','library/source_profile.c','library/source_binding.c','library/source_param.c',
             'streamreader/container_source.c','streamreader/container_manifest.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c']
    command=['clang-18','-D_DEFAULT_SOURCE','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
             '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Wl,--wrap=malloc',
             '-I'+str(src),'-I'+str(root/'vendor/libsmb2/include'),str(root/'tests/compressed/prepare_batch_test.c'),
             *[str(src/name) for name in sources],'-o',str(exe)]
    compiled=subprocess.run(command,capture_output=True);(out/'compile.log').write_bytes(compiled.stdout+compiled.stderr);compiled.check_returncode()
    body=(bytes(range(256))*(5*1024*1024//256))+b'end-of-synthetic-startup'
    contents={'eboot.bin':body,'sce_sys/param.json':b'{"titleId":"PPSA99001"}',
              'sce_module/startup.prx':b'module'+bytes(1024*1024+17),'assets/stream.pak':b'asset'+bytes(256*1024)}
    with tempfile.TemporaryDirectory(prefix='blackwire-prep-batch-') as work:
        temp=Path(work);image=temp/'fixture.ffpfsc';original=temp/'original.bin';digest=temp/'original.sha256'
        image.write_bytes(pfsc(make_pfs(contents)[0]));original.write_bytes(body);digest.write_bytes(hashlib.sha256(body).digest())
        result=subprocess.run([str(exe),str(image),str(original),str(digest)],capture_output=True,
            env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
        (out/'test.log').write_bytes(result.stdout+result.stderr);result.check_returncode();proof=json.loads(result.stdout)
    proof.update(consoleContacted=False,syntheticOnly=True)
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps(proof))

if __name__=='__main__':main()
