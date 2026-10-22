#!/usr/bin/env python3
"""Synthetic compressed reader tests; fixtures contain no game material."""
from pathlib import Path
import argparse
import json
import random
import struct
import subprocess
import sys
import tempfile
from fixtures import make_pfs, pfsc, wrapped

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rng = random.Random(84271)
    raw = rng.randbytes(65536)
    contents = {'eboot.bin': b'SYNTHETIC-BOOT' * 100, 'sce_sys/param.json': b'{"titleId":"PPSA09999"}',
                'packs/first.pak': raw * 16 + b'A' * 65536 + b'tail-one',
                'other/equal.pak': b'B' * 65536 + raw * 16 + b'tail-two', 'zero.dat': b''}
    pfs, _ = make_pfs(contents)
    packed = pfsc(pfs)
    reports = []
    with tempfile.TemporaryDirectory(prefix='blackwire-compressed-fixtures-') as work:
        temp = Path(work)
        folder = temp / 'originals'
        folder.mkdir()
        for name, body in contents.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
        admitted = temp / 'admitted.pfs'
        admitted.write_bytes(pfs)
        subprocess.run([sys.executable, str(root / 'tests/compressed/check_reader.py'), '--image', str(admitted), '--folder', str(folder),
                        '--output', str(out / 'compiled')], check=True, capture_output=True, text=True)
        exe = out / 'compiled/compressed-reader-test'
        variants = {'plain-pfs': pfs, 'bare-pfsc': packed, 'wrapped-pfsc': wrapped(pfs), 'wrapped-plain': wrapped(pfs, encode=False)}
        damaged = bytearray(packed)
        first, end = struct.unpack_from('<QQ', damaged, 0x400)
        if not end - first < 65536:
            raise ValueError('Metadata block must exercise the compressed checksum')
        damaged[end - 1] ^= 0x01
        invalid_table = bytearray(packed)
        struct.pack_into('<Q', invalid_table, 0x408, first)
        invalid_mode = bytearray(pfs)
        struct.pack_into('<H', invalid_mode, 0x1c, 0x10)
        exfat = bytearray(65536)
        exfat[3:11] = b'EXFAT   '
        mismatch, _ = make_pfs({**contents, 'packs/first.pak': contents['packs/first.pak'] + b'mismatch'})
        refused = {'manifest-mismatch': mismatch, 'checksum-failure': damaged, 'nonmonotonic-table': invalid_table, 'unsupported-signed-pfs': invalid_mode,
                   'unsupported-exfat': pfsc(exfat), 'truncated-header': packed[:32]}
        for label, body in {**variants, **refused}.items():
            image = temp / (label + '.ffpfsc')
            image.write_bytes(body)
            mode = 'refuse-codec-cancel' if label == 'checksum-failure' else 'refuse-late-cancel' if label.startswith('unsupported-') else 'refuse'
            result = subprocess.run([str(exe), str(admitted), str(image), str(folder), *([mode] if label in refused else [])],
                                    capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(label + ': ' + result.stdout + result.stderr)
            reports.append({'case': label, 'result': json.loads(result.stdout)})
    proof = {'passed': True, 'cases': reports, 'syntheticOnly': True, 'liveSmbServer': False, 'consoleContacted': False}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'passed': True, 'cases': len(reports), 'checks': sum(row['result']['checks'] for row in reports),
                      'faultsAndUnsupportedFormatsRefused': len(refused)}))

if __name__ == '__main__':
    main()
