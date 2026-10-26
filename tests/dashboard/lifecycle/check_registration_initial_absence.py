#!/usr/bin/env python3
"""Actual production initial ownership/registration lifecycle, offline only."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LIB = ROOT / 'src/blackwire/library'
STREAM = ROOT / 'src/blackwire/streamreader'


def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        raise ValueError('Use a fresh output below this repository build directory')
    out.mkdir(parents=True, exist_ok=False)
    from fixture_sdk import production_rows
    own = [HERE / n for n in ('test_registration_initial_absence.c', 'test_registration_cleanup.c',
                              'test_registration_cleanup_io.h', 'fixture_sdk.py')]
    core = [LIB / n for n in ('native_registration_lifecycle.c', 'native_registration_cleanup.c',
        'native_cleanup_remote.c', 'native_registration.c', 'registration_db.c', 'source_param.c',
        'reader_selection.c', 'native_prepared_target.c', 'register_job.c')]
    core += [STREAM / 'manifest.c', own[0]]
    core += [STREAM / n for n in ('container_source.c', 'pfs_source.c', 'pfsc_range.c', 'pfsc_inflate.c')]
    sqlite = ROOT / 'vendor/sqlite/sqlite3.c'
    possible = set(core + own + [Path(__file__), sqlite, ROOT / 'tools/build/app-recipe.json'])
    possible.update((STREAM / 'vendor/miniz').glob('*.c'))
    for base in (LIB, STREAM, ROOT / 'vendor/libsmb2/include', ROOT / 'vendor/sqlite',
                 ROOT / 'src/blackwire/tools/compressed_stream_candidate'):
        possible.update(base.rglob('*.h'))
    before = {path.resolve(): pin(path) for path in possible}
    commands, dependencies = [], []

    def run(label, argv):
        r = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
                     UBSAN_OPTIONS='halt_on_error=1'))
        commands.append({'label': label, 'argv': list(map(str, argv)), 'exitCode': r.returncode,
                         'stdout': r.stdout, 'stderr': r.stderr})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if r.returncode:
            raise RuntimeError(label + ' failed; evidence retained')
        return r.stdout

    includes = [LIB, STREAM, HERE, ROOT / 'vendor/sqlite', ROOT / 'vendor/libsmb2/include',
                ROOT / 'src/blackwire/tools/compressed_stream_candidate']
    host = ['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
            '-Wno-unused-function', '-Wno-unused-variable', '-O1', '-g', '-fsanitize=address,undefined',
            '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections',
            '-DBW_COPY_HOST_TEST=1', *[str(v) for d in includes for v in ('-I', d)]]
    objects = []
    for n, path in enumerate(core):
        obj = out / (str(n) + '-' + path.stem + '.o')
        dep = obj.with_suffix('.d'); dependencies.append(dep)
        extra = ['-include', HERE / 'test_registration_cleanup_io.h'] if path.name in {
            'native_registration_cleanup.c', 'native_registration_lifecycle.c', 'native_cleanup_remote.c'} else []
        run('host-' + path.stem, [*host, *extra, '-MD', '-MF', dep, '-c', path, '-o', obj])
        objects.append(obj)
    sqlite_obj = out / 'sqlite.o'
    run('source-sqlite', ['clang-18', '-std=gnu11', '-O0', '-g', '-DSQLITE_THREADSAFE=1',
        '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_TEMP_STORE=3', '-c', sqlite, '-o', sqlite_obj])
    binary = out / 'test-initial-absence'
    run('host-link', ['clang-18', '-fsanitize=address,undefined', *objects, sqlite_obj,
                     '-Wl,--gc-sections', '-pthread', '-lm', '-o', binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-initial-absence-') as tmp:
        result = json.loads(run('actual-fs-sqlite-normal-registration', [binary, Path(tmp) / 'fixtures']))
    sdk_objects = {}
    templates = production_rows(ROOT, out / 'sdk-template')
    for name in ('native_registration_cleanup.c', 'native_registration_lifecycle.c'):
        template = next(row for row in templates if '-c' in row['argv'] and
                        Path(row['argv'][row['argv'].index('-c') + 1]).name == name)
        argv = template['argv'].copy()
        sdk = out / (Path(name).stem + '.sdk.o'); dep = sdk.with_suffix('.d')
        argv[argv.index('-o') + 1] = str(sdk)
        argv[argv.index('-MF') + 1] = str(dep); dependencies.append(dep)
        run('production-sdk-' + Path(name).stem, argv); sdk_objects[name] = pin(sdk)
    actual = set(core + own + [Path(__file__), sqlite, ROOT / 'tools/build/app-recipe.json'])
    for dep in dependencies:
        for name in shlex.split(dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split('\n\n', 1)[0]):
            path = (Path(name) if Path(name).is_absolute() else ROOT / name).resolve()
            if path.is_relative_to(ROOT) and not path.is_relative_to(out):
                actual.add(path)
    for path in actual:
        if path.resolve() not in before or before[path.resolve()] != pin(path):
            raise ValueError('Actual source input changed or was unpinned')
    proof = {'passed': True, 'sourceBeforeAfterMatch': True, 'filesystem': result,
        'source': {p.relative_to(ROOT).as_posix(): pin(p) for p in sorted(actual)},
        'productionSdkObjects': sdk_objects, 'sdkUnits': 2, 'consoleActions': 0, 'fullBuilds': 0,
        'nativeRuntimeExecuted': False, 'normalTransportAndReleaseModeled': True}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'passed': True, 'filesystem': result, 'sdkUnits': 2, 'proof': pin(out / 'proof.json')}))


if __name__ == '__main__':
    main()
