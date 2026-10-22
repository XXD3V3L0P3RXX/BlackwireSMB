#!/usr/bin/env python3
"""Qualify the early compressed reader with real files through a modeled SMB transport."""
from pathlib import Path
import argparse
import json
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--folder', type=Path, required=True)
    parser.add_argument('--live-image', type=Path)
    parser.add_argument('--refuse', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / 'tools/build'))
    import reader
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source, _ = reader.prepare(root, out)
    exe = out / 'compressed-reader-test'
    command = ['clang-18', '-D_DEFAULT_SOURCE', '-DBW_GTA_AUDIO_BOOTSTRAP=1', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I' + str(root / 'vendor/libsmb2/include'), '-I' + str(root / 'src/blackwire'),
               *[str(source / 'bw' / name) for name in reader.CFILES if not name.endswith('private_smb.c') and name not in reader.HOME_CFILES],
               str(root / 'src/blackwire/streamreader/container_smb.c'),
               str(root / 'tests/compressed/reader_test.c'), '-o', str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        print(compiled.stderr, file=sys.stderr)
        compiled.check_returncode()
    result = subprocess.run([str(exe), str(args.image.resolve(strict=True)), str((args.live_image or args.image).resolve(strict=True)),
                             str(args.folder.resolve(strict=True)), *(['refuse'] if args.refuse else [])], capture_output=True, text=True)
    (out / 'test.log').write_text(result.stdout + result.stderr)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    result.check_returncode()
    report = json.loads(result.stdout)
    (out / 'proof.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
