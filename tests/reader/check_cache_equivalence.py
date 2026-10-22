#!/usr/bin/env python3
"""Compare the indexed cache to an explicitly supplied released cache source."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / 'src/blackwire/streamreader'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = out / 'released-cache.c'
    baseline.write_bytes(args.baseline.resolve(strict=True).read_bytes())
    renames = ['-D' + name + '=reference_' + name.removeprefix('bw_') for name in ('bw_cache_init', 'bw_cache_init_with_storage', 'bw_cache_destroy', 'bw_cache_contains', 'bw_cache_get')]
    def run(argv):
        result = subprocess.run(list(map(str, argv)), capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout
    def build(sanitized):
        flags = ['-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-I' + str(source)]
        flags += ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitized else ['-O3']
        label = 'sanitized' if sanitized else 'optimized'
        obj = out / (label + '-reference.o')
        run(['clang-18', *flags, *renames, '-c', baseline, '-o', obj])
        exe = out / label
        run(['clang-18', *flags, source / 'cache.c', root / 'tests/reader/cache_equivalence.c', obj, '-o', exe])
        return exe
    verified = json.loads(run([build(True)]))
    exe = build(False)
    timings = [json.loads(run([exe, 'benchmark'])) for _ in range(3)]
    proof = {'passed': verified['passed'], 'functional': verified, 'timings': timings,
             'medianReferenceNanoseconds': statistics.median(row['referenceNanoseconds'] for row in timings),
             'medianIndexedNanoseconds': statistics.median(row['indexedNanoseconds'] for row in timings),
             'baselineSha256': hashlib.sha256(baseline.read_bytes()).hexdigest(), 'cpuMicrobenchmarkOnly': True,
             'consoleContacted': False}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof))

if __name__ == '__main__':
    main()
