#!/usr/bin/env python3
"""Reproduce missing-global setup and verify embedded reader installation."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile

def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=Path,required=True)
 a.add_argument('--baseline-c',type=Path);a.add_argument('--host-only',action='store_true');a.add_argument('--sdk',type=Path,default=Path('/opt/ps5-payload-sdk'))
 args=a.parse_args();here=Path(__file__).resolve().parent;root=here.parents[2];lib=root/'src/blackwire/library'
 out=args.output.resolve();assert out.is_relative_to(root/'build') and not out.exists();out.mkdir(parents=True)
 selected=(args.baseline_c or lib/'preload_bundle.c').resolve(strict=True)
 used=[selected,lib/'preload_bundle.h',here/'test_preload_self_contained.c',Path(__file__).resolve()]
 before={str(p):pin(p) for p in used};commands=[]
 def run(label,argv):
  p=subprocess.run(list(map(str,argv)),capture_output=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
  (out/(label+'.log')).write_bytes(p.stdout+p.stderr);commands.append({'label':label,'exitCode':p.returncode})
  if p.returncode:raise RuntimeError(label+': '+p.stderr.decode(errors='replace')[-3000:])
  return p.stdout
 image=bytes(i%251 for i in range(2048));image_c=out/'image.c'
 image_c.write_text('#include "preload_bundle.h"\nstatic const uint8_t bytes[]={'+','.join(map(str,image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={bytes,sizeof bytes,{'+','.join(map(str,hashlib.sha256(image).digest()))+'},"'+'0'*64+'",10000};\n')
 with tempfile.TemporaryDirectory(prefix='blackwire-no-global-') as work:
  fixture=Path(work);assert not (fixture/'missing-global-library-set').exists()
  flags=['-std=c11','-D_DEFAULT_SOURCE','-DBW_PRELOAD_BUNDLE_HOST_TEST=1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-I'+str(lib)]
  define='-DBW_PRELOAD_GLOBAL_ROOT="'+str(fixture/'missing-global-library-set')+'"'
  exe=out/'self-contained-bundle'
  run('host-compile',['clang-18',*flags,define,'-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
   here/'test_preload_self_contained.c',selected,image_c,'-o',exe])
  result=json.loads(run('missing-global-test',[exe,fixture,'baseline' if args.baseline_c else 'current']))
  assert not (fixture/'missing-global-library-set').exists()
  if not args.host_only and not args.baseline_c:
   for name,p in [('bundle',selected),('image',image_c)]:
    run('sdk-'+name,[args.sdk/'bin/prospero-clang',*flags,'-fPIC','-c',p,'-o',out/(name+'.o')])
   run('sdk-link',[args.sdk/'bin/ld.lld','-r',out/'bundle.o',out/'image.o','-o',out/'joined.o'])
  assert before=={str(p):pin(p) for p in used}
  proof={'passed':True,'baseline':bool(args.baseline_c),'result':result,'sourceBeforeAfterMatch':True,'source':before,
   'syntheticImage':True,'globalFolderAbsentBeforeAndAfter':True,'commands':commands,'consoleActions':0,'deployed':False}
  (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k not in ('source','commands')}))

if __name__=='__main__':main()
