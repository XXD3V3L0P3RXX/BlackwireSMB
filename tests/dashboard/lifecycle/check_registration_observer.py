#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,os,shlex,subprocess,sys
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3];SVC=ROOT/'src/blackwire/dashboard/service';LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
OUT=ROOT/'build/native-dashboard/registration-observer-01'/('checks-'+(sys.argv[1] if len(sys.argv)>1 else '01'))
def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 OUT.mkdir(parents=True,exist_ok=False);commands=[]
 def run(label,argv,cwd=ROOT):
  p=subprocess.run(list(map(str,argv)),cwd=cwd,text=True,capture_output=True,timeout=120,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=label,argv=list(map(str,argv)),exitCode=p.returncode,stdout=p.stdout,stderr=p.stderr));(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if p.returncode:raise RuntimeError(label+' failed')
  return p.stdout
 sources=[SVC/'registration_observer.c',SVC/'registration_observer.h',SVC/'resident.c',HERE/'test_registration_observer.c',Path(__file__),LIB/'native_backend.h',LIB/'source_param.c',STREAM/'manifest.c']
 before={str(p.relative_to(ROOT)):pin(p) for p in sources};binary=OUT/'observer-host'
 inc=[v for p in (SVC,SVC.parent/'model',LIB,STREAM,ROOT/'vendor/libsmb2/include') for v in ('-I',p)]
 run('actual-observer-host',['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections',*inc,HERE/'test_registration_observer.c',LIB/'source_param.c',STREAM/'manifest.c','-Wl,--gc-sections','-pthread','-o',binary])
 observations=json.loads(run('observer-fault-cases',[binary]).strip().splitlines()[-1]);rows=production_rows(ROOT,OUT/'sdk-template');template=next(r['argv'] for r in rows if '-c' in r['argv'] and Path(r['argv'][r['argv'].index('-c')+1]).name=='frontend_identity.c')
 (OUT/'sdk/objects').mkdir(parents=True);(OUT/'sdk/deps').mkdir();objects=[]
 for source in (SVC/'registration_observer.c',SVC/'resident.c'):
  argv=[v.replace(str(ROOT/'build/native-dashboard/sdk-template-placeholder'),str(OUT/'sdk')) for v in template];argv[argv.index('-c')+1]=str(source);obj=OUT/'sdk/objects'/source.with_suffix('.o').name;argv[argv.index('-o')+1]=str(obj);argv[argv.index('-MF')+1]=str(OUT/'sdk/deps'/source.with_suffix('.d').name);argv+=list(map(str,inc+['-I',SVC.parent/'local-removal']));run('sdk-'+source.stem,argv,LIB);objects.append(obj)
 linked=OUT/'sdk/observer-resident.o';run('sdk-link',['/opt/ps5-payload-sdk/bin/ld.lld','-r',*objects,'-o',linked])
 symbols=run('sdk-observer-imports',['/opt/ps5-payload-sdk/bin/llvm-nm','--undefined-only',objects[0]])
 forbidden=('bw_sm_uninstall','bw_sm_post','bw_native_game_uninstall','sceAppInstUtilAppUnInstall','unlink','rename','kill','raise','ptrace')
 if any(word in symbols for word in forbidden):raise RuntimeError('observer imported a forbidden mutation API')
 dependencies={}
 for dep in (OUT/'sdk/deps').glob('*.d'):
  for value in shlex.split(dep.read_text().replace('\\\n',' ').split(': ',1)[1]):
   path=Path(value) if Path(value).is_absolute() else LIB/value
   if path.is_file():dependencies[str(path.resolve())]=pin(path.resolve())
 if before!={str(p.relative_to(ROOT)):pin(p) for p in sources}:raise RuntimeError('source changed during tests')
 proof=dict(passed=True,sources=before,observations=observations,sdkDependencies=dependencies,sdkObjects={str(p.relative_to(ROOT)):pin(p) for p in objects},sdkRelocatable=pin(linked),observerImports=symbols,commands=commands,consoleActions=0,fullAppBuilds=0)
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,observations=observations,proof=pin(OUT/'proof.json'))))
if __name__=='__main__':main()
