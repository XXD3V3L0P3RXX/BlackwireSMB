#!/usr/bin/env python3
"""Check first-read PFSC batching, exact bytes and bounded/failing fetches."""
from pathlib import Path
import argparse,json,os,random,subprocess,tempfile
from fixtures import pfsc

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=root/'src/blackwire/streamreader';exe=out/'pfsc-batching'
    command=['clang-18','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(source),
             str(root/'tests/compressed/pfsc_batch_test.c'),str(source/'pfsc_range.c'),str(source/'pfsc_inflate.c'),'-o',str(exe)]
    result=subprocess.run(command,capture_output=True);(out/'compile.log').write_bytes(result.stdout+result.stderr);result.check_returncode()
    reports=[]
    with tempfile.TemporaryDirectory(prefix='blackwire-pfsc-batch-') as work:
        temp=Path(work)
        compressible=b''.join(bytes((i+j)%251 for j in range(65536)) for i in range(16))
        incompressible=random.Random(41491).randbytes(1024*1024)
        for label,body,calls in [('compressed',compressible,1),('stored',incompressible,4)]:
            image=temp/(label+'.pfsc');original=temp/(label+'.bin');image.write_bytes(pfsc(body));original.write_bytes(body)
            result=subprocess.run([str(exe),str(image),str(original),str(calls)],capture_output=True,
                env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
            (out/(label+'.log')).write_bytes(result.stdout+result.stderr);result.check_returncode()
            report=json.loads(result.stdout);report['case']=label;reports.append(report)
    proof={'passed':True,'cases':reports,'consoleContacted':False,'syntheticOnly':True}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps(proof))

if __name__=='__main__':main()
