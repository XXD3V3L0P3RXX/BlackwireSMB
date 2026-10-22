#!/usr/bin/env python3
"""Real private Removed/Restore filesystem faults and strict SDK TU; no console."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
from fixture_sdk import production_rows
ROOT = Path(__file__).resolve().parents[3]
LIB = ROOT / 'src/blackwire/library'
STREAM = ROOT / 'src/blackwire/streamreader'

def pin(path):
    b = path.read_bytes()
    return dict(bytes=len(b), sha256=hashlib.sha256(b).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build/native-dashboard'):
        raise ValueError('Use a new isolated native-dashboard output')
    out.mkdir(parents=True, exist_ok=False)
    owned = [LIB / 'native_removed_sources.c', LIB / 'native_removed_sources.h',
             HERE/'test_removed_sources.c',
             HERE/'test_removed_sources_io.h', Path(__file__).resolve()]
    core = [owned[0], STREAM / 'manifest.c', owned[2]]
    possible = set(owned + core) | set(LIB.glob('*.h')) | set(STREAM.rglob('*.h'))
    before = {p.resolve(): pin(p) for p in possible}
    commands, deps = [], []
    def run(label, argv):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=150,
            env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        commands.append(dict(label=label, argv=argv, exitCode=p.returncode, stdout=p.stdout, stderr=p.stderr))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if p.returncode:
            raise RuntimeError(label + ' failed; output retained')
        return p.stdout
    common = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
              '-Wno-unused-function', '-Wno-unused-variable', '-g', '-I', LIB, '-I', STREAM]
    host = ['clang-18', *common, '-O1', '-DBW_COPY_HOST_TEST=1', '-fsanitize=address,undefined',
            '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    objects = []
    for n, source in enumerate(core):
        obj = out / f'host-{n}.o'
        dep = obj.with_suffix('.d'); deps.append(dep)
        extra = ['-include', owned[3]] if source == owned[0] else []
        run('host-' + source.stem, [*host, *extra, '-MD', '-MF', dep, '-c', source, '-o', obj])
        objects.append(obj)
    binary = out / 'removed-sources-host'
    run('host-link', ['clang-18', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *objects, '-pthread', '-o', binary])
    with tempfile.TemporaryDirectory(prefix='blackwire-removed-sources-') as tmp:
        observed = json.loads(run('actual-filesystem-faults', [binary, Path(tmp) / 'cases']))
    if not observed['passed'] or observed['moduleOpens'] != observed['moduleCloses']:
        raise RuntimeError('Incomplete descriptor/source preservation proof')
    sdk = Path('/opt/ps5-payload-sdk')
    native = out / 'native_removed_sources.sdk.o'
    dep = native.with_suffix('.d'); deps.append(dep)
    run('sdk-removed-sources', [sdk / 'bin/prospero-clang', *common, '-MD', '-MF', dep, '-c', owned[0], '-o', native])
    actual = set(owned + core)
    for dep in deps:
        for name in shlex.split(dep.read_text().replace('\\\n', ' ').split(':', 1)[1]):
            actual.add((ROOT / name).resolve())
    local = {p for p in actual if p.is_relative_to(ROOT) and not p.is_relative_to(out)}
    for p in local:
        if p not in before or pin(p) != before[p]:
            raise RuntimeError('Actual source changed during check: ' + str(p))
    proof = dict(passed=True, atUtc=datetime.now(timezone.utc).isoformat(), host=observed,
        source={str(p.relative_to(ROOT)): before[p] for p in sorted(local)},
        sourceBeforeAfterMatch=True, hostBinary=pin(binary), sdkObject=pin(native),
        sdkInputs={str(p): pin(p) for p in sorted(actual - local) if p.is_file()},
        commands=commands, pcWrites=False, registrationActions=False, preparedDeletion=False,
        consoleActions=0, nativeRuntimeProved=False, completeUninstallImplemented=False,
        limitations=['This is durable library state, not the game-data deletion component.',
                     'Source/title absence is an explicitly supplied trusted callback; host fixtures use real isolated paths.',
                     'Native/service integration and public export are not performed.'])
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(dict(passed=True, host=observed, proof=pin(out / 'proof.json'), sdkObject=pin(native))))

if __name__ == '__main__':
    main()
