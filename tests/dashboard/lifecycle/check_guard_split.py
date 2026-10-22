"""Portable actual-module regression of bundle CONTROL amplification; no console I/O."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def pin(p):
 b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--root',type=Path,required=True);a.add_argument('--candidate',type=Path);a.add_argument('--output',type=Path,required=True);v=a.parse_args()
 root=v.root.resolve();candidate=(v.candidate or root).resolve();out=v.output.resolve();out.mkdir(parents=True,exist_ok=False)
 lib=candidate/'src/blackwire/library';original=root/'src/blackwire/library';stream=root/'src/blackwire/streamreader';tests=root/'tests/dashboard/lifecycle'
 source={p:pin(p) for p in [lib/'preload_bundle.c',lib/'preload_bundle.h',lib/'native_backend.c',original/'native_setup.c',original/'startup_runtime_idle.c',stream/'share_config.c',stream/'diagnostics/game_stop_identity.c',tests/'test_ready_control_idle.c',HERE/'test_bundle_control_split.inc',Path(__file__)]}
 unit=out/'unit';(unit/'library').mkdir(parents=True);(unit/'streamreader').mkdir()
 for n in ['copy_fs.h','copy_sha256.h']:(unit/'streamreader'/n).write_bytes((stream/n).read_bytes())
 for n in ['preload_bundle.h','native_backend.c']:(unit/'library'/n).write_bytes((lib/n).read_bytes())
 names=['libSceAmpr.sprx','libSceAppContent.sprx','libSceGameUpdate.sprx','libSceNpEntitlementAccess.sprx','libSceAmpr.sprx.orig']
 bodies=[bytes((i*13+11)%251 for i in range(n)) for n in [423350,32982,9126,43286,423350]];bodies[4]=bodies[0]
 previous=bytes((i*17+3)%251 for i in range(3072));public07=bytes((i*19+5)%251 for i in range(4608));current=bytes((i*31+7)%251 for i in range(770304))
 production=(lib/'preload_bundle.c').read_text();table='static const struct pb_pin pb_globals[5]={\n'+',\n'.join('    {"%s",%d,"%s"}'%(n,len(b),hashlib.sha256(b).hexdigest()) for n,b in zip(names,bodies))+'\n};'
 tested,count=re.subn(r'static const struct pb_pin pb_globals\[5\]=\{.*?\n\};',lambda _:table,production,count=1,flags=re.S);assert count==1
 tested,count=re.subn(r'static const struct pb_pin pu_previous=\{.*?\};',lambda _:'static const struct pb_pin pu_previous={"libSceAmpr.sprx",3072,"'+hashlib.sha256(previous).hexdigest()+'"};',tested,count=1,flags=re.S);assert count==1
 tested,count=re.subn(r'static const struct pb_pin pu_public07=\{.*?\};',lambda _:'static const struct pb_pin pu_public07={"libSceAmpr.sprx",4608,"'+hashlib.sha256(public07).hexdigest()+'"};',tested,count=1,flags=re.S);assert count==1
 (unit/'library/under-test.c').write_text(tested)
 (unit/'library/preload_bundle.c').write_text(tested)
 fixture=(tests/'test_ready_control_idle.c').read_text().replace('int main(void){','int retained_control_main(void){',1)+'\n'+(HERE/'test_bundle_control_split.inc').read_text()
 fixture=fixture.replace('static int call(void){return play_registration_control(&ctx,BW_NRL_REGISTER);}','static int call(void){return play_registration_control(&ctx,BW_NRL_REGISTER);}')
 (unit/'library/fixture.c').write_text(fixture)
 array=lambda b:','.join(map(str,b))
 image=out/'image.c';image.write_text('#include "preload_bundle.h"\nstatic const uint8_t data[]={'+array(current)+'};\nconst uint8_t fixture_previous[]={'+array(previous)+'};\nconst size_t fixture_previous_size=sizeof fixture_previous;\nconst uint8_t fixture_public07[]={'+array(public07)+'};\nconst size_t fixture_public07_size=sizeof fixture_public07;\nconst struct bw_preload_image bw_preload_embedded_image={data,sizeof data,{'+array(hashlib.sha256(current).digest())+'},"'+'6'*64+'",10000};\n')
 includes=[unit/'library',original,stream,stream/'diagnostics',root/'vendor/libsmb2/include',root/'vendor/sqlite',root/'src/blackwire/tools/compressed_stream_candidate']
 flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections','-DBW_COPY_HOST_TEST=1','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1']+[str(x) for p in includes for x in ['-I',p]]
 san=['-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'];rows=[]
 def run(label,cmd):
  cmd=list(map(str,cmd));r=subprocess.run(cmd,cwd=root,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
  rows.append(dict(label=label,argv=cmd,exitCode=r.returncode));(out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n');(out/(label+'.log')).write_text(r.stdout+r.stderr)
  assert not r.returncode,(label,r.stdout[-2000:],r.stderr[-3000:]);return r.stdout
 objects=[]
 for i,p in enumerate([original/'native_setup.c',original/'startup_runtime_idle.c',stream/'diagnostics/game_stop_identity.c',stream/'share_config.c',image]):
  obj=out/(str(i)+'.o');extra=['-Dbw_native_setup_read=rc_actual_setup_read'] if i==0 else []
  run('compile-'+str(i),['clang-18',*flags,*san,*extra,'-c',p,'-o',obj]);objects.append(obj)
 with tempfile.TemporaryDirectory(prefix='blackwire-control-split-') as tmp:
  temp=Path(tmp);globals_dir=temp/'globals';globals_dir.mkdir();cases=temp/'cases';cases.mkdir()
  for n,b in zip(names,bodies):(globals_dir/n).write_bytes(b)
  binary=out/'actual-guard-split';wraps=['open','dup','close','fclose','fopen','fgets','clock_gettime']
  run('compile-link-fixture',['clang-18',*flags,*san,'-DBW_PRELOAD_GLOBAL_ROOT="'+str(globals_dir)+'"',unit/'library/fixture.c',*objects,*['-Wl,--wrap='+x for x in wraps],'-Wl,--gc-sections','-lm','-o',binary])
  lines=run('actual-module-cases',[binary,cases]).splitlines()
  retained={}
  for label,name,more in [('retained-upgrade','test_preload_bundle_upgrade.c',['closedir','renameat','clock_gettime']),('retained-bundle','test_preload_bundle.c',[])]:
   source[tests/name]=pin(tests/name);test_binary=out/label;area=temp/label;area.mkdir()
   retained_flags=[x for x in flags if x!='-DBW_COPY_HOST_TEST=1']
   run(label+'-build',['clang-18',*retained_flags,*san,'-DBW_PRELOAD_GLOBAL_ROOT="'+str(globals_dir)+'"',tests/name,objects[-1],*['-Wl,--wrap='+x for x in ['close','write','fsync',*more]],'-Wl,--gc-sections','-o',test_binary])
   retained[label]=json.loads(run(label+'-run',[test_binary,area]).splitlines()[-1]);assert retained[label]['passed']
 result=json.loads(lines[-1]);assert result['passed'] and result['cases']>=12
 assert all(pin(p)==wanted for p,wanted in source.items())
 proof={'passed':True,'format':'blackwire-actual-bundle-control-split-host/1','atUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'result':result,'cases':[json.loads(x) for x in lines[:-1]],'retained':retained,'sanitizers':'ASan+UBSan','source':{str(p):v for p,v in source.items()},'sourceBeforeAfterMatch':True,'syntheticFixedLibraryHashesOnly':True,'globalAndReaderLengthsMatchProduction':True,'commands':rows,'consoleContacted':False,'productionFullBuild':False}
 (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'passed':True,'result':result,'cases':proof['cases'],'proof':pin(out/'proof.json')}))
if __name__=='__main__':main()
