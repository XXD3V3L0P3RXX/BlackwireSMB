"""Actual dashboard worker: failed admission ordinary refusal versus unconfirmed release."""
from pathlib import Path
import hashlib,json,os,subprocess
from support import locations
R,B,S,O=locations('worker-release')
import sys
sys.path.insert(0,str(B/'tools/build'))
from reader_patch import function
back=(S/'library/native_backend.c').read_text();home=(S/'library/native_backend_home.h').read_text()
structure=back[back.index('struct bw_native_backend {'):back.index('\n};',back.index('struct bw_native_backend {'))+3]
worker=home[home.index('struct home_worker {'):home.index('\nstatic void home_phase(',home.index('struct home_worker {'))]
prefix='''#define BW_HOME_STARTUP 1
#define BW_NATIVE_PRELOAD 1
#include "native_backend.h"
#include "home_protocol.h"
#include "home_session.h"
#include "native_launch_admission.h"
#include "preload_bundle.h"
#include "launch_timing.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static const char *backend_data_root="/fixture/blackwire";
enum {WORK_SCAN,WORK_SETUP,WORK_PREPARE,WORK_PLAY,WORK_UNINSTALL,WORK_RESTORE};
'''
declarations='''
static void home_phase(struct home_worker *,unsigned,const char *);
static int home_lease(void *);
static int home_select(struct home_worker *,struct bw_share_config *);
static int home_startup_control(struct home_worker *,const struct bw_reader_selection *);
static int home_source(struct home_worker *,const struct bw_share_config *,const struct bw_reader_selection *);
static void play_failure_locked(struct bw_native_backend *,const char *);
static void home_consumed_retire_locked(struct bw_native_backend *,const struct bw_stop_identity *);
'''
header=O/'worker-release-under-test.h';header.write_text(prefix+structure+'\n'+worker+'\n'+declarations+function(back,'static double now(')+'\n'+function(home,'static void *home_worker(')+'\n')
inputs=[R/'worker_release_test.c',Path(__file__),S/'library/native_backend.c',S/'library/native_backend_home.h',S/'library/native_launch_admission.h']
pins=lambda:{str(p.relative_to(B)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs}
before=pins()
command=['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
for d in [O,S/'library',S/'streamreader',S/'streamreader/diagnostics',B/'vendor/libsmb2/include']:command+=['-I',str(d)]
command += [str(R/'worker_release_test.c'),str(S/'streamreader/share_config.c'),str(S/'streamreader/diagnostics/game_stop_identity.c'),'-Wl,--gc-sections','-pthread','-lm','-o',str(O/'test')]
records=[]
for label,argv in [('compile',command),('run',[str(O/'test')])]:
 r=subprocess.run(argv,capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
 records.append(dict(label=label,argv=argv,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));(O/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 if r.returncode:raise RuntimeError(label+' failed; evidence retained')
if before!=pins():raise RuntimeError('Source changed during test')
proof={'passed':True,'result':json.loads(r.stdout),'source':before,'sourceBeforeAfterMatched':True,'boundaryScope':'Failed admission and Sony/process session boundaries explicitly modeled; actual worker body, no source/bundle/launch path allowed.','consoleActions':0}
(O/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof['result']))

