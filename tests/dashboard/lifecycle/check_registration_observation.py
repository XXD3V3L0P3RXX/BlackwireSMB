#!/usr/bin/env python3
"""Actual native registration reasons and unchanged states, offline only."""
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
    data = path.read_bytes()
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-only', action='store_true', help='Skip SDK object compilation')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        raise ValueError('Use a fresh output below this repository build directory')
    out.mkdir(parents=True, exist_ok=False)
    status = out / 'manual.status'
    status.write_bytes(b'installed\tCUSA00001\t/data/neutral-existing-cusa\tNeutral fixture\n')
    status.chmod(0o666)
    maintained = HERE / 'test_registration_lifecycle.c'
    entry = '\nint main(int argc,char **argv){'
    text = maintained.read_text()
    if text.count(entry) != 1:
        raise ValueError('Retained lifecycle fixture entry changed')
    # Reuse the exact maintained helpers and actual lifecycle inclusion, with
    # the original unrelated test main excluded. Never replace state/DB code.
    prefix = text.split(entry, 1)[0] + '\n'
    (out / 'registration_observation_fixture.h').write_text(prefix)
    own = [HERE / name for name in ('test_registration_observation.c',
        'test_registration_lifecycle.c', 'test_registration_cleanup.c',
        'test_registration_cleanup_io.h', 'fixture_sdk.py')]
    core = [LIB / name for name in ('native_registration_cleanup.c', 'native_cleanup_remote.c',
        'native_registration.c', 'registration_db.c', 'source_param.c', 'reader_selection.c',
        'native_prepared_target.c')]
    core += [STREAM / 'manifest.c', HERE / 'legacy_projection_boundary.c', own[0]]
    core += [STREAM / name for name in ('container_source.c', 'pfs_source.c',
        'pfsc_range.c', 'pfsc_inflate.c')]
    sqlite = ROOT / 'vendor/sqlite/sqlite3.c'
    possible = set(core + own + [Path(__file__), sqlite, LIB / 'native_registration_lifecycle.c',
        ROOT / 'tools/build/app-recipe.json'])
    possible.update((LIB).glob('*.h'))
    possible.update(STREAM.rglob('*.h'))
    possible.update((STREAM / 'vendor/miniz').glob('*.c'))
    for base in (ROOT / 'vendor/libsmb2/include', ROOT / 'vendor/sqlite',
                 ROOT / 'src/blackwire/tools/compressed_stream_candidate'):
        possible.update(base.rglob('*.h'))
    before = {p.resolve(): pin(p) for p in possible}
    commands, dependencies = [], []

    def run(label, argv):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
        commands.append(dict(label=label, argv=argv, exitCode=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(label + ' failed; evidence retained')
        return result.stdout

    includes = [LIB, STREAM, STREAM / 'diagnostics', HERE, out, ROOT / 'vendor/sqlite',
        ROOT / 'vendor/libsmb2/include', ROOT / 'src/blackwire/tools/compressed_stream_candidate']
    flags = ['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Wno-misleading-indentation', '-Wno-unused-function', '-Wno-unused-variable',
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections',
        '-DBW_COPY_HOST_TEST=1',
        '-DBW_NRC_HOST_MANUAL_STATUS_PATH=' + json.dumps(str(status)),
        *[str(x) for directory in includes for x in ('-I', directory)]]
    objects = []
    for index, source in enumerate(core):
        obj = out / f'{index}-{source.stem}.o'
        dep = obj.with_suffix('.d')
        dependencies.append(dep)
        extra = []
        if source.name in {'native_registration_cleanup.c', 'native_cleanup_remote.c'}:
            extra += ['-include', HERE / 'test_registration_cleanup_io.h']
        if source.name == 'test_registration_observation.c':
            extra += ['-DBW_NRL_UNINSTALL_BOUNDARY_TEST=1']
        if source.name == 'native_registration.c':
            extra += ['-Dopen=registration_observation_open']
        if source.name == 'registration_db.c':
            extra += ['-Dsqlite3_close=registration_observation_db_close']
        run('host-' + source.stem, [*flags, *extra, '-MD', '-MF', dep, '-c', source, '-o', obj])
        objects.append(obj)
    sqlite_obj = out / 'sqlite.o'
    dep = sqlite_obj.with_suffix('.d')
    dependencies.append(dep)
    run('source-sqlite', ['clang-18', '-std=gnu11', '-O0', '-g', '-DSQLITE_THREADSAFE=1',
        '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_TEMP_STORE=3', '-MD', '-MF', dep,
        '-c', sqlite, '-o', sqlite_obj])
    binary = out / 'test-registration-observation'
    run('host-link', ['clang-18', '-fsanitize=address,undefined', *objects, sqlite_obj,
        '-Wl,--gc-sections', '-pthread', '-lm', '-o', binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-registration-observation-') as temporary:
        result = json.loads(run('actual-native-fs-sqlite-observation', [binary, Path(temporary) / 'fixtures']))
    sdk_objects = {}
    if not args.host_only:
        from fixture_sdk import production_rows
        templates = production_rows(ROOT, out / 'sdk-template')
        template = next(row['argv'] for row in templates if '-c' in row['argv'] and
                        Path(row['argv'][row['argv'].index('-c') + 1]).name == 'native_register.c')
        for name in ('native_registration.c', 'registration_db.c', 'native_registration_lifecycle.c'):
            argv = template.copy()
            obj = out / (Path(name).stem + '.sdk.o')
            dep = obj.with_suffix('.d')
            dependencies.append(dep)
            argv[argv.index('-c') + 1] = str(LIB / name)
            argv[argv.index('-o') + 1] = str(obj)
            argv[argv.index('-MF') + 1] = str(dep)
            run('production-sdk-' + Path(name).stem, argv)
            sdk_objects[name] = pin(obj)
    actual = set(core + own + [Path(__file__), sqlite, LIB / 'native_registration_lifecycle.c',
        ROOT / 'tools/build/app-recipe.json'])
    for dep in dependencies:
        for name in shlex.split(dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split('\n\n', 1)[0]):
            path = (Path(name) if Path(name).is_absolute() else ROOT / name).resolve()
            if path.is_relative_to(ROOT) and not path.is_relative_to(out):
                actual.add(path)
    for path in actual:
        if path.resolve() not in before or before[path.resolve()] != pin(path):
            raise ValueError('Actual source input changed or was unpinned: ' + str(path))
    if (out / 'registration_observation_fixture.h').read_text() != prefix:
        raise ValueError('Exact retained fixture prefix changed')
    proof = dict(passed=True, sourceBeforeAfterMatch=True, filesystem=result,
        retainedLifecycleFixturePrefixExact=True, source={p.relative_to(ROOT).as_posix(): pin(p) for p in sorted(actual)},
        productionSdkObjects=sdk_objects, sdkUnits=len(sdk_objects), hostOnly=args.host_only,
        consoleActions=0, fullBuilds=0, nativeRuntimeExecuted=False, normalSmpTransportModeled=True,
        injectedFaults=['native open EACCES', 'actual sqlite close then reported IOERR'])
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(dict(passed=True, filesystem=result, sdkUnits=len(sdk_objects), proof=pin(out / 'proof.json'))))


if __name__ == '__main__':
    main()
