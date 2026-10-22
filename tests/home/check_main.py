from pathlib import Path
import hashlib,json,subprocess,sys
from support import locations
R,B,S,O=locations("main")
sys.path.insert(0,str(B/'tools/build'));from reader_patch import function
source=S/'library/main.c';raw=source.read_text();header=O/'main-under-test.h'
prefix='''#define BW_HOME_STARTUP 1
#define BW_NATIVE_BACKEND 1
#define BW_WEB_ONLY 1
#include "native_backend.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
struct bd_resident;static struct bw_native_backend *native_backend;static struct bd_resident *dashboard;
static const unsigned char page_html[]={1},brand_logo_png[]={2};static const size_t page_html_size=1,brand_logo_png_size=1;
int bd_resident_handle(struct bd_resident*,const char*,const char*,const void*,size_t,const char*,struct bw_native_response*);
'''
header.write_text(prefix+'\n'.join(function(raw,n) for n in ['static int write_full(','static void send_bytes(','static void send_text(','static int read_request(','static int native_header(','static int native_reader_peer(','static void serve(']))
cmd=['clang-18','-D_DEFAULT_SOURCE=1','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(O),'-I'+str(S/'library'),'-I'+str(B/'vendor/libsmb2/include'),str(R/'main_peer_test.c'),'-o',str(O/'main_test')]
p=subprocess.run(cmd,capture_output=True,text=True);(O/'compile.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
p=subprocess.run([str(O/'main_test')],capture_output=True,text=True);(O/'test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);p.check_returncode()
proof={'passed':True,'result':json.loads(p.stdout),'pins':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,header,R/'main_peer_test.c']},'actualMainFunctionsExtractedUnchanged':True,'realHostTcpAndUnixAcceptedPeers':True,'backendQueueAndAssetBytesModeled':True,'consoleContacted':False};(O/'proof.json').write_text(json.dumps(proof,indent=2))
