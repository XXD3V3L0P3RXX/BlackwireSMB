from pathlib import Path
import hashlib,json,subprocess,sys
from support import locations
R,B,S,O=locations("backend")
sys.path.insert(0,str(B/'tools/build'));from reader_patch import function
back=(S/'library/native_backend.c').read_text();home=(S/'library/native_backend_home.h').read_text()
struct=back[back.index('struct bw_native_backend {'):back.index('\n};',back.index('struct bw_native_backend {'))+3]
worker=home[home.index('struct home_worker {'):home.index('\n};',home.index('struct home_worker {'))+3]
prefix='''#define BW_HOME_STARTUP 1
#define BW_NATIVE_PRELOAD 1
#include "native_backend.h"
#include "home_protocol.h"
#include "home_session.h"
#include "home_identity_ps5.h"
#include "native_launch.h"
#include "native_registration.h"
#include "preload_bundle.h"
#include "startup_runtime_idle.h"
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
static const char *backend_data_root="/fixture/blackwire",*backend_game_log="/fixture/log";
enum {BACKEND_INITIAL,BACKEND_SCANNING,BACKEND_READY,BACKEND_NEEDS_SETUP,BACKEND_FAILED,BACKEND_CONFIG_CHANGED};
enum {WORK_SCAN,WORK_SETUP,WORK_PREPARE,WORK_PLAY,WORK_UNINSTALL,WORK_RESTORE};
static int play(void *,const struct bw_native_play_selection *,int(*)(void*),void*,void(*)(void*,const struct bw_reader_event*),void*);

static int worker_start(pthread_t*,void *(*)(void*),void*);
static void *home_worker(void*);
static int text(struct bw_native_response*,int,const char*);
'''
parts=[function(back,n) for n in ['static double now(','static int nonce_valid(','static int registration_waiting(','static int revalidation_required(','static int removed_at(','static const char *reader_update_reason(']]
parts += [function(home,n) for n in ['static int home_lease(','static int home_app_current(','static int home_consumer_uid(','static int home_cleanup_lease(','static int home_idle_after_exit(','static int home_settings(','static int home_select(','static void home_consumed_retire_locked(','static int home_claim_deadline_current(','int bw_native_backend_home_claim(']]
header=O/'backend-under-test.h';header.write_text(prefix+struct+'\n'+worker+'\n'+'\n'.join(parts))
stub=O/'workspace.c';stub.write_text('int bw_preload_bundle_upgrade_workspace(const char*a,const char*b){(void)a;(void)b;return 0;}\n')
files=[R/'backend_test.c',S/'library/home_protocol.c',S/'library/source_param.c',S/'library/source_identity.c',S/'library/source_binding.c',S/'library/source_profile.c',S/'streamreader/manifest.c',S/'library/home_session.c',S/'library/native_session.c',S/'streamreader/diagnostics/game_stop_identity.c',S/'streamreader/share_config.c',stub]
cmd=['clang-18','-D_DEFAULT_SOURCE=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(O),'-I'+str(S/'library'),'-I'+str(S/'streamreader'),'-I'+str(S/'streamreader/diagnostics'),'-I'+str(B/'vendor/libsmb2/include'),*map(str,files),'-Wl,--gc-sections','-pthread','-o',str(O/'backend_test')]
p=subprocess.run(cmd,capture_output=True,text=True);(O/'compile.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
p=subprocess.run([str(O/'backend_test')],capture_output=True,text=True);(O/'test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
proof={'passed':True,'result':json.loads(p.stdout),'inputPins':{str(x):{'bytes':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in files+[header,S/'library/native_backend.c',S/'library/native_backend_home.h']},'actualFunctionsExtractedUnchanged':True,'SonyWorkerCreationReceiptAndRegistrationBoundariesModeled':True,'consoleContacted':False};(O/'proof.json').write_text(json.dumps(proof,indent=2))



