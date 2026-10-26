#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private catalog/container first-failure provenance; no console/network I/O."""
from pathlib import Path
import argparse, hashlib, json, os, re, struct, subprocess, sys, tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tests/compressed'))
from fixtures import make_pfs, pfsc

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
OUT = parser.parse_args().output.resolve()
if not OUT.is_relative_to(ROOT / 'build') or OUT.exists():
    raise ValueError('Use a fresh output below build/')
OUT.mkdir(parents=True)
names = ('library/source_catalog.c', 'library/source_param.c', 'library/source_plan.c',
         'library/source_profile.c', 'library/source_binding.c', 'streamreader/manifest.c',
         'streamreader/container_source.c', 'streamreader/container_smb.c',
         'streamreader/pfs_source.c', 'streamreader/pfsc_range.c', 'streamreader/pfsc_inflate.c')
sources = [ROOT / 'src/blackwire' / name for name in names] + [HERE / 'test_catalog_image_failure.c']
tracked = sources + [Path(__file__), ROOT / 'tests/compressed/fixtures.py']
tracked += list((ROOT / 'src/blackwire').rglob('*.h'))
tracked += list((ROOT / 'vendor/libsmb2/include').rglob('*.h'))
tracked += list((ROOT / 'src/blackwire/streamreader/vendor/miniz').glob('*.c'))
pin = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p): pin(p) for p in tracked}
commands = []

def run(argv):
    p = subprocess.run(list(map(str, argv)), capture_output=True, text=True, errors='replace', timeout=90,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1',
                            'UBSAN_OPTIONS': 'halt_on_error=1'})
    commands.append({'argv': list(map(str, argv)), 'exitCode': p.returncode,
                     'stdout': p.stdout, 'stderr': p.stderr})
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if p.returncode:
        raise RuntimeError(p.stderr or p.stdout)
    return p.stdout

with tempfile.TemporaryDirectory(prefix='blackwire-image-failure-') as temp:
    work = Path(temp)
    image = work / 'Neutral.ffpfsc'
    image.write_bytes(make_pfs({'eboot.bin': b'NEUTRAL INERT STARTUP',
                               'sce_sys/param.json': b'{"titleId":"PPSA09999","titleName":"Neutral"}'})[0])
    original_image_pin = pin(image)
    malformed = work / 'Neutral-decoder.ffpfsc'
    malformed_bytes = pfsc(make_pfs({'eboot.bin': b'NEUTRAL INERT STARTUP',
                                     'sce_sys/param.json': b'{"titleId":"PPSA09999","titleName":"Neutral"}',
                                     'asset.bin': bytes(128 * 1024)})[0])
    # Valid PFSC first/last bounds; the interior block end is invalid. The
    # actual decoder reports FORMAT, which the current container layer maps
    # to fatal IO. Retain that underlying distinction in the private row.
    struct.pack_into('<Q', malformed_bytes, 0x408, 65536 + 65537)
    malformed.write_bytes(malformed_bytes)
    malformed_pin = pin(malformed)
    log = work / 'send.log'
    binaries = {}
    flags = ['clang-18', '-std=gnu11', '-D_DEFAULT_SOURCE=1', '-O1', '-g', '-Wall', '-Wextra',
             '-Werror', '-Wno-misleading-indentation', '-Wno-unused-function', '-ffunction-sections',
             '-fdata-sections', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-fno-omit-frame-pointer', '-I', ROOT / 'src/blackwire', '-I', ROOT / 'vendor/libsmb2/include']
    for diagnostic in (False, True):
        binary = OUT / ('diagnostic-test' if diagnostic else 'normal-test')
        run([*flags, f'-DBW_SEND_DIAGNOSTIC={int(diagnostic)}',
             f'-DBW_SEND_LOG_PATH="{log}"', *sources, '-Wl,--gc-sections', '-o', binary])
        binaries[diagnostic] = binary
    # The build-directory name appears in DWARF; inspect runtime constants,
    # not debug-path text that happens to contain the event name.
    normal_rodata = run(['readelf', '--string-dump=.rodata', binaries[False]])
    for private in ('container-image-failure', 'catalog-image-failure',
                    'heldDifference=', 'connectionPresent=%u containerKind=', 'diagnostic-inert-probe'):
        assert private not in normal_rodata, private
    # Transport failures drain/clear the shared context; generation mismatch,
    # zero/max-read refusal and the deadline preserve the original behavior.
    cases = [('valid', None, 'validated', False), ('short-read', None, 'validated', False),
             ('admit-stat', 'admit-stat', 'container_open', True),
             ('admit-metadata', 'admit-metadata', 'container_open', False),
             ('open-readonly', 'open-readonly', 'container_open', True),
             ('generation-fstat', 'generation-fstat', 'container_open', True),
             ('generation-stat', 'generation-stat', 'container_open', True),
             ('pread-error', 'pread-return', 'container_open', True),
             ('pread-eof', 'pread-return', 'container_open', False),
             ('pread-eof-close', 'pread-return', 'container_open', True),
             ('pread-oversize', 'pread-return', 'container_open', True),
             ('pread-max-read', 'pread-max-read', 'container_open', False),
             ('close', 'close', 'validated', True),
             ('decoder-format', None, 'container_open', False),
             ('deadline', None, 'namespace', False)]
    fields = ('type', 'inode', 'size', 'mtime_seconds', 'mtime_nanoseconds', 'ctime_seconds', 'ctime_nanoseconds')
    for side in ('held', 'named'):
        cases += [(f'{side}-{field}', f'generation-{side}-mismatch', 'container_open', False) for field in fields]
    reports = []
    for case, operation, stage, cleared in cases:
        pair = []
        for diagnostic in (False, True):
            if log.exists():
                log.unlink()
            value = json.loads(run([binaries[diagnostic], malformed if case == 'decoder-format' else image, case]))
            assert value['stage'] == stage and value['ownerPresentBeforeCleanup'] == (not cleared), (case, value)
            lines = log.read_text().splitlines() if log.exists() else []
            low = [line for line in lines if 'event=container-image-failure ' in line]
            high = [line for line in lines if 'event=catalog-image-failure ' in line]
            if not diagnostic:
                assert not lines and not log.exists(), (case, lines)
            else:
                assert len(low) == int(operation is not None), (case, low)
                assert len(high) == int(case not in ('valid', 'short-read')), (case, high)
                if low:
                    assert f'operation={operation} ' in low[0] and 'path=Neutral.ffpfsc ' in low[0], (case, low)
                    if case.startswith(('held-', 'named-')):
                        side, field = case.split('-', 1)
                        assert f'{side}Difference={field}' in low[0], (case, low)
                        inode = re.search(r'admitted=\{type:\d+,ino:(\d+),', low[0])
                        assert inode and int(inode[1]) > 2**53, (case, low)
                    if case == 'pread-error':
                        assert 'raw=-110 ' in low[0] and 'requestedBytes=48 ' in low[0], low
                    if case == 'pread-eof':
                        assert 'raw=0 ' in low[0], low
                if high:
                    assert 'path=Neutral.ffpfsc ' in high[0] and f'stage={stage} ' in high[0] and 'deadline=' in high[0], high
                    if case == 'deadline':
                        assert 'containerError=source_generation ' in high[0] and 'deadline=0.000000' in high[0], high
                    if case == 'decoder-format':
                        assert 'containerError=io_failure ' in high[0] and 'decoderResult=-2 ' in high[0] and 'transportResult=0 ' in high[0], high
                assert len([line for line in lines if 'event=diagnostic-inert-probe ' in line]) == 1
            pair.append(value)
            reports.append({'case': case, 'diagnostic': diagnostic, 'result': value, 'privateLog': lines})
        # Instrumentation must not change transport/ownership/public results.
        assert pair[0] == pair[1], (case, pair)
        assert pin(image) == original_image_pin
        assert pin(malformed) == malformed_pin
    after = {str(p): pin(p) for p in tracked}
    proof = {'passed': before == after, 'cases': len(cases) * 2,
             'checks': sum(row['result']['checks'] for row in reports),
             'actualCatalogContainerAndDecoder': True, 'readOnlyLocalSmbAdapter': True,
             'normalModeNoPrivateMarkersOrFile': True, 'samePublicResultsAndOwnership': True,
             'sourceFixtureUnchanged': True, 'consoleActions': 0,
             'sourcePinsUnchanged': before == after, 'sourceBefore': before, 'sourceAfter': after,
             'reports': reports}
    (OUT / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    assert proof['passed']
    print(json.dumps({key: proof[key] for key in ('passed', 'cases', 'checks', 'sourcePinsUnchanged', 'normalModeNoPrivateMarkersOrFile')}))
