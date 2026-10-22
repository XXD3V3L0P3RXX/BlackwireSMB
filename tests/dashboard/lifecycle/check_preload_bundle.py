#!/usr/bin/env python3
"""Actual-C bundle diagnostics using explicit qualified companion inputs, no network."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = Path(__file__).resolve().parents[3]
NAMES = ('libSceAmpr.sprx', 'libSceAppContent.sprx', 'libSceGameUpdate.sprx',
         'libSceNpEntitlementAccess.sprx', 'libSceAmpr.sprx.orig')


def pin(p):
    raw = Path(p).read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--libraries', type=Path, required=True)
    parser.add_argument('--library-prefix', default='')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/opt/ps5-payload-sdk'))
    a = parser.parse_args()
    if not a.library_prefix.replace('-', '').isalnum() and a.library_prefix:
        raise ValueError('Simple explicit library filename prefix required')
    out = a.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('New qualification output must be under build/')
    out.mkdir()
    fixture = Path(tempfile.mkdtemp(prefix='blackwire-bundle-diagnostics-'))
    libraries = fixture / 'globals'
    libraries.mkdir(mode=0o777)
    (fixture / 'cases').mkdir(mode=0o700)
    inputs = {}
    for name in NAMES:
        source = a.libraries / (a.library_prefix + name)
        if not source.is_file() or source.is_symlink():
            raise ValueError('Ordinary explicit companion input required')
        inputs[name] = pin(source)
        shutil.copyfile(source, libraries / name)
        (libraries / name).chmod(0o777)
    library = ROOT / 'src/blackwire/library'
    paths = [library / n for n in ('preload_bundle.c', 'preload_bundle.h')]
    paths += [ROOT / 'src/blackwire/streamreader' / n for n in ('copy_fs.h', 'copy_sha256.h')]
    paths += [Path(__file__), HERE / 'test_preload_bundle.c']
    before = {str(p.relative_to(ROOT)): pin(p) for p in paths}
    # Deliberately synthetic embedded image: the test checks material admission,
    # not an executable reader or native module behavior.
    image = bytes(i % 251 for i in range(2048))
    source = out / 'image.c'
    source.write_text('#include "preload_bundle.h"\nstatic const uint8_t data[]={'+
                      ','.join(map(str, image))+'};\nconst struct bw_preload_image bw_preload_embedded_image={data,sizeof data,{'+
                      ','.join(str(v) for v in hashlib.sha256(image).digest())+'},"'+
                      '0'*64+'",10000};\n', encoding='utf-8')
    commands = []

    def run(label, argv):
        result = subprocess.run([str(v) for v in argv], cwd=ROOT,
            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),
            capture_output=True, timeout=180)
        data = result.stdout + result.stderr
        (out / (label + '.log')).write_bytes(data)
        commands.append({'label': label, 'argv': [str(v) for v in argv], 'exitCode': result.returncode})
        if result.returncode or any(s in data for s in (b'runtime error:', b'ERROR: AddressSanitizer')):
            raise RuntimeError(label + ' failed; output retained')
        return result.stdout

    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
             '-Wno-unused-function', '-I', library]
    host = out / 'host-bundle-diagnostics'
    define = '-DBW_PRELOAD_GLOBAL_ROOT="' + str(libraries) + '"'
    run('host-build', ['clang-18', *flags, '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', define,
        HERE / 'test_preload_bundle.c', source,
        '-Wl,--wrap=close', '-Wl,--wrap=write', '-Wl,--wrap=fsync', '-o', host])
    tested = json.loads(run('actual-bundle-faults', [host, fixture / 'cases']))
    objects = []
    for name, path in [('bundle', library / 'preload_bundle.c'), ('image', source)]:
        obj = out / (name + '.o')
        run('sdk-' + name, [a.sdk / 'bin/prospero-clang', *flags, '-fPIC', '-c', path, '-o', obj])
        objects.append(obj)
    joined = out / 'bundle-joined.o'
    run('sdk-relocatable-link', [a.sdk / 'bin/ld.lld', '-r', *objects, '-o', joined])
    after = {str(p.relative_to(ROOT)): pin(p) for p in paths}
    if before != after:
        raise ValueError('Qualified source changed during check')
    proof = {'passed': True, 'host': tested, 'source': before, 'sourceBeforeAfterMatch': True,
        'qualifiedCompanionInputs': inputs, 'syntheticEmbeddedImage': True,
        'sdkObjects': {p.name: pin(p) for p in [*objects, joined]}, 'commands': commands,
        'fixturesRetained': str(fixture), 'nativeRuntimeQualified': False,
        'consoleActions': 0, 'deployed': False}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'host': tested, 'sdkUnits': 2, 'relocatableLink': True}))


if __name__ == '__main__':
    main()
