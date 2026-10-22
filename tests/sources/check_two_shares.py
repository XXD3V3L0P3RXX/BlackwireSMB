#!/usr/bin/env python3
"""Offline source-selection, merged-catalog and private-settings regression."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/build'))
from reader_patch import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'linux':
        raise ValueError('Use Linux/WSL Python and clang-18')
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh build output directory')
    out.mkdir(parents=True)
    lib, stream = ROOT / 'src/blackwire/library', ROOT / 'src/blackwire/streamreader'
    backend = (lib / 'native_backend.c').read_text()
    structure = backend[backend.index('struct bw_native_backend {'):backend.index('\n};', backend.index('struct bw_native_backend {')) + 3]
    names = ['static int source_compare(', 'static int catalog_order(',
             'static size_t catalog_selected(', 'static int valid_library(',
             'static int library_merge(', 'void bw_native_library_destroy(']
    header = out / 'source-selection-under-test.h'
    header.write_text('#include "native_backend.h"\n#include "native_json.h"\n'
                      '#include <pthread.h>\n#include <stdlib.h>\n#include <string.h>\n'
                      + structure + '\n' + '\n'.join(function(backend, name) for name in names))
    sources = [Path(__file__).with_name('two_shares_test.c')]
    sources += [lib / name for name in ('source_catalog.c', 'source_identity.c', 'source_binding.c',
                                       'source_profile.c', 'native_request.c', 'native_removal_request.c', 'source_param.c', 'native_setup.c')]
    sources += [stream / name for name in ('share_config.c', 'manifest.c')]
    pinned = sources + [header, lib / 'native_backend.c', lib / 'native_backend.h',
                        lib / 'source_catalog.h', lib / 'source_identity.h', stream / 'share_config.h']
    def pins():
        return {str(path.relative_to(ROOT)): {'bytes': path.stat().st_size,
                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in pinned}
    before = pins()
    flags = ['clang-18', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-Wno-unused-function', '-Wno-unused-variable',
             '-D_DEFAULT_SOURCE=1', '-DBW_NATIVE_SETUP_HOST_TEST=1', '-DBW_GTA_AUDIO_BOOTSTRAP=1',
             '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    for directory in (out, lib, stream, ROOT / 'vendor/libsmb2/include', ROOT / 'vendor/sqlite'):
        flags += ['-I', str(directory)]
    binary = out / 'two-shares-test'
    commands = []
    def run(label, argv):
        result = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True, text=True,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1',
                                     'UBSAN_OPTIONS': 'halt_on_error=1'}, timeout=120)
        commands.append({'label': label, 'argv': list(map(str, argv)), 'exitCode': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(label + ' failed; inspect retained commands.json')
        return result.stdout
    run('compile-actual-selection-settings-and-merge', [*flags, *sources, '-Wl,--gc-sections', '-pthread', '-o', binary])
    # Run anchored inode/rename checks on the Linux filesystem. DrvFS can
    # report different named/held timestamps after a successful replacement.
    scratch = Path(tempfile.mkdtemp(prefix='blackwire-two-shares-'))
    observed = json.loads(run('source-isolation-and-settings', [binary, scratch / 'settings']))
    shutil.copytree(scratch / 'settings', out / 'settings-fixture')
    if before != pins():
        raise ValueError('Source changed during test')
    proof = {'passed': True, 'observed': observed, 'sanitizers': ['address', 'undefined'],
             'actualBackendFunctionsExtracted': names, 'inputPins': before,
             'consoleContacted': False, 'smbConnectionsMade': False,
             'fixtureFilesystem': 'Linux', 'retainedScratch': str(scratch),
             'scope': 'Actual C selection, binding, parser, settings filesystem and merged metadata; SMB/PS5 runtime needs separate qualification'}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({key: value for key, value in proof.items() if key != 'inputPins'}))


if __name__ == '__main__':
    main()
