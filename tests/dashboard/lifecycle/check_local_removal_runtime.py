#!/usr/bin/env python3
"""Focused actual asynchronous local removal + native SDK checks; no console."""
from pathlib import Path
import hashlib,json,os,shlex,sqlite3,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/native-dashboard/local-removal-runtime-01'/('checks-'+(sys.argv[1] if len(sys.argv)>1 else '01'))
LOCAL=ROOT/'src/blackwire/dashboard/local-removal';SVC=ROOT/'src/blackwire/dashboard/service';MODEL=ROOT/'src/blackwire/dashboard/model';LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 OUT.mkdir(parents=True,exist_ok=False);commands=[]
 def run(label,argv,cwd=ROOT):
  p=subprocess.run(list(map(str,argv)),cwd=cwd,text=True,capture_output=True,timeout=120,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=label,argv=list(map(str,argv)),exitCode=p.returncode,stdout=p.stdout,stderr=p.stderr));(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if p.returncode:raise RuntimeError(label+' failed; see commands.json')
  return p.stdout
 sources=[LOCAL/'local_removal.c',LOCAL/'local_removal.h',LOCAL/'local_removal_runtime.c',LOCAL/'local_removal_runtime.h',HERE/'test_local_removal_runtime.c',HERE/'test_local_removal.c',Path(__file__),SVC/'catalog_adapter.c',SVC/'resident.c',SVC/'dashboard_service.h',MODEL/'dashboard_model.c',MODEL/'dashboard_model.h',LIB/'source_param.c',LIB/'registration_db.c',LIB/'native_backend.h',STREAM/'manifest.c',ROOT/'vendor/sqlite/sqlite3.c',ROOT/'vendor/sqlite/sqlite3.h']
 before={str(p.relative_to(ROOT)):pin(p) for p in sources}
 sqlite=OUT/'sqlite.o';run('sqlite',['clang-18','-O0','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite])
 binary=OUT/'runtime-host';core=[SVC/'catalog_adapter.c',MODEL/'dashboard_model.c',LIB/'source_param.c',LIB/'registration_db.c',STREAM/'manifest.c']
 flags=['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections']
 inc=[v for p in (LOCAL,SVC,MODEL,LIB,STREAM,ROOT/'vendor/sqlite',ROOT/'vendor/libsmb2/include') for v in ('-I',p)]
 run('actual-runtime-host',flags+inc+core+[HERE/'test_local_removal_runtime.c',sqlite,'-pthread','-Wl,--gc-sections','-lm','-o',binary])
 observations=[]
 for case in ('runtime-normal','runtime-unknown','runtime-pkg-unknown','runtime-busy','runtime-scan-refused','runtime-close-retained','runtime-close-error'):
  with tempfile.TemporaryDirectory(prefix='blackwire-local-runtime-') as folder:
   area=Path(folder)
   for leaf in ('data','apps','Games','saved-data'):(area/leaf).mkdir()
   (area/'saved-data/save.dat').write_bytes(b'original-save');app=area/'apps/PPSA99981';(app/'sce_sys').mkdir(parents=True)
   param=b'{"titleId":"PPSA99981","contentId":"UP0000-PPSA99981_00-LOCALFIXTURE0000"}';(app/'sce_sys/param.json').write_bytes(param)
   if case=='runtime-pkg-unknown':(app/'app.pkg').write_bytes(b'package-fixture'*8)
   else:
    source=area/'Games/Selected game';(source/'sce_sys').mkdir(parents=True);(source/'assets').mkdir();(source/'sce_sys/param.json').write_bytes(param);(source/'assets/level.bin').write_bytes(b'game-data');(source/'eboot.bin').write_bytes(bytes(range(64)));(source/'eboot.bin').chmod(0o755);(app/'mount.lnk').write_text(str(source))
   db=sqlite3.connect(area/'app.db');db.execute('CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)');db.execute("INSERT INTO tbl_contentinfo VALUES('PPSA99981','UP0000-PPSA99981_00-LOCALFIXTURE0000')");db.commit();db.close()
   observations.append(json.loads(run(case,[binary,area,case]).strip().splitlines()[-1]))
 historical=ROOT/'build/native-dashboard/sdk-template-placeholder';rows=production_rows(ROOT,OUT/'sdk-template');template=next(r['argv'] for r in rows if '-c' in r['argv'] and Path(r['argv'][r['argv'].index('-c')+1]).name=='frontend_identity.c')
 (OUT/'sdk/objects').mkdir(parents=True);(OUT/'sdk/deps').mkdir();objects=[]
 for source in (LOCAL/'local_removal_runtime.c',LOCAL/'local_removal.c',SVC/'resident.c'):
  argv=[v.replace(str(historical),str(OUT/'sdk')) for v in template];argv[argv.index('-c')+1]=str(source);obj=OUT/'sdk/objects'/source.with_suffix('.o').name;argv[argv.index('-o')+1]=str(obj);argv[argv.index('-MF')+1]=str(OUT/'sdk/deps'/source.with_suffix('.d').name);argv+=list(map(str,inc));run('sdk-'+source.stem,argv,LIB);objects.append(obj)
 linked=OUT/'sdk/local-runtime.o';run('sdk-link',['/opt/ps5-payload-sdk/bin/ld.lld','-r',*objects,'-o',linked])
 if before!={str(p.relative_to(ROOT)):pin(p) for p in sources}:raise RuntimeError('source changed during checks')
 dependencies={}
 for dep in (OUT/'sdk/deps').glob('*.d'):
  for value in shlex.split(dep.read_text().replace('\\\n',' ').split(': ',1)[1]):
   path=Path(value) if Path(value).is_absolute() else LIB/value
   if path.is_file():dependencies[str(path.resolve())]=pin(path.resolve())
 proof=dict(passed=True,sources=before,observations=observations,cases=len(observations),checks=sum(o['checks'] for o in observations),sdkDependencies=dependencies,sdkObjects={str(p.relative_to(ROOT)):pin(p) for p in objects},sdkRelocatable=pin(linked),commands=commands,consoleActions=0,fullAppBuilds=0)
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=proof['cases'],checks=proof['checks'],proof=pin(OUT/'proof.json'))))
if __name__=='__main__':main()
