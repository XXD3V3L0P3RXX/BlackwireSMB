#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic container admission diagnostics and mixed-library isolation."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tests/compressed'))
from fixtures import make_pfs,pfsc,wrapped
from fixtures_exfat import make_exfat
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
OUT=parser.parse_args().output.resolve()
if not OUT.is_relative_to(ROOT/'build') or OUT.exists():raise ValueError('Use a fresh output below build/')
OUT.mkdir(parents=True)
work=tempfile.TemporaryDirectory(prefix='blackwire-catalog-admission-');FIXTURES=Path(work.name)
names=['library/source_catalog.c','library/source_param.c','library/source_plan.c','library/compressed_data.c','library/source_profile.c','library/source_binding.c','streamreader/manifest.c','streamreader/container_source.c','streamreader/container_manifest.c','streamreader/container_smb.c','streamreader/pfs_source.c','streamreader/pfsc_range.c','streamreader/pfsc_inflate.c']
inputs=[ROOT/'src/blackwire'/n for n in names]+[HERE/'catalog_admission_test.c']
tracked=inputs+[Path(__file__),HERE/'fixtures.py',HERE/'fixtures_exfat.py',HERE/'namespace_limit_test.c',ROOT/'src/blackwire/library/source_catalog.h',ROOT/'src/blackwire/library/compressed_data.h']
# Include transitive source headers and miniz's included implementation unit,
# not only the top-level compile inputs.
for directory in (ROOT/'src/blackwire',ROOT/'vendor/libsmb2/include',ROOT/'vendor/sqlite',ROOT/'vendor/ampr/third_party/lz4/lib'):
 tracked += list(directory.rglob('*.h'))
tracked += list((ROOT/'src/blackwire/streamreader/vendor/miniz').glob('*.c'))
backend_sources=[HERE/'backend_catalog_diagnostic_test.c',ROOT/'src/blackwire/library/startup_runtime_idle.c',
 ROOT/'src/blackwire/streamreader/diagnostics/game_stop_identity.c']
backend_sources += [ROOT/'src/blackwire/library'/n for n in ('native_job.c','source_catalog.c','source_binding.c','source_identity.c','native_request.c','source_profile.c')]
backend_sources += [ROOT/'src/blackwire/streamreader'/n for n in ('share_config.c','manifest.c')]
tracked += backend_sources+[ROOT/'src/blackwire/library/native_backend.c',ROOT/'tools/build/reader_patch.py',HERE.parent/'dashboard/lifecycle/test_play_preparation_failure.c']
tracked += list((ROOT/'src/blackwire/library').glob('*.h'))

def pin(p):return hashlib.sha256(p.read_bytes()).hexdigest()
before={str(p):pin(p) for p in tracked}
commands=[]
def run(argv):
 p=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
 commands.append({'argv':list(map(str,argv)),'exitCode':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 (OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if p.returncode:raise RuntimeError(p.stderr)
 return p.stdout
binary=OUT/'catalog-admission-test'
run(['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',ROOT/'src/blackwire','-I',ROOT/'vendor/libsmb2/include',*inputs,'-Wl,--gc-sections','-o',binary])
base={'eboot.bin':b'NEUTRAL SYNTHETIC STARTUP NOT EXECUTABLE','sce_sys/param.json':b'{"titleId":"PPSA09999","titleName":"Neutral Fixture"}'}
raw,layout=make_pfs(base)
exfat,exlayout=make_exfat(base)
variants=[]
def add(label,data,expected='available',error=0,suffix='.ffpfsc'):
 p=FIXTURES/(label+suffix);p.write_bytes(data);variants.append((label,p,expected,error))
add('valid-raw-pfs',raw)
add('valid-pfsc-pfs',pfsc(raw))
add('valid-wrapped-pfsc-pfs',wrapped(raw))
add('valid-raw-exfat',exfat)
add('valid-pfsc-exfat',pfsc(exfat))
add('valid-wrapped-pfsc-exfat',wrapped(exfat))
add('unrecognized-extension-valid-pfs',raw,None,0,'.ffpfs')
add('undersized-image',b'neutral','unsupported_format',-4)
add('unrecognized-header',bytes(48),'unsupported_format',-4)
add('missing-root-eboot',make_pfs({'nested/eboot.bin':b'neutral','sce_sys/param.json':base['sce_sys/param.json']})[0],'container_unverified',-2)
add('zero-root-eboot',make_pfs({**base,'eboot.bin':b''})[0],'container_unverified',-2)
add('oversize-param',make_pfs({**base,'sce_sys/param.json':bytes(64*1024+1)})[0],'container_unverified',-2)
add('missing-root-param',make_pfs({'eboot.bin':base['eboot.bin'],'param.json':base['sce_sys/param.json']})[0],'container_unverified',-2)
add('invalid-title-metadata',make_pfs({'eboot.bin':base['eboot.bin'],'sce_sys/param.json':b'{"titleId":"NOT-A-PPSA","titleName":"Neutral"}'})[0],'container_unverified',-2)
add('signed-pfs-variant',make_pfs(base,mode=0x10)[0],'unsupported_format',-4)
inode_limit=bytearray(raw);struct.pack_into('<Q',inode_limit,0x30,131073)
add('parser-inode-budget',inode_limit,'container_unverified',-2)
module={**base,'sce_module/required.prx':b'neutral unsupported inner-compressed module'};_,m=make_pfs(module)
add('compressed-required-module',make_pfs(module,compressed={m['ids']['sce_module/required.prx']:100})[0],'unsupported_format',-4)

inner=dict(base);inner['asset.bin']=b'neutral compressed marker';_,a=make_pfs(inner)
add('compressed-inner-file',make_pfs(inner,compressed={a['ids']['asset.bin']:100})[0],'unsupported_format',-4)
excluded=dict(base);excluded['unneeded.bak']=b'neutral excluded'
_,a=make_pfs(excluded)
add('compressed-excluded-inner-file',make_pfs(excluded,compressed={a['ids']['unneeded.bak']:100})[0])
for n in (4096,4097):
 files=dict(base)
 for i in range(n-2):files[f'd{i//50:03d}/f{i:04d}.bin']=b'x'
 namespace=make_pfs(files)[0];add(f'namespace-{n}-files',namespace)
 if n==4097:
  add('namespace-4097-pfsc-pfs',pfsc(namespace))
  add('namespace-4097-wrapped-pfsc-exfat',wrapped(make_exfat(files)[0]))

# Regression thresholds: the former 1MiB path ceiling was separate from the former 4096-file ceiling.
longfiles=dict(base)
for i in range(4050):
 group=i//14
 leaf=f'{i:05d}-'+'x'*238
 longfiles[f'g{group//50:02d}/subgroup-{group:03d}/'+leaf]=b'x'
add('namespace-path-budget',make_pfs(longfiles)[0])
# A well-formed but fragmented exFAT allocation: two occupied adjacent data clusters are swapped in FAT.
frag_files={**base,'asset.bin':b'x'*8192}
frag,fl=make_exfat(frag_files)
first=fl['nodes']['asset.bin']['first']
# Alternate second cluster is otherwise unused; mark allocated and extend volume's source by filling its existing slot.
second=first+3
struct.pack_into('<I',frag,fl['fat']+first*4,second)
struct.pack_into('<I',frag,fl['fat']+second*4,0xffffffff)
add('fragmented-exfat',frag,'unsupported_format',-4)
reports=[]
stages={
 'undersized-image':'image_size','unrecognized-header':'container_open',
 'missing-root-eboot':'boot_file','zero-root-eboot':'boot_file',
 'missing-root-param':'param_file','oversize-param':'param_file',
 'invalid-title-metadata':'param_metadata','signed-pfs-variant':'container_open',
 'compressed-inner-file':'namespace','compressed-required-module':'namespace','parser-inode-budget':'container_open','fragmented-exfat':'container_open'}
errors={0:'none',-2:'invalid_format',-4:'unsupported_format',-6:'metadata_limit'}
def check(label,p,status,error):
 prepare=label in ('namespace-4097-files','namespace-4097-pfsc-pfs','namespace-4097-wrapped-pfsc-exfat','namespace-path-budget','compressed-excluded-inner-file')
 r=json.loads(run([binary,*(['--prepare'] if prepare else []),p]));r['label']=label
 assert r['result']==0,(label,r)
 if status is None:
  assert r['items']==0 and r['catalog']['skipped']==1,(label,r)
  assert all(r['catalog'][x]==0 for x in ('containerCandidates','containersValidated','containersRejected')),(label,r)
 else:
  assert r['items']==1 and r['catalog']['items'][0]['status']==status,(label,r)
  assert r['internalErrors']==[error],(label,r)
  item=r['catalog']['items'][0]
  assert item['containerErrorCode']==error and item['containerErrorName']==errors[error],(label,r)
  assert item['containerStage']==stages.get(label,'validated'),(label,r)
  assert r['catalog']['containerCandidates']==1 and r['catalog']['containersValidated']==int(error==0) and r['catalog']['containersRejected']==int(error!=0),(label,r)
  assert 0<=item['namespaceFilesObserved']<=131072 and 0<=item['namespacePathBytesObserved']<=32*1024*1024,(label,r)
  if label=='namespace-4096-files':assert item['namespaceFilesObserved']==4096,(label,r)
  if label.startswith('namespace-4097'):assert item['namespaceFilesObserved']==4097 and r['preparedFiles']==4097,(label,r)
  if label=='namespace-path-budget':assert item['namespacePathBytesObserved']>1024*1024 and r['preparedFiles']==4052,(label,r)
  if label=='compressed-excluded-inner-file':assert item['namespaceFilesObserved']==2 and r['preparedFiles']==2,(label,r)
  if prepare:assert r['hashedFiles']==r['copiedFiles']==2 and r['hashedBytes']==r['copiedBytes']>0,(label,r)
 reports.append(r)
for variant in variants:check(*variant)
# Vary location of rejected images to detect aborted or reordered valid neighbors.
bylabel={label:p for label,p,_,_ in variants}
mixed=[]
for i in range(52):
 p=FIXTURES/f'mixed-{i:02d}.ffpfsc';p.write_bytes(raw if i>=25 else bylabel['compressed-inner-file'].read_bytes());mixed.append(p)
for order,paths in (('forward',mixed),('reverse',list(reversed(mixed))),('interleaved',[mixed[-1],*mixed[:-2],mixed[-2]])):
 r=json.loads(run([binary,*paths]));r['label']='controlled-mixed-'+order
 assert r['result']==0 and r['items']==52,r
 accepted=sum(x['status']=='available' for x in r['catalog']['items'])
 assert accepted==27 and r['internalErrors'].count(-4)==25,r
 assert r['catalog']['containerCandidates']==52 and r['catalog']['containersValidated']==27 and r['catalog']['containersRejected']==25,r
 assert sorted(x['source'] for x in r['catalog']['items'])==sorted(x.name for x in paths),r
 for item in r['catalog']['items']:
  assert (item['containerErrorCode'],item['containerErrorName'],item['containerStage'])==((0,'none','validated') if item['status']=='available' else (-4,'unsupported_format','namespace')),item
 reports.append(r)
# Maximum shared budgets are tested with the actual callbacks/planner over a
# generated metadata stream. Real parsers count directories too, so this does
# not claim a 131072-file disk image passed its independent inode/entry cap.
boundaries=[]
for preparation in (False,True):
 exe=OUT/('namespace-preparation-test' if preparation else 'namespace-catalog-test')
 flags=['clang-18','-std=gnu11','-D_DEFAULT_SOURCE=1','-O1','-g','-Wall','-Wextra','-Werror',
        '-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
        '-I',ROOT/'src/blackwire','-I',ROOT/'vendor/libsmb2/include']
 if preparation:flags+=['-DBW_NAMESPACE_PREPARATION=1']
 run([*flags,HERE/'namespace_limit_test.c',ROOT/'src/blackwire/library/source_plan.c',
      ROOT/'src/blackwire/streamreader/manifest.c','-Wl,--gc-sections','-o',exe])
 boundaries.append(json.loads(run([exe])))
# Exercise the maintained complete /api/state serializer with its existing
# ownership/transport spies, rather than duplicating the new JSON fragment.
backend=OUT/'backend-catalog-diagnostic-test'
sys.path.insert(0,str(ROOT/'tools/build'))
from reader_patch import function
backend_original=(ROOT/'src/blackwire/library/native_backend.c').read_text()
backend_fixture=backend_original
# state() compares these addresses but must never execute a console operation.
# Replace only their bodies with aborting link spies; retain the complete real
# serializer and its status helpers byte-for-byte.
for signature,parameters in (
 ('static int prepare(',('opaque','config','selection','current','guard','observe','observer','out')),
 ('static int play(',('opaque','selected','current','guard','observe','observer'))):
 original=function(backend_fixture,signature)
 spy='{'+''.join('(void)'+p+';' for p in parameters)+'abort();}'
 backend_fixture=backend_fixture.replace(original,original.split('{',1)[0]+spy)
def state_section(text):
 start=text.index('static int state(');end=text.index('\nstatic int text(',start)
 return text[start:end]
assert state_section(backend_original)==state_section(backend_fixture)
(OUT/'native_backend.c').write_text(backend_fixture)
includes=[OUT,ROOT/'src/blackwire/library',ROOT/'src/blackwire/streamreader',
 ROOT/'src/blackwire/streamreader/diagnostics',ROOT/'vendor/libsmb2/include',
 ROOT/'vendor/sqlite',ROOT/'src/blackwire/tools/compressed_stream_candidate']
flags=['clang-18','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
 '-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable',
 '-ffunction-sections','-fdata-sections','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread',
 '-DBW_COPY_HOST_TEST=1','-DBW_NATIVE_SEND_HOST_TEST=1','-DBW_NATIVE_FS_HOST_TEST=1',
 '-DBW_NATIVE_PRELOAD=1','-DBW_GTA_AUDIO_BOOTSTRAP=1']
run([*flags,*[str(v) for d in includes for v in ('-I',d)],*backend_sources,
 '-Wl,--wrap=bw_idle_log_cache_close','-Wl,--gc-sections','-pthread','-lm','-o',backend])
backend_report=json.loads(run([backend]))
assert backend_report['passed'] and backend_report['actualBackendStateJson']
for label in ('configured','unknown'):
 state=backend_report[label];summary=state['library']
 assert (summary['items'],summary['containerCandidates'],summary['containersValidated'],summary['containersRejected'])==(3,2,1,1),state
 rows=state['games'];assert len(rows)==3
 assert (rows[0]['containerErrorCode'],rows[0]['containerErrorName'],rows[0]['containerStage'])==(0,'not_applicable','not_checked'),rows[0]
 assert (rows[1]['containerErrorCode'],rows[1]['containerErrorName'],rows[1]['containerStage'],rows[1]['namespaceFilesObserved'],rows[1]['namespacePathBytesObserved'])==(0,'none','validated',5000,1200000),rows[1]
 expected=(-6,'metadata_limit','namespace') if label=='configured' else (-1234,'unknown_error','unknown')
 assert (rows[2]['containerErrorCode'],rows[2]['containerErrorName'],rows[2]['containerStage'])==expected,rows[2]
hidden=backend_report['hidden'];assert hidden['games']==[] and all(hidden['library'][x]==0 for x in ('items','containerCandidates','containersValidated','containersRejected')),hidden
assert before=={str(p):pin(p) for p in tracked},'Production changed while check ran'
proof={'passed':True,'cases':len(reports)+sum(x['cases'] for x in boundaries)+backend_report['cases'],'checks':sum(r['checks'] for r in reports)+sum(x['checks'] for x in boundaries)+backend_report['checks'],'sourceBeforeAfterMatch':True,'syntheticOnly':True,'consoleContacted':False,'networkContacted':False,'inputPins':before,'observations':reports,'boundaryModels':boundaries,'backendSerializer':backend_report,'backendSerializerBodyUnchanged':True,'defaultConsoleOperationsAreAbortingLinkSpies':True}
(OUT/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
work.cleanup()
print(json.dumps({k:v for k,v in proof.items() if k not in ('inputPins','observations','backendSerializer')}))
