#!/usr/bin/env python3
"""Read-only host verification against an independently unpacked game folder."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys
import tempfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--no-read-ahead', action='store_true')
    parser.add_argument('--sequential-mib', type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.sequential_mib <= 1024:
        parser.error('Use 0 through 1024 sequential MiB')
    if sys.platform != 'linux':
        parser.error('Run through Linux or WSL; original game files are opened read-only')
    root = Path(__file__).resolve().parents[1]
    source = root / 'src/blackwire/streamreader'
    with tempfile.TemporaryDirectory(prefix='blackwire-container-') as work:
        exe = Path(work) / 'compare'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_DEFAULT_SOURCE', '-O2', '-Wall', '-Wextra', '-Werror',
                        *(['-DBW_PFSC_READAHEAD=0'] if args.no_read_ahead else []),
                        '-Wno-misleading-indentation', '-I', str(source),
                        str(root / 'tests/compressed/container_compare.c'),
                        *[str(source / name) for name in ('container_source.c', 'pfs_source.c', 'pfsc_range.c', 'pfsc_inflate.c', 'manifest.c')],
                        '-o', str(exe)], check=True)
        result = subprocess.run([str(exe), str(args.image.resolve(strict=True)), str(args.folder.resolve(strict=True)), str(args.sequential_mib)],
                                text=True, capture_output=True)
        if result.stdout:
            report = json.loads(result.stdout)
            print(json.dumps(report))
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)

if __name__ == '__main__':
    main()
