"""Actual projected Send/admission/envelope -> DATA read, local transport only."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys, tempfile
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SRC = ROOT / 'src/blackwire'
sys.path.insert(0, str(ROOT / 'tests/compressed'))
from fixtures import make_pfs, pfsc
from fixtures_exfat import make_exfat

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = ['library/source_namespace.c', 'library/source_companions.c', 'library/source_provider.c', 'library/source_prepare.c', 'library/source_smb.c', 'library/source_directory.c', 'library/source_snapshot.c', 'library/compressed_data.c', 'library/source_plan.c', 'library/source_profile.c', 'library/source_binding.c', 'library/source_identity.c', 'library/source_param.c', 'library/reader_selection.c', 'library/reader_projection.c', 'reader/preload/source.c', 'streamreader/filemap.c', 'streamreader/container_source.c', 'streamreader/container_manifest.c', 'streamreader/container_smb.c', 'streamreader/pfs_source.c', 'streamreader/pfsc_range.c', 'streamreader/pfsc_inflate.c', 'streamreader/manifest.c', 'streamreader/share_config.c', 'streamreader/cache.c']
    # The producer and admission are included exactly as the production Send TU.
    inputs = [SRC / n for n in sources] + list((SRC / 'library').glob('*.h')) + list((SRC / 'streamreader').glob('*.h')) + list((SRC / 'reader/preload').glob('*.h')) + [SRC / 'library' / n for n in ('native_send.c', 'native_companion_state.c', 'native_prepare_ops.c', 'native_startup_upgrade.c', 'native_launch_admission.c')] + [HERE / 'test_projected_data_chain.c', ROOT / 'tests/compressed/namespace_test.c', ROOT / 'tests/compressed/fixtures.py', ROOT / 'tests/compressed/fixtures_exfat.py', Path(__file__)]
    def pins():
        return {str(p.relative_to(ROOT)): {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs}
    before = pins()
    commands = []
    def run(label, argv):
        p = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=180, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        commands.append({'label': label, 'argv': list(map(str, argv)), 'exitCode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if p.returncode:
            raise RuntimeError(label + ' failed; see retained commands')
        return p.stdout
    run('compile', ['clang-18', '-D_DEFAULT_SOURCE', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-I' + str(SRC / 'library'), '-I' + str(SRC), '-I' + str(ROOT / 'vendor/libsmb2/include'), HERE / 'test_projected_data_chain.c', *[SRC / n for n in sources], '-Wl,--gc-sections', '-pthread', '-o', out / 'test'])
    results = []
    base = {'eboot.bin': b'BLACKWIRE INERT EBOOT FIXTURE', 'sce_sys/param.json': b'{"titleId":"PPSA99001"}', 'assets/stream.pak': bytes((i * 13 + 7) % 251 for i in range(180017))}
    cases = [('f1', {'fakelib/libSceAmpr.sprx': b'ORIGINAL INERT AMPR', 'fakelib/README': b'first metadata', 'fakelib/helper.so': b'opaque module'}, ()), ('f2', {'fakelib2/libSceAmpr.sprx': b'ORIGINAL INERT AMPR', 'fakelib2/README': b'preferred metadata', 'fakelib2/helper.so': b'opaque module'}, ()), ('both', {'fakelib/README': b'first metadata', 'fakelib2/README': b'preferred metadata', 'fakelib2/helper.so': b'opaque module'}, ()), ('empty-f2', {'fakelib/README': b'nonpreferred'}, ('fakelib2',))]
    with tempfile.TemporaryDirectory(prefix='blackwire-projected-data-') as work:
        temp = Path(work)
        for label, extra, dirs in cases:
            files = {**base, **extra}
            for kind in ('folder', 'pfs', 'pfsc', 'exfat'):
                share = temp / (label + '-' + kind + '-share')
                game = share / 'fixture'
                game.mkdir(parents=True)
                for directory in dirs:
                    (game / directory).mkdir(parents=True, exist_ok=True)
                for name, body in files.items():
                    p = game / name
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(body)
                image = None
                if kind == 'pfs': image = make_pfs(files, directories=dirs)[0]
                elif kind == 'pfsc': image = pfsc(make_pfs(files, directories=dirs)[0])
                elif kind == 'exfat': image = make_exfat(files, directories=dirs)[0]
                if image is not None: (share / 'fixture.ffpfsc').write_bytes(image)
                output = temp / (label + '-' + kind + '-console-model')
                output.mkdir()
                result = json.loads(run(label + '-' + kind, [out / 'test', 'folder' if kind == 'folder' else 'image', share, output, label]))
                results.append({'case': label, 'kind': kind, **result})
                if any((game / name).read_bytes() != body for name, body in files.items()): raise ValueError('Folder source changed')
                if image is not None and (share / 'fixture.ffpfsc').read_bytes() != image: raise ValueError('Container source changed')
    after = pins()
    if before != after: raise RuntimeError('Source changed during verification')
    proof = {'passed': True, 'cases': len(results), 'checks': sum(r['checks'] for r in results), 'source': after, 'sourceBeforeAfterMatch': True, 'results': results, 'actualProducerSourceCheckedAdmissionEnvelopeAndDATARead': True, 'sourceUnchanged': True, 'transportOnlyModeled': True, 'consoleActions': 0, 'gameplayOrNativeRuntimeQualified': False}
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({k: proof[k] for k in ('passed', 'cases', 'checks', 'consoleActions')}))

if __name__ == '__main__': main()
