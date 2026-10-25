#!/usr/bin/env python3
"""Read-only exFAT range tests and corrupt-metadata refusal, no game material."""
from pathlib import Path
import argparse,json,os,random,struct,subprocess,tempfile
from fixtures import pfsc,wrapped
from fixtures_exfat import make_exfat,entry_checksum,checksum

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=root/'src/blackwire/streamreader';exe=out/'exfat-compare'
    cmd=['clang-18','-D_DEFAULT_SOURCE','-DBW_COMPARE_ALL_BYTES','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
         '-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
         '-I'+str(source),str(root/'tests/compressed/container_compare.c'),
         *[str(source/n) for n in ('container_source.c','pfs_source.c','pfsc_range.c','pfsc_inflate.c','manifest.c')],'-o',str(exe)]
    p=subprocess.run(cmd,capture_output=True);(out/'compile.log').write_bytes(p.stdout+p.stderr);p.check_returncode()
    contents={'eboot.bin':b'BLACKWIRE-NONEXECUTABLE-FIXTURE','sce_sys/param.json':b'{"titleId":"PPSA99001"}',
              'assets/bulk.bin':random.Random(829).randbytes(2*1024*1024+317),'assets/zero.bin':b'',
              'unicode/\u00e9clair.bin':b'unicode-one','unicode/\U0001f680.bin':b'unicode-two'}
    image,meta=make_exfat(contents);nofat,_=make_exfat(contents,nofat=True)
    variants=[('bare',image,True),('pfsc',pfsc(image),True),('wrapped',wrapped(image),True),
              ('wrapped-plain',wrapped(image,encode=False),True),('nofat',nofat,True)]
    def broken(label,mutate,repair=None):
        bad=bytearray(image);mutate(bad)
        if repair is not None:entry_checksum(bad,meta['entries'][repair])
        variants.append((label,bad,False))
    bulk=meta['entries']['assets/bulk.bin'];first=meta['nodes']['assets/bulk.bin']['first']
    broken('boot-checksum',lambda b:b.__setitem__(100,b[100]^1))
    broken('backup-checksum',lambda b:b.__setitem__(12*512+100,b[12*512+100]^1))
    broken('entry-checksum',lambda b:b.__setitem__(bulk+8,b[bulk+8]^1))
    broken('root-cycle',lambda b:struct.pack_into('<I',b,meta['fat']+meta['nodes']['']['first']*4,meta['nodes']['']['first']))
    broken('fragmented',lambda b:struct.pack_into('<I',b,meta['fat']+first*4,first+2))
    broken('bitmap-unallocated',lambda b:b.__setitem__(meta['bitmap']+(first-2)//8,b[meta['bitmap']+(first-2)//8]&~(1<<((first-2)%8))))
    broken('upcase-checksum',lambda b:b.__setitem__(meta['upcase']+3,b[meta['upcase']+3]^1))
    broken('short-valid-data',lambda b:struct.pack_into('<Q',b,bulk+40,len(contents['assets/bulk.bin'])-1),'assets/bulk.bin')
    broken('overflow-length',lambda b:struct.pack_into('<Q',b,bulk+56,0xffffffffffffffff),'assets/bulk.bin')
    broken('bad-stream-count',lambda b:b.__setitem__(bulk+1,18))
    broken('critical-root-entry',lambda b:b.__setitem__(meta['root'],0x84))
    def overlap(b):
        at=meta['entries']['unicode/\u00e9clair.bin'];struct.pack_into('<I',b,at+52,first);struct.pack_into('<Q',b,at+40,len(contents['assets/bulk.bin']));struct.pack_into('<Q',b,at+56,len(contents['assets/bulk.bin']));entry_checksum(b,at)
    broken('overlap',overlap)
    variants.append(('truncated',image[:-1],False))
    variants.append(('duplicate-ascii',make_exfat({'A.bin':b'one','a.bin':b'two'})[0],False))
    variants.append(('duplicate-unicode',make_exfat({'\u00e9.bin':b'one','\u00c9.bin':b'two'})[0],False))
    variants.append(('traversal',make_exfat({'../escape.bin':b'data'})[0],False))
    reports=[]
    with tempfile.TemporaryDirectory(prefix='blackwire-exfat-') as work:
        temp=Path(work);folder=temp/'originals';folder.mkdir()
        for name,body in contents.items():
            p=folder/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
        for label,body,accepted in variants:
            path=temp/(label+'.image');path.write_bytes(body)
            p=subprocess.run([str(exe),str(path),str(folder)],capture_output=True,
                             env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
            (out/(label+'.log')).write_bytes(p.stdout+p.stderr)
            report=json.loads(p.stdout);report.update(case=label,exitCode=p.returncode);reports.append(report)
            if p.returncode not in (0,1) or b'Sanitizer' in p.stderr or report['passed']!=accepted:
                raise AssertionError((label,report,p.stderr.decode()))
            if accepted and (report['filesCompared']!=len(contents) or report['bytesCompared']!=sum(map(len,contents.values()))):raise AssertionError(report)
        proof={'passed':True,'cases':len(reports),'fullFileBytesCompared':sum(map(len,contents.values())),
               'consoleContacted':False,'reports':reports}
        (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k!='reports'}))

if __name__=='__main__':main()
