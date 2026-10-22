"""Declaration-only SDK object checks for offline synthetic lifecycle tests.

No historical build, embedded payload, private proof or console is required.
The complete source builder separately links the real generated package.
"""
from pathlib import Path
import json


def production_rows(root, out, sdk=Path('/opt/ps5-payload-sdk')):
    out = Path(out)
    headers = out / 'declarations'
    headers.mkdir(parents=True, exist_ok=True)
    (headers / 'native_bundle.h').write_text(
        '#include "native_install.h"\n#include "frontend_identity.h"\n'
        'extern const struct bd_native_bundle bd_embedded_bundle;\n'
        'extern const struct bd_frontend_package bd_embedded_frontend_package;\n'
        'extern const unsigned char bd_embedded_mount_link[];\n'
        'extern const size_t bd_embedded_mount_link_size;\n', encoding='ascii')
    source = root / 'src/blackwire'
    includes = [source / n for n in ('library', 'streamreader', 'dashboard/model',
        'dashboard/service', 'dashboard/package', 'dashboard/local-removal',
        'tools/compressed_stream_candidate')]
    includes += [root / 'vendor/libsmb2/include', root / 'vendor/sqlite',
                 root / 'vendor/ampr/third_party/lz4/lib', headers,
                 sdk / 'target/user/homebrew/include']
    recipe = json.loads((root / 'tools/build/app-recipe.json').read_text())
    sources = set()
    for row in recipe['commands']:
        if '-c' not in row['argv']:
            continue
        name = row['argv'][row['argv'].index('-c') + 1]
        if name == '{READER}':
            sources.add(source / 'reader/native-reader.c')
        elif name.startswith('{SRC}/'):
            sources.add(source / name[len('{SRC}/'):])
    for part in ('model', 'service', 'package', 'local-removal'):
        sources.update((source / 'dashboard' / part).glob('*.c'))
    sources.update(source / 'library' / n for n in ('preload_bundle.c',
        'preload_handoff.c', 'preload_launch.c', 'launch_timing.c'))
    rows = []
    for path in sorted(sources):
        if path.suffix != '.c':
            continue
        target = out / 'template' / (path.stem + '.o')
        target.parent.mkdir(parents=True, exist_ok=True)
        argv = [str(sdk / 'bin/prospero-clang'), '-std=gnu11', '-Wall', '-Wextra',
            '-Werror', '-Wno-misleading-indentation', '-g', '-DBW_NATIVE_BACKEND=1',
            '-DBW_NATIVE_PLAYBACK=1', '-DBW_NATIVE_PRELOAD=1',
            '-DBW_GTA_AUDIO_BOOTSTRAP=1', '-DBW_LAUNCH_TIMING=1']
        if path.name == 'native-reader.c':
            argv += ['-DBW_READER_READY=1', '-DBW_OWNED_VM=1', '-DBW_NATIVE_READER_EMBEDDED=1']
        argv += [str(v) for directory in includes for v in ('-I', directory)]
        argv += ['-MD', '-MF', str(target.with_suffix('.d')), '-c', str(path), '-o', str(target)]
        rows.append({'label': 'sdk-' + path.stem, 'argv': argv})
    return rows
