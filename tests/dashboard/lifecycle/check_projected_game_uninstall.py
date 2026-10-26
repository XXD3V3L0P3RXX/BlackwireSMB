#!/usr/bin/env python3
"""Actual projected uninstall inventory/deletion helpers; no console or SMB."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LIB = ROOT / 'src/blackwire/library'
STREAM = ROOT / 'src/blackwire/streamreader'


def pin(path):
    body = path.read_bytes()
    return {'bytes': len(body), 'sha256': hashlib.sha256(body).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-only', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh absent output below build/')
    out.mkdir(parents=True)
    sources = [HERE / 'test_projected_game_uninstall.c', LIB / 'source_companions.c',
               LIB / 'source_plan.c', LIB / 'registration_db.c', STREAM / 'manifest.c']
    flags = ['-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-Wno-unused-variable', '-Wno-misleading-indentation',
             '-ffunction-sections', '-fdata-sections', '-DBW_COPY_HOST_TEST=',
             '-DBW_NATIVE_SEND_HOST_TEST=1', '-DBW_NATIVE_FS_HOST_TEST=1', '-DBW_NATIVE_PRELOAD=1',
             '-I', LIB, '-I', STREAM, '-I', ROOT / 'vendor/libsmb2/include',
             '-I', ROOT / 'vendor/sqlite', '-I', ROOT / 'src/blackwire/tools/compressed_stream_candidate']
    commands = []

    def run(label, argv):
        argv = list(map(str, argv))
        r = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        commands.append({'label': label, 'argv': argv, 'exitCode': r.returncode})
        (out / (label + '.log')).write_text(r.stdout + r.stderr)
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if r.returncode or 'ERROR: AddressSanitizer' in r.stderr or 'runtime error:' in r.stderr:
            raise RuntimeError(label + ' failed; see retained log')
        return r.stdout

    used = {Path(__file__).resolve(), *sources, ROOT / 'vendor/sqlite/sqlite3.c', ROOT / 'vendor/sqlite/sqlite3.h'}
    for i, source in enumerate(sources):
        dep = run('dependencies-' + str(i), ['clang-18', *flags, '-MM', source])
        for name in shlex.split(dep.replace('\\\n', ' ').split(':', 1)[1]):
            p = Path(name)
            used.add((p if p.is_absolute() else ROOT / p).resolve())
    before = {str(p.relative_to(ROOT)): pin(p) for p in sorted(used)}
    binary = out / 'projected-game-uninstall'
    sql = out / 'sqlite.o'
    run('sqlite-compile', ['clang-18', '-O0', '-std=gnu11', '-DSQLITE_THREADSAFE=1',
        '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_TEMP_STORE=3', '-c', ROOT / 'vendor/sqlite/sqlite3.c', '-o', sql])
    run('host-build', ['clang-18', *flags, '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', *sources, sql,
        '-Wl,--gc-sections', '-pthread', '-o', binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-projected-uninstall-') as tmp:
        result = json.loads(run('actual-inventory-delete-cases', [binary, tmp]).splitlines()[-1])
    sdk = out / 'native_game_uninstall.sdk.o'
    if not args.host_only:
        sdk_flags = [x for x in flags if not str(x).startswith('-DBW_COPY_HOST_TEST') and
                     not str(x).startswith('-DBW_NATIVE_SEND_HOST_TEST') and
                     not str(x).startswith('-DBW_NATIVE_FS_HOST_TEST')]
        run('sdk-compile', ['/opt/ps5-payload-sdk/bin/prospero-clang', *sdk_flags,
                           '-fPIC', '-c', LIB / 'native_game_uninstall.c', '-o', sdk])
    after = {str(p.relative_to(ROOT)): pin(p) for p in sorted(used)}
    if before != after:
        raise ValueError('Actual source or dependency changed during verification')
    proof = {'passed': True, 'host': result, 'source': before, 'sourceBeforeAfterMatch': True,
        'sdkUnits': 0 if args.host_only else 1, 'sdkObject': None if args.host_only else pin(sdk),
        'limits': ['Explicit synthetic parent receipts; normal Send/unregister API is not qualified',
                   'No actual console, SMB, release image or firmware behavior is exercised'],
        'consoleActions': 0, 'networkActions': 0, 'fullBuilds': 0}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'passed': True, 'host': result, 'sdkUnits': proof['sdkUnits']}))


if __name__ == '__main__':
    main()
