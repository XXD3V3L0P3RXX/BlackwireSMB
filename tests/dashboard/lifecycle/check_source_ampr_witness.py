#!/usr/bin/env python3
"""Actual bundle source-witness tests; synthetic data and host filesystem only."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, tempfile, zipfile
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
LIB = ROOT / 'src/blackwire/library'
def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--playgo-v04-archive', type=Path, help='Optional exact official archive as private host test data; never executed')
    a = parser.parse_args(); out = a.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists(): raise ValueError('Fresh local build output required')
    out.mkdir()
    # POSIX identity/mode faults need the native host filesystem; an NTFS
    # checkout may report emulated modes despite successful chmod calls.
    cases = Path(tempfile.mkdtemp(prefix='blackwire-source-ampr-witness-'))
    run_inputs = []
    official = {}
    if a.playgo_v04_archive:
        archive = a.playgo_v04_archive.resolve(strict=True)
        if archive.is_relative_to(ROOT): raise ValueError('Official fixture archive must remain outside source repository')
        if pin(archive) != {'bytes':12288,'sha256':'615999cf2869218f0f1b07b27545dde62905887435c3c0e9fa9f119eceb85732'}: raise ValueError('Official archive differs')
        variants = [('log/libScePlayGo.sprx',17430,'39c508a84208a81042dfe637619ece44a6b1d7f737076523aa8096e5eba3457f'),('nolog/libScePlayGo.sprx',11590,'a0f20f40107713523ba1a3a0281fb7c4e238b2b5d3fda9354119352d9393d2f0')]
        with zipfile.ZipFile(archive) as zipped:
            for index,(name,size,sha) in enumerate(variants):
                raw = zipped.read(name)
                if len(raw)!=size or hashlib.sha256(raw).hexdigest()!=sha: raise ValueError('Official member differs')
                fixture = cases / ('official-playgo-%d.sprx' % index)
                with fixture.open('xb') as target: target.write(raw)
                official[name] = pin(fixture); run_inputs.append(str(fixture))
    owned = [LIB / n for n in ('preload_bundle.c', 'preload_bundle.h', 'preload_source_ampr_internal.h', 'native_launch_admission.c', 'native_launch_admission.h')]
    owned += [Path(__file__), HERE / 'test_source_ampr_witness.c']
    dependencies=[LIB/'source_companions.c',LIB/'source_companions.h',LIB/'preload_companions_internal.h',ROOT/'src/blackwire/streamreader/manifest.c',ROOT/'src/blackwire/streamreader/manifest.h']
    owned += dependencies
    before = {str(p.relative_to(ROOT)): pin(p) for p in owned}
    image = bytes((i * 19 + 1) % 251 for i in range(2048)); digest = hashlib.sha256(image).digest()
    image_source = out / 'image.c'
    image_source.write_text('#include "preload_bundle.h"\nstatic const unsigned char image[]={' + ','.join(map(str,image)) + '};\nconst struct bw_preload_image bw_preload_embedded_image={image,sizeof image,{' + ','.join(map(str,digest)) + '},"' + '0'*64 + '",10000};\n')
    command = ['clang-18', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-unused-function',
               '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-I', str(LIB),
               str(HERE / 'test_source_ampr_witness.c'), str(image_source), *[str(p) for p in dependencies if p.suffix=='.c'], '-Wl,--wrap=close', '-Wl,--wrap=write', '-pthread', '-o', str(out / 'host-source-ampr-witness')]
    commands = []
    for name, argv in [('compile', command), ('run', [str(out / 'host-source-ampr-witness'), str(cases), *run_inputs])]:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=180,
                                env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS':'halt_on_error=1'})
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        commands.append({'argv': argv, 'exitCode': result.returncode})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
        if result.returncode: raise RuntimeError(name+' failed; private fixtures/logs retained')
    host = json.loads(result.stdout)
    after = {str(p.relative_to(ROOT)): pin(p) for p in owned}
    if before != after: raise RuntimeError('Source changed during qualification')
    proof = {'passed': True, 'host': host, 'source': before, 'sourceBeforeAfterMatch': True,
             'consoleActions': 0, 'moduleExecution': False, 'officialPlayGo04FixturePins': official, 'productionCallerIntegrationQualified': False, 'syntheticFixturesRetained': str(cases),
             'scope': 'Actual bundle implementation with an independently pinned synthetic admission-owner fixture; producer/caller integration remains required.'}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps({'passed':True, **host}))
if __name__ == '__main__': main()
