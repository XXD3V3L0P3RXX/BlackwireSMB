"""Actual readiness/bundle counters; private host filesystem only."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
from support import locations
R,B,S,O=locations('ready-observe')
fixture=Path(tempfile.mkdtemp(prefix='blackwire-readiness-'))
image=bytes((i*19+1)%251 for i in range(2048));sha=hashlib.sha256(image).digest()
generated=O/'image.c';generated.write_text('#include "preload_bundle.h"\nstatic const unsigned char image[]={'+','.join(map(str,image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={image,sizeof image,{'+','.join(map(str,sha))+'},"'+'0'*64+'",10000};\n')
actual_dependencies=[S/'library/source_companions.c',S/'library/source_companions.h',S/'streamreader/manifest.c',S/'streamreader/manifest.h',S/'library/preload_companions_internal.h']
inputs=[R/'ready_observe_test.c',S/'library/native_backend.c',S/'library/native_backend_home.h',S/'library/preload_bundle.c',S/'library/preload_bundle.h',S/'library/preload_source_ampr_internal.h',S/'streamreader/share_config.c',Path(__file__)]+actual_dependencies
def pins():return {str(p.relative_to(B)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs}
before=pins()
command=['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(S/'library'),'-I'+str(S/'streamreader'),'-I'+str(S/'streamreader/diagnostics'),'-I'+str(B/'vendor/libsmb2/include'),str(R/'ready_observe_test.c'),str(generated),str(S/'streamreader/share_config.c'),*[str(p) for p in actual_dependencies if p.suffix=='.c'],'-Wl,--gc-sections','-Wl,--wrap=close','-pthread','-lm','-o',str(O/'test')]
commands=[]
for label,argv in [('compile',command),('run',[str(O/'test'),str(fixture)])]:
    result=subprocess.run(argv,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
    commands.append({'label':label,'argv':argv,'exitCode':result.returncode,'stdout':result.stdout,'stderr':result.stderr});(O/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if result.returncode:raise RuntimeError(label+' failed; private host evidence retained')
after=pins()
if before!=after:raise RuntimeError('Inputs changed during test')
proof={'passed':True,'result':json.loads(result.stdout),'source':after,'sourceBeforeAfterMatch':True,'actualReadinessAndBundleImplementation':True,'receiptSourceRegistrationAdapters':'explicit recording model; independently qualified by producer and connector suites','consoleActions':0,'moduleExecution':False}
(O/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof['result']))
