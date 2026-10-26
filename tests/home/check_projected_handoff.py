"""Actual three-file publication/session ownership, without console access."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
from support import locations
R,B,S,O=locations('projected-handoff')
files=[S/'library'/n for n in ['home_protocol.c','home_session.c','native_session.c','preload_handoff.c','preload_launch.c','reader_selection.c','source_identity.c','source_binding.c','source_profile.c','source_param.c','startup_runtime_idle.c']]+[S/'streamreader'/n for n in ['manifest.c','share_config.c','diagnostics/game_stop_identity.c']]+[R/'projected_handoff_test.c']
reader=S/'reader/native-reader.c';predicate=reader.read_text().split('released.private_released=',1)[1].split(';',1)[0]
assert 'reader_preloaded.handoff.released' in predicate and 'reader_preloaded.handoff.descriptors_closed' in predicate
(O/'native-reader-private-release-under-test.h').write_text('static int native_reader_private_release_under_test(struct bw_preload_launch reader_preloaded){return '+predicate+';}\n')
pins=files+[R/'home_test.c',S/'library/preload_handoff.h',S/'library/home_session.h',S/'library/preload_launch.h',S/'library/launch_timing.h',S/'library/reader_projection.h',reader,Path(__file__).resolve()]
def pin_inputs():return {str(x):{'size':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in pins}
before=pin_inputs()
cmd=['clang-18','-D_DEFAULT_SOURCE=1','-DBW_PRELOAD_HANDOFF_HOST_TEST=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(O),'-I'+str(S/'library'),'-I'+str(S/'streamreader'),'-I'+str(S/'streamreader/diagnostics'),'-I'+str(B/'vendor/libsmb2/include'),*map(str,files),'-Wl,--wrap=close','-pthread','-o',str(O/'projected_handoff_test')]
p=subprocess.run(cmd,capture_output=True,text=True);(O/'compile.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
with tempfile.TemporaryDirectory(prefix='blackwire-projected-handoff-') as tmp:
 p=subprocess.run([str(O/'projected_handoff_test'),tmp],capture_output=True,text=True);(O/'test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
after=pin_inputs();assert before==after,'Source changed during handoff qualification'
proof={'passed':True,'host':json.loads(p.stdout),'commands':[cmd],'inputPins':after,'sourceBeforeAfterMatch':True,'actualNativeReaderReleasePredicate':True,'emptyRefusalReuseAndUnknownCloseGuard':True,'consoleContacted':False,'nativeRuntimeProved':False};(O/'proof.json').write_text(json.dumps(proof,indent=2))
