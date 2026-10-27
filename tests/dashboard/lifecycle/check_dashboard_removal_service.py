"""Actual service/model/FS/readonly SQLite action integration; no console."""
from pathlib import Path
import argparse,hashlib,json,os,sqlite3,struct,subprocess,sys,tempfile,zlib
HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('name',nargs='?',default='checks-01')
parser.add_argument('--output',type=Path)
parser.add_argument('--host-only',action='store_true')
ARGS=parser.parse_args();NAME=ARGS.name
if not NAME.startswith('checks-') or not NAME[7:].isdigit():raise ValueError('fresh checks-NN required')
OUT=(ARGS.output if ARGS.output else ROOT/'build/native-dashboard/dashboard-removal-service-01'/NAME).resolve()
if not OUT.is_relative_to(ROOT/'build') or OUT==ROOT/'build':raise ValueError('Use a fresh repository build child')
LIB=ROOT/'src/blackwire/library';SVC=ROOT/'src/blackwire/dashboard/service';STREAM=ROOT/'src/blackwire/streamreader';MODEL=ROOT/'src/blackwire/dashboard/model'
def pin(p):
 b=p.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 OUT.mkdir(parents=True,exist_ok=False);records=[]
 sources=[SVC/n for n in ('dashboard_service.c','catalog_adapter.c','catalog_snapshot.c','frontend_identity.c','local_http.c')]+[MODEL/'dashboard_model.c',LIB/'source_param.c',LIB/'registration_db.c',LIB/'native_request.c',LIB/'native_removal_request.c',STREAM/'manifest.c',STREAM/'diagnostics/game_stop_identity.c']
 tests=[HERE/n for n in ('test_dashboard_removal_service.c','test_launch_feedback_service.c')]
 inputs=sources+tests+[SVC/'dashboard_service.h',SVC/'catalog_snapshot.h',MODEL/'dashboard_model.h',LIB/'native_game_uninstall.h',LIB/'native_removal_request.h',Path(__file__)]
 before={str(p.relative_to(ROOT)):pin(p) for p in inputs}
 def run(label,args):
  args=list(map(str,args));r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=120,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
  records.append(dict(label=label,argv=args,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(OUT/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
  if r.returncode:raise RuntimeError(label+' failed; outputs retained')
  return r.stdout
 raw=(SVC/'frontend_identity.c').read_text(encoding='utf-8')
 def extract(sig):
  at=raw.index(sig);brace=raw.index('{',at);depth=1;end=brace+1
  while depth:
   if raw[end]=='{':depth+=1
   elif raw[end]=='}':depth-=1
   end+=1
  return raw[at:end]+'\n'
 fingerprint=OUT/'fingerprint.c';fingerprint.write_text('#include "frontend_identity.h"\n#include "copy_sha256.h"\n'+extract('static void hash_u64(')+extract('static void package_fingerprint(').replace('static void package_fingerprint(','void fixture_package_fingerprint('),encoding='utf-8')
 sqlite=OUT/'sqlite.o';run('sqlite-source',['clang-18','-std=gnu11','-O0','-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_TEMP_STORE=3','-c',ROOT/'vendor/sqlite/sqlite3.c','-o',sqlite])
 flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-DBD_FRONTEND_IDENTITY_HOST_TEST=1']
 includes=['-I',LIB,'-I',SVC,'-I',STREAM,'-I',ROOT/'vendor/sqlite','-I',ROOT/'vendor/libsmb2/include']
 binary=OUT/'service';run('actual-service-build',['clang-18',*flags,*includes,*sources,tests[0],fingerprint,sqlite,'-Wl,--gc-sections','-pthread','-lm','-o',binary])
 cases=['startup-browser', 'startup-native', 'startup-unknown', 'startup-confirmation', 'pc-browser', 'pc-native', 'send-only', 'restore-browser', 'restore-native', 'local-browser', 'local-native', 'local-unknown', 'pc-unknown', 'wrong-status', 'confirmation', 'busy', 'refused', 'lost-reply', 'lost-reply-held', 'source-absent', 'status-unavailable', 'pc-reconcile-done', 'pc-reconcile-held', 'pc-reconcile-foreign', 'pc-reconcile-busy', 'pc-reconcile-throttle', 'pc-reconcile-error', 'pc-reconcile-done-release-failure', 'pc-reconcile-wrong-source', 'pc-reconcile-wrong-title'];observations=[]
 for case in cases:
  with tempfile.TemporaryDirectory(prefix='blackwire-removal-service-') as tmp:
   base=Path(tmp);source=base/'full-local';apps=base/'apps';app=apps/'PPSA99911'
   for directory in (source/'sce_sys',app/'sce_sys'):directory.mkdir(parents=True)
   param=json.dumps({'titleId':'PPSA99911','contentId':'EP0000-PPSA99911_00-BLACKWIRETEST0001','titleName':'Local fixture','applicationDrmType':'standard'}).encode()
   (source/'sce_sys/param.json').write_bytes(param);(app/'sce_sys/param.json').write_bytes(param);(source/'eboot.bin').write_bytes(b'fixture-not-a-console-game'*3);(source/'eboot.bin').chmod(0o755);(app/'mount.lnk').write_text(str(source),encoding='utf-8')
   def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
   icon=app/'sce_sys/icon0.png';icon.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\0\0\0\xff'))+chunk(b'IEND',b''))
   db=base/'app.db'
   with sqlite3.connect(db) as c:
    c.execute('CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)');c.execute('INSERT INTO tbl_contentinfo VALUES(?,?)',('PPSA99911','EP0000-PPSA99911_00-BLACKWIRETEST0001'))
   smp=base/'games.json';smp.write_text(json.dumps({'status':0,'count':1,'games':[dict(path=str(source),runtime_path=str(source),source_type='folder',title_id='PPSA99911',title_name='Local fixture',content_id='EP0000-PPSA99911_00-BLACKWIRETEST0001',installed=True,installed_pkg=False,managed=True,source_available=True,image_backed=False)]}),encoding='utf-8')
   observations.append(json.loads(run(case,[binary,case,smp,apps,db,icon])))
 objects=[]
 if not ARGS.host_only:
  historical=ROOT/'build/native-dashboard/sdk-template-placeholder';rows=production_rows(ROOT,OUT/'sdk-template');sdk=OUT/'sdk';(sdk/'objects').mkdir(parents=True);(sdk/'deps').mkdir();objects=[]
  for source in (SVC/'dashboard_service.c',SVC/'catalog_snapshot.c'):
   row=next(r for r in rows if '-c' in r['argv'] and Path(r['argv'][r['argv'].index('-c')+1])==source)
   args=[a.replace(str(historical),str(sdk)) for a in row['argv']];obj=sdk/'objects'/(source.stem+'.o');args[args.index('-o')+1]=str(obj);run('sdk-'+source.stem,args);objects.append(obj)
 assert before=={str(p.relative_to(ROOT)):pin(p) for p in inputs}
 proof=dict(passed=True,cases=len(observations),checks=sum(x['checks'] for x in observations),observations=observations,source=before,sourceBeforeAfterMatch=True,sdkObjects={p.name:pin(p) for p in objects},commands=records,actualServiceModelParserFilesystemAndReadonlySQLite=True,platformBackendAndSonyEffectsModeled=True,consoleActions=0,deployed=False)
 (OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(passed=True,cases=proof['cases'],checks=proof['checks'],proof=pin(OUT/'proof.json'))))
if __name__=='__main__':main()
