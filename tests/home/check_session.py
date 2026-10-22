from pathlib import Path
import hashlib,json,subprocess,tempfile
from support import locations
R,B,S,O=locations("session")
files=[S/'library'/n for n in ['home_protocol.c','home_session.c','native_session.c','preload_handoff.c','reader_selection.c','source_binding.c','source_profile.c','source_param.c','startup_runtime_idle.c']]+[S/'streamreader'/n for n in ['manifest.c','share_config.c','diagnostics/game_stop_identity.c']]+[R/'home_test.c']
cmd=['clang-18','-D_DEFAULT_SOURCE=1','-DBW_PRELOAD_HANDOFF_HOST_TEST=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(S/'library'),'-I'+str(S/'streamreader'),'-I'+str(S/'streamreader/diagnostics'),*map(str,files),'-pthread','-o',str(O/'home_test')]
p=subprocess.run(cmd,capture_output=True,text=True);(O/'compile.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
with tempfile.TemporaryDirectory(prefix='blackwire-home-') as tmp:
 p=subprocess.run([str(O/'home_test'),tmp],capture_output=True,text=True);(O/'test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
proof={'passed':True,'host':json.loads(p.stdout),'commands':[cmd],'inputPins':{str(x):{'size':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in files},'consoleContacted':False,'nativeRuntimeProved':False};(O/'proof.json').write_text(json.dumps(proof,indent=2))


