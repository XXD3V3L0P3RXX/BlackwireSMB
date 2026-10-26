#!/usr/bin/env python3
"""Qualify the pure source-companion projection codec, with no console I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LIB = ROOT / 'src/blackwire/library'
STREAM = ROOT / 'src/blackwire/streamreader'


def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--host-only', action='store_true')
    parser.add_argument('--sdk', type=Path, default=Path('/opt/ps5-payload-sdk'))
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build' or out.exists():
        raise ValueError('Use a fresh output below this repository build directory')
    out.mkdir(parents=True)
    commands = []

    def run(label, argv):
        result = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                     ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'), timeout=180)
        commands.append({'label': label, 'argv': list(map(str, argv)), 'exitCode': result.returncode})
        (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode or b'runtime error:' in result.stderr or b'ERROR: AddressSanitizer' in result.stderr:
            raise RuntimeError(label + ' failed; evidence retained')
        return result.stdout

    common = ['-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
              '-Wno-misleading-indentation', '-Wno-unused-function', '-I', LIB]
    sources = [HERE / 'test_source_companions.c', LIB / 'source_companions.c',
               LIB / 'source_plan.c', STREAM / 'manifest.c']
    # Collect the actual transitive local headers before compilation. Avoid
    # pinning unrelated, concurrently developed consumers as codec evidence.
    actual = {Path(__file__).resolve(), *sources}
    for index, source in enumerate(sources):
        raw = run('dependencies-' + str(index), ['clang-18', *common, '-MM', source])
        for item in shlex.split(raw.decode().replace('\\\n', ' ').split(':', 1)[1]):
            path = Path(item)
            actual.add((path if path.is_absolute() else ROOT / path).resolve())
    before = {str(p.relative_to(ROOT)): pin(p) for p in sorted(actual)}
    binary = out / 'source-companions-host'
    sanitizer = ['-O1', '-g', '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    run('host-build', ['clang-18', *common, *sanitizer, *sources,
        '-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-o', binary])
    tested = json.loads(run('actual-codec-faults', [binary]))
    sdk_objects = {}
    if not args.host_only:
        for source in sources[1:]:
            target = out / (source.stem + '.sdk.o')
            run('sdk-' + source.stem, [args.sdk / 'bin/prospero-clang', *common,
                '-fPIC', '-c', source, '-o', target])
            sdk_objects[target.name] = pin(target)
    after = {str(p.relative_to(ROOT)): pin(p) for p in sorted(actual)}
    if before != after:
        raise ValueError('Actual codec or dependency source changed during check')
    proof = {'passed': True, 'host': tested, 'source': before,
        'sourceBeforeAfterMatch': True, 'sdkObjects': sdk_objects, 'commands': commands,
        'fixtureScope': 'Actual codec and explicit source planner with deterministic, non-executable original-source bytes; malformed negative fixtures intentionally alter tables.',
        'limits': ['Does not prove provider freshness or filesystem ownership',
                   'Does not qualify Send, runtime reader admission, bundle install, refresh or uninstall consumers',
                   'No complete ELF or live console is built or executed'],
        'consoleActions': 0, 'networkActions': 0, 'deployed': False}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'passed': True, 'host': tested, 'sdkUnits': len(sdk_objects), 'consoleActions': 0}))


if __name__ == '__main__':
    main()
