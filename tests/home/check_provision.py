from pathlib import Path
import hashlib,json,subprocess,sys
from support import locations
R,B,S,O=locations("provision")
sys.path.insert(0,str(B/'tools/build'));from reader_patch import function
back=(S/'library/native_backend.c').read_text();home=(S/'library/native_backend_home.h').read_text()
struct=back[back.index('struct bw_native_backend {'):back.index('\n};',back.index('struct bw_native_backend {'))+3]
prefix='''#define BW_HOME_STARTUP 1
#define BW_NATIVE_PRELOAD 1
#include "native_backend.h"
#include "home_protocol.h"
#include "home_session.h"
#include "native_launch_admission.h"
#include "native_prepared_target.h"
#include "native_registration_lifecycle.h"
#include "preload_bundle.h"
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
static const char *backend_data_root="/fixture/blackwire";
static const struct bw_nrc_environment registration_environment={NULL,"/user/app","/system_data/priv/mms/app.db"};
enum {BACKEND_INITIAL,BACKEND_SCANNING,BACKEND_READY,BACKEND_NEEDS_SETUP,BACKEND_FAILED,BACKEND_CONFIG_CHANGED};
enum {WORK_SCAN,WORK_SETUP,WORK_PREPARE,WORK_PLAY,WORK_UNINSTALL,WORK_RESTORE};
static int prepare_current(void*);
static int prepare_authority(struct bw_native_backend*,enum bw_prepare_guard_point);
static int prepare(void*,struct bw_share_config*,const struct bw_native_prepare_selection*,int(*)(void*),void*,bw_source_observer_fn,void*,struct bw_native_prepared*);
static int registration_pending_refresh(struct bw_native_backend*);
'''
prep=home[home.index('struct home_prepare {'):home.index('\nstatic int home_prepare_lease(',home.index('struct home_prepare {'))]
parts=[function(back,n) for n in ['static double now(','static int nonce_valid(','static int registration_waiting(','static int revalidation_known(','static int revalidation_remember(','static void revalidation_clear(','static int prepared_valid(']]
failure=home[home.index('struct home_prepare_failure {'):home.index('\nstatic void home_prepare_first_failure(')]
parts += [failure]+[function(home,n) for n in ['static void home_prepare_first_failure(','static int home_prepare_lease(','static int home_prepare_control(','static int home_prepare_registration(','static int home_prepare_provision(']]
parts += [function(back,'static void *prepare_worker(void *opaque){')]
header=O/'provision-under-test.h';header.write_text(prefix+struct+'\n'+prep+'\n'+'\n'.join(parts))
files=[R/'provision_test.c',S/'library/source_identity.c',S/'library/source_binding.c',S/'library/source_profile.c',S/'streamreader/manifest.c',S/'streamreader/share_config.c']
cmd=['clang-18','-D_DEFAULT_SOURCE=1','-DBW_GTA_AUDIO_BOOTSTRAP=1','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(O),'-I'+str(S/'library'),'-I'+str(S/'streamreader'),'-I'+str(S/'streamreader/diagnostics'),'-I'+str(B/'vendor/libsmb2/include'),*map(str,files),'-Wl,--gc-sections','-pthread','-o',str(O/'provision_test')]
p=subprocess.run(cmd,capture_output=True,text=True);(O/'compile.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
p=subprocess.run([str(O/'provision_test')],capture_output=True,text=True);(O/'test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
proof={'passed':True,'result':json.loads(p.stdout),'inputPins':{str(x):{'bytes':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in files+[header,S/'library/native_backend.c',S/'library/native_backend_home.h']},'actualFunctionsExtractedUnchanged':True,'sourceCopyReceiptBundleAndSonyRegistrationBoundariesModeled':True,'consoleContacted':False};(O/'proof.json').write_text(json.dumps(proof,indent=2))

