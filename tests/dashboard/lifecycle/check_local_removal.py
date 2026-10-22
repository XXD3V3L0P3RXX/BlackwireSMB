#!/usr/bin/env python3
"""Qualified local removal component only; no console/network/full app build."""
from pathlib import Path
import hashlib,json,os,shlex,sqlite3,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/native-dashboard/local-removal-01'/('checks-'+(sys.argv[1] if len(sys.argv)>1 else '01'))
LOCAL=ROOT/'src/blackwire/dashboard/local-removal';SVC=ROOT/'src/blackwire/dashboard/service';MODEL=ROOT/'src/blackwire/dashboard/model';LIB=ROOT/'src/blackwire/library';STREAM=ROOT/'src/blackwire/streamreader'
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 OUT.mkdir(parents=True,exist_ok=False);commands=[]
 def run(name,args,cwd=ROOT):
  r=subprocess.run(list(map(str,args)),cwd=cwd,text=True,capture_output=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  commands.append(dict(label=name,argv=list(map(str,args)),exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if r.returncode:raise RuntimeError(name+' failed; retained commands.json')
  return r.stdout
 sources=[LOCAL/'local_removal.c',LOCAL/'local_removal.h',HERE/'test_local_removal.c',Path(__file__),SVC/'catalog_adapter.c',SVC/'catalog_adapter.h',MODEL/'dashboard_model.c',MODEL/'dashboard_model.h',LIB/'source_param.c',LIB/'registration_db.c',STREAM/'manifest.c',STREAM/'copy_fs.h',LIB/'shadowmount_http.c',LIB/'shadowmount_reply.c',LIB/'shadowmount_client.h',LIB/'native_game_uninstall.h',ROOT/'vendor/sqlite/sqlite3.c',ROOT/'vendor/sqlite/sqlite3.h']
 before={str(p.relative_to(ROOT)):pin(p) for p in sources}
 flags=['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections']
 inc=['-I',LOCAL,'-I',SVC,'-I',MODEL,'-I',LIB,'-I',STREAM,'-I',ROOT/'vendor/sqlite','-I',ROOT/'vendor/libsmb2/include']
 sqlite=OUT/'sqlite.o';run('sqlite-source',['clang-18','-std=gnu11','-O0','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite])
 binary=OUT/'local-removal-host';core=[SVC/'catalog_adapter.c',MODEL/'dashboard_model.c',LIB/'source_param.c',LIB/'registration_db.c',STREAM/'manifest.c']
 run('actual-component-host',flags+inc+core+[HERE/'test_local_removal.c',sqlite,'-Wl,--gc-sections','-pthread','-lm','-o',binary])
 scenarios=['normal','pkg','pkg-unknown-removed','pkg-unknown-present','pkg-substitution','default-normal','default-unknown','default-refused','unknown-removed','unknown-present','refused','pkg-refused','manual-unknown','confirmation','wrong-title','wrong-id','wrong-generation','wrong-kind','wrong-fingerprint','busy','alternative','symlink','hardlink','special','scan-root','inventory-change','extra-entry','new-original','db-conflict','guard-after','intent-change','inventory-replaced','registered-change','db-changed-before','foreign-ledger','nonce-collision','existing-ledger','pending-history','completed-history','malformed-history']
 observations=[]
 for scenario in scenarios:
  with tempfile.TemporaryDirectory(prefix='blackwire-local-removal-') as folder:
   area=Path(folder);(area/'data').mkdir();(area/'apps').mkdir();(area/'Games').mkdir();(area/'saved-data').mkdir();(area/'saved-data/save.dat').write_bytes(b'original-save')
   other=area/'Games/Unrelated game';other.mkdir();(other/'keep.bin').write_bytes(b'unrelated-game')
   app=area/'apps/PPSA99981';(app/'sce_sys').mkdir(parents=True)
   param=b'{"titleId":"PPSA99981","contentId":"UP0000-PPSA99981_00-LOCALFIXTURE0000"}'
   (app/'sce_sys/param.json').write_bytes(param)
   if scenario.startswith('pkg'):(app/'app.pkg').write_bytes(b'package-fixture'*8)
   else:
    source=area/'Games/Selected game';(source/'sce_sys').mkdir(parents=True);(source/'assets').mkdir()
    (source/'sce_sys/param.json').write_bytes(param);(source/'eboot.bin').write_bytes(bytes(range(64)));(source/'eboot.bin').chmod(0o755)
    (source/'assets/level.bin').write_bytes(b'game-content'*10);(app/'mount.lnk').write_text(str(source))
   db=sqlite3.connect(area/'app.db');db.execute('CREATE TABLE tbl_contentinfo(titleId TEXT, contentId TEXT)');db.execute("INSERT INTO tbl_contentinfo VALUES('PPSA99981','UP0000-PPSA99981_00-LOCALFIXTURE0000')");db.commit();db.close()
   observations.append(json.loads(run(scenario,[binary,area,scenario]).strip().splitlines()[-1]))
 historical=ROOT/'build/native-dashboard/sdk-template-placeholder';rows=production_rows(ROOT,OUT/'sdk-template');sdk=OUT/'sdk';(sdk/'objects').mkdir(parents=True);(sdk/'deps').mkdir()
 template=next(row['argv'] for row in rows if '-c' in row['argv'] and Path(row['argv'][row['argv'].index('-c')+1]).name=='frontend_identity.c')
 sdk_sources=[LOCAL/'local_removal.c']+core+[LIB/'shadowmount_http.c',LIB/'shadowmount_reply.c'];objects=[]
 for source in sdk_sources:
  argv=[v.replace(str(historical),str(sdk)) for v in template];argv[argv.index('-c')+1]=str(source);obj=sdk/'objects'/source.with_suffix('.o').name;argv[argv.index('-o')+1]=str(obj)
  argv[argv.index('-MF')+1]=str(sdk/'deps'/source.with_suffix('.d').name);argv+=['-I',str(LOCAL),'-I',str(SVC),'-I',str(MODEL)]
  run('sdk-'+source.stem,argv,LIB);objects.append(obj)
 linked=sdk/'local-removal.o';run('sdk-relocatable-link',['/opt/ps5-payload-sdk/bin/ld.lld','-r',*objects,'-o',linked])
 dependencies={}
 for dep in (sdk/'deps').glob('*.d'):
  for value in shlex.split(dep.read_text().replace('\\\n',' ').split(': ',1)[1]):
   path=Path(value) if Path(value).is_absolute() else LIB/value
   if path.is_file():dependencies[str(path.resolve())]=pin(path.resolve())
 proof={'passed':True,'source':before,'observations':observations,'cases':len(observations),'checks':sum(o['checks'] for o in observations),'actualFilesystemAndReadonlySqlite':True,'normalManagerAndSmpTransportModeled':True,'sdkDependencies':dependencies,'sdkObjects':{str(p.relative_to(ROOT)):pin(p) for p in objects},'sdkRelocatable':pin(linked),'consoleActions':0,'fullAppBuilds':0,'commands':commands}
 # Source map may be extended by root between runs. A successful final scope
 # must have a stable shared model header for this exact compiled checkpoint.
 if before!={str(p.relative_to(ROOT)):pin(p) for p in sources}:raise RuntimeError('source changed during focused test')
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'cases':proof['cases'],'checks':proof['checks'],'proof':pin(OUT/'proof.json')}))
if __name__=='__main__':main()
