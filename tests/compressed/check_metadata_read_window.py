#!/usr/bin/env python3
"""Pure window and actual owned-container cache/failure lifetime boundary."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];S=ROOT/'src/blackwire'
sys.dont_write_bytecode=True;sys.path.insert(0,str(HERE))
from fixtures import make_pfs
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
OUT=parser.parse_args().output.resolve()
if not OUT.is_relative_to(ROOT/'build') or OUT.exists():raise ValueError('Use a fresh output below build/')
OUT.mkdir(parents=True)
names=['streamreader/container_source.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c','streamreader/manifest.c']
tracked=[S/n for n in names]+[HERE/'metadata_read_window_test.c',HERE.parent/'diagnostics/test_catalog_image_failure.c',Path(__file__),HERE/'fixtures.py',S/'streamreader/container_smb.c']
tracked+=list((S/'streamreader').glob('*.h'))+list((S/'library').glob('*.h'))+list((S/'streamreader/vendor/miniz').glob('*.[ch]'))+list((ROOT/'vendor/libsmb2/include').rglob('*.h'))
pin=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before={str(p):pin(p) for p in tracked};commands=[]
def run(args):
 r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=120,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
 commands.append({'argv':list(map(str,args)),'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if r.returncode:raise RuntimeError(r.stderr or r.stdout)
 return r.stdout
binary=OUT/'metadata-read-window-test'
run(['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(S),'-I'+str(ROOT/'vendor/libsmb2/include'),HERE/'metadata_read_window_test.c',*[S/n for n in names],'-Wl,--gc-sections','-Wl,--wrap=malloc','-Wl,--wrap=free','-o',binary])
with tempfile.TemporaryDirectory(prefix='blackwire-metadata-window-') as temp:
 image=Path(temp)/'Neutral.ffpfsc';image.write_bytes(make_pfs({'eboot.bin':b'NEUTRAL INERT STARTUP','sce_sys/param.json':b'{"titleId":"PPSA09999","titleName":"Neutral"}','asset.bin':bytes((i*29+i//251)%256 for i in range(128*1024))})[0]);original=pin(image)
 result=json.loads(run([binary,image]));assert pin(image)==original
after={str(p):pin(p) for p in tracked};assert before==after,'Source changed during cache verification'
proof={**result,'sourceBeforeAfterMatch':True,'source':before,'originalSyntheticSourceUnchanged':True,'sanitizers':['address','undefined'],'sourceWrites':0}
(OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({**result,'proofSha256':pin(OUT/'proof.json')}))
