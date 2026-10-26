#!/usr/bin/env python3
"""Actual projected producer/removal chain using a local raw-SMB service model."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = ROOT / 'src/blackwire'
sys.path.insert(0, str(ROOT / 'tests/compressed'))
from fixtures import make_pfs, pfsc
from fixtures_exfat import make_exfat


def pin(path):
    body = path.read_bytes()
    return {'bytes': len(body), 'sha256': hashlib.sha256(body).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--registration', action='store_true', help='Qualify projected ordinary registration ledger and offline retirement')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh absent output below build/')
    out.mkdir(parents=True)
    names = ['library/source_namespace.c', 'library/source_companions.c', 'library/source_provider.c',
        'library/source_prepare.c', 'library/source_smb.c', 'library/source_directory.c', 'library/source_snapshot.c',
        'library/compressed_data.c', 'library/source_plan.c', 'library/source_profile.c', 'library/source_binding.c',
        'library/source_identity.c', 'library/source_param.c', 'library/reader_selection.c',
        'library/native_game_uninstall.c', 'library/native_removed_sources.c', 'library/registration_db.c',
        'library/preload_bundle.c', 'streamreader/container_source.c', 'streamreader/container_manifest.c',
        'streamreader/pfs_source.c', 'streamreader/pfsc_range.c', 'streamreader/pfsc_inflate.c',
        'streamreader/manifest.c', 'streamreader/share_config.c']
    if args.registration:
        names += ['library/native_registration_cleanup.c', 'library/native_registration.c', 'library/native_prepared_target.c']
    test = HERE / ('test_projected_registration.c' if args.registration else 'test_projected_game_uninstall_e2e.c')
    sources = [test, *[SRC / name for name in names]]
    flags = ['-D_DEFAULT_SOURCE', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-Wno-misleading-indentation', '-Wno-unused-function', '-Wno-unused-variable',
        '-ffunction-sections', '-fdata-sections', '-DBW_COPY_HOST_TEST=', '-DBW_NATIVE_SEND_HOST_TEST=1',
        '-DBW_NATIVE_FS_HOST_TEST=1', '-DBW_NATIVE_PRELOAD=1', '-DBW_PRELOAD_BUNDLE_HOST_TEST=1',
        '-I' + str(SRC / 'library'), '-I' + str(SRC), '-I' + str(ROOT / 'vendor/libsmb2/include'),
        '-I' + str(ROOT / 'vendor/sqlite')]
    commands = []

    def run(label, argv):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        commands.append({'label': label, 'argv': argv, 'exitCode': p.returncode})
        (out / (label + '.log')).write_text(p.stdout + p.stderr)
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if p.returncode or 'ERROR: AddressSanitizer' in p.stderr or 'runtime error:' in p.stderr:
            raise RuntimeError(label + ' failed; see retained log')
        return p.stdout

    image = out / 'image.c'
    body = b'CURRENT NONEXECUTABLE READER FIXTURE' * 64
    image.write_text('#include "preload_bundle.h"\nstatic const uint8_t bytes[]={' +
        ','.join(map(str, body)) + '};\nconst struct bw_preload_image bw_preload_embedded_image={bytes,sizeof bytes,{' +
        ','.join(map(str, hashlib.sha256(body).digest())) + '},"' + '6' * 64 + '",1000};\n')
    used = {Path(__file__).resolve(), *sources, ROOT / 'vendor/sqlite/sqlite3.c', ROOT / 'vendor/sqlite/sqlite3.h',
            ROOT / 'tests/compressed/fixtures.py', ROOT / 'tests/compressed/fixtures_exfat.py'}
    for i, source in enumerate(sources):
        dep = run('dependencies-' + str(i), ['clang-18', *flags, '-MM', source])
        for name in shlex.split(dep.replace('\\\n', ' ').split(':', 1)[1]):
            p = Path(name)
            used.add((p if p.is_absolute() else ROOT / p).resolve())
    before = {str(p.relative_to(ROOT)): pin(p) for p in sorted(used)}
    sql = out / 'sqlite.o'
    run('sqlite-build', ['clang-18', '-std=gnu11', '-O0', '-DSQLITE_THREADSAFE=1',
        '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_TEMP_STORE=3', '-c', ROOT / 'vendor/sqlite/sqlite3.c', '-o', sql])
    binary = out / 'projected-uninstall-e2e'
    run('host-build', ['clang-18', *flags, '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer', *sources, image, sql, *(['-Wl,--wrap=close'] if args.registration else []), '-Wl,--gc-sections', '-pthread', '-lm', '-o', binary])
    base = {'eboot.bin': b'BLACKWIRE NONEXECUTABLE FIXTURE',
            'sce_sys/param.json': b'{"titleId":"PPSA99001","contentId":"UP0000-PPSA99001_00-BLACKWIREFIXTURE0"}',
            'assets/stream.pak': b'asset' * 200}
    cases = [('f1', {**{f'fakelib/module{i}.so': b'unknown source module' for i in range(8)},
                     'fakelib/libSceAmpr.sprx': b'ORIGINAL UNEXECUTED AMPR', 'fakelib/README.txt': b'metadata'}, ()),
             ('f2', {**{f'fakelib2/module{i}.so': b'unknown source module' for i in range(8)},
                     'fakelib2/libSceAmpr.sprx': b'ORIGINAL UNEXECUTED AMPR', 'fakelib2/README.txt': b'metadata'}, ()),
             ('empty-f2', {'fakelib/README.txt': b'nonpreferred'}, ('fakelib2',))]
    results = []
    with tempfile.TemporaryDirectory(prefix='blackwire-project-remove-e2e-') as temporary:
        temp = Path(temporary)
        for label, extra, dirs in cases:
            parent = temp / (label + '-source')
            parent.mkdir()
            game = parent / 'fixture'
            game.mkdir()
            files = {**base, **extra}
            for directory in dirs:
                (game / directory).mkdir(parents=True, exist_ok=True)
            for name, data in files.items():
                p = game / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            images = [('folder', None), ('pfs', make_pfs(files, directories=dirs)[0]),
                      ('pfsc', pfsc(make_pfs(files, directories=dirs)[0])),
                      ('exfat', make_exfat(files, directories=dirs)[0])]
            if args.registration:
                images = images[:1]
            for kind, image_body in images:
                source_image = temp / (label + '-' + kind + '.image')
                if image_body is not None:
                    source_image.write_bytes(image_body)
                modes = ['normal', 'absent-reader']
                if args.registration:
                    modes = ['normal', 'no-source', 'no-startup', 'control-changed', 'receipt-before-intent',
                             'receipt-before-commit', 'receipt-before-retire', 'lost-uninstall', 'retire-foreign', 'retire-close-unknown']
                elif label == 'f2':
                    modes += ['receipt-tamper', 'original-tamper', 'extra-legacy', 'transient-lease']
                for mode in modes:
                    console = temp / (label + '-' + kind + '-' + mode)
                    console.mkdir()
                    result = json.loads(run(label + '-' + kind + '-' + mode,
                        [binary, 'folder' if kind == 'folder' else 'image', parent, source_image, console, mode]).splitlines()[-1])
                    results.append({'case': label, 'format': kind, 'mode': mode, **result})
                    if image_body is not None and source_image.read_bytes() != image_body:
                        raise ValueError('Original source image changed')
                    if any((game / name).read_bytes() != data for name, data in files.items()):
                        raise ValueError('Original source folder changed')
    after = {str(p.relative_to(ROOT)): pin(p) for p in sorted(used)}
    if before != after:
        raise ValueError('Actual producer/removal source changed during qualification')
    proof = {'passed': True, 'cases': len(results), 'checks': sum(r['checks'] for r in results),
        'source': before, 'sourceBeforeAfterMatch': True, 'results': results,
        'actualSendRecordAndIndex': True, 'actualRemovalWitnessAndReaderSelection': not args.registration,
        'actualNormalUninstall': not args.registration, 'actualOrdinaryRegistrationLedger': args.registration,
        'postPreparationSourceCalls': 0 if not args.registration else 'one explicit execution-source qualification, no offline retirement source calls',
        'sourceAndSavesPreserved': not args.registration,
        'normalExclusionCallbacksModeled': True, 'consoleActions': 0, 'networkActions': 0,
        'limits': ['No actual Sony unregister transport, process list or scan-root service is exercised',
                   'Synthetic non-executable source/current-reader bytes; no hardware compatibility claim',
                   'Registration variant qualifies folder source admission only; image registration source admission needs separate coverage']}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({k: proof[k] for k in ('passed', 'cases', 'checks', 'consoleActions')}))


if __name__ == '__main__':
    main()
