#!/usr/bin/env python3
"""Offline actual libsmb2 CMAC vectors, key-schedule counts and context reuse."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
def pin(path):
    raw=path.read_bytes();return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',required=True,type=Path);args=ap.parse_args()
    here=Path(__file__).resolve().parent;root=here.parents[1];vendor=root/'vendor/libsmb2';out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    macros='ARPA_INET_H DLFCN_H FCNTL_H INTTYPES_H NETDB_H NETINET_IN_H NETINET_TCP_H POLL_H SOCKADDR_STORAGE LINGER STDDEF_H STDINT_H STDIO_H STDLIB_H STRINGS_H STRING_H SYS_IOCTL_H SYS_POLL_H SYS_SOCKET_H SYS_STAT_H SYS_TYPES_H SYS_UIO_H TIME_H SYS_TIME_H UNISTD_H ERRNO_H'.split()
    (out/'config.h').write_text('\n'.join('#define HAVE_'+m+' 1' for m in macros)+'\n#define STDC_HEADERS 1\n#define VERSION "host-cmac-test"\n',encoding='utf-8')
    sources=[here/'cmac_schedule_test.c',vendor/'lib/aes.c',vendor/'lib/smb2-signing.c'];before={str(p.relative_to(root)):pin(p) for p in [*sources,vendor/'lib/aes.h']}
    flags=['clang-18','-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-misleading-indentation',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,--wrap=AES128_init_ctx','-Wl,--wrap=AES128_ECB_encrypt',
        '-DHAVE_CONFIG_H','-D_GNU_SOURCE','-D_U_=__attribute__((unused))','-I'+str(out),'-I'+str(vendor/'lib'),'-I'+str(vendor/'include'),'-I'+str(vendor/'include/smb2'),
        '-fsanitize=address,undefined','-fno-omit-frame-pointer']
    records=[];modes=[]
    def run(label,argv):
        r=subprocess.run(argv,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
        (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');records.append({'label':label,'argv':argv,'exitCode':r.returncode})
        (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if r.returncode:raise RuntimeError(label+' failed; inspect output log')
        return r.stdout
    for mode,extra in [('automatic',[]),('forced-software',['-DBLACKWIRE_AES_TEST_FORCE_SOFTWARE=1'])]:
        exe=out/('cmac-'+mode);run('compile-'+mode,[*flags,*extra,*map(str,sources),'-o',str(exe)])
        report=json.loads(run('test-'+mode,[str(exe)]));report['mode']=mode
        if mode=='forced-software':assert report['hardwareEncryptionUsed'] is False
        generic=run('generic-instructions-'+mode,['llvm-objdump-18','-d','--disassemble-symbols=AES128_init_ctx,AES128_ECB_encrypt_ctx,AES128_ECB_encrypt',str(exe)])
        assert not re.search(r'\s(?:aesenc|aesenclast)\s',generic)
        if report['hardwareEncryptionUsed']:
            isolated=run('hardware-instructions-'+mode,['llvm-objdump-18','-d','--disassemble-symbols=AES128_encrypt_aesni',str(exe)])
            assert re.search(r'\saesenc\s',isolated) and re.search(r'\saesenclast\s',isolated)
        modes.append(report)
    after={str(p.relative_to(root)):pin(p) for p in [*sources,vendor/'lib/aes.h']};assert before==after
    proof={'passed':all(r['passed'] for r in modes),'modes':modes,'sourceBeforeAfterMatched':True,'source':after,
        'rfc4493':'https://www.rfc-editor.org/rfc/rfc4493#section-4','actualProductionCmacCompiled':True,'testTransport':'none',
        'genericEntryPointsContainNoAesInstructions':True,'fallbackTested':True,'consoleContacted':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in proof.items() if k!='source'}))
if __name__=='__main__':main()
