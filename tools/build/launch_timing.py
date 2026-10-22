"""Add the observational timing TU to the maintained app, never its SPRX."""
INSTRUMENTED = frozenset((
    'main.c', 'native_backend.c', 'native_register.c', 'reader_preflight.c',
    'source_prepare.c', 'native_setup.c', 'native_send.c', 'preload_launch.c',
    'native-reader.c',
))


def commands(recipe_commands):
    rows = [{'label': row['label'], 'argv': list(row['argv'])} for row in recipe_commands]
    if any(row['label'] == 'compile-launch-timing' for row in rows):
        raise ValueError('Timing source was already added')
    for row in rows:
        argv = row['argv']
        if '-c' not in argv:
            continue
        source = argv[argv.index('-c') + 1]
        if source.rsplit('/', 1)[-1] in INSTRUMENTED or row['label'] == 'compile-reader':
            if '-DBW_LAUNCH_TIMING=1' in argv:
                raise ValueError('Unexpected repeated timing flag')
            argv.insert(1, '-DBW_LAUNCH_TIMING=1')
    links = [row for row in rows if '-o' in row['argv'] and '{OUT}/blackwire-native.elf' in row['argv']]
    if len(links) != 1:
        raise ValueError('Expected exactly one maintained app link')
    link = links[0]
    target = '{OUT}/objects/launch-timing.o'
    link['argv'].insert(link['argv'].index('-o'), target)
    rows.insert(rows.index(link), {'label': 'compile-launch-timing', 'argv': [
        '{SDK}/bin/prospero-clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
        '-DBW_LAUNCH_TIMING=1', '-I', '{SRC}/library', '-c', '{SRC}/library/launch_timing.c', '-o', target,
    ]})
    return rows
