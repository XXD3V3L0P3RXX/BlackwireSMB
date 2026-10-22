# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent synthetic PFS/PFSC fixture writer, retained from earlier format tests."""
import struct
import zlib
BLOCK=4096

def dir_blob(entries):
    output = bytearray()
    positions = {}
    for name, inode, kind in entries:
        encoded = name.encode('utf-8')
        size = (16 + len(encoded) + 1 + 7) // 8 * 8
        positions[name] = len(output)
        output += struct.pack('<IIII', inode, kind, len(encoded), size)
        output += encoded + bytes(size - 16 - len(encoded))
    if len(output) > BLOCK:
        raise ValueError('Fixture directory exceeds one block')
    return output + bytes(BLOCK - len(output)), positions


def make_pfs(files, *, compressed=None, mode=8):
    compressed = compressed or {}
    folders = {''}
    for path in files:
        parts = path.split('/')
        folders.update('/'.join(parts[:i]) for i in range(1, len(parts)))
    folders = sorted(folders, key=lambda value: (value.count('/'), value))
    names = folders + sorted(files)
    ids = {name: i + 2 for i, name in enumerate(names)}
    count = len(names) + 2
    per = BLOCK // 168
    inode_blocks = (count + per - 1) // per
    entries = {'@super': [('flat_path_table', 1, 2), ('uroot', ids[''], 3)]}
    for folder in folders:
        parent = folder.rsplit('/', 1)[0] if '/' in folder else ''
        values = [('.', ids[folder], 4), ('..', ids[parent], 5)]
        prefix = folder + '/' if folder else ''
        for name in names:
            if name != folder and name.startswith(prefix) and '/' not in name[len(prefix):]:
                values.append((name[len(prefix):], ids[name], 3 if name in folders else 2))
        entries[folder] = values
    payloads = {0: dir_blob(entries['@super'])[0], 1: b''}
    for folder in folders:
        payloads[ids[folder]] = dir_blob(entries[folder])[0]
    for name, data in files.items():
        payloads[ids[name]] = data
    location = 1 + inode_blocks
    bases, blocks = {}, {}
    for inode in range(count):
        data = payloads[inode]
        blocks[inode] = max(1, (len(data) + BLOCK - 1) // BLOCK)
        bases[inode] = location * BLOCK
        location += blocks[inode]
    output = bytearray(location * BLOCK)
    struct.pack_into('<qq', output, 0, 2, 20130315)
    output[0x1a] = 1
    struct.pack_into('<H', output, 0x1c, mode)
    struct.pack_into('<I', output, 0x20, BLOCK)
    struct.pack_into('<qqqq', output, 0x28, 1, count, location, inode_blocks)
    inode_offsets = {}
    for inode in range(count):
        at = (1 + inode // per) * BLOCK + (inode % per) * 168
        inode_offsets[inode] = at
        data = payloads[inode]
        directory = inode == 0 or inode in {ids[name] for name in folders}
        logical = compressed.get(inode, len(data))
        flags = 0x10 | (1 if inode in compressed else 0)
        struct.pack_into('<HHIqq', output, at, 0x41ff if directory else 0x81ff,
                         2 if directory else 1, flags, len(data), logical)
        struct.pack_into('<I', output, at + 0x60, blocks[inode])
        struct.pack_into('<12i5i', output, at + 0x64, bases[inode] // BLOCK,
                         *([-1] * 16))
        output[bases[inode]:bases[inode] + len(data)] = data
    return output, {'ids': ids, 'bases': bases, 'inodeOffsets': inode_offsets,
                    'entries': entries, 'count': count}


def pfsc(data):
    block_size = 65536
    padded = data + bytes((-len(data)) % block_size)
    pieces = [zlib.compress(padded[i:i + block_size]) for i in range(0, len(padded), block_size)]
    pieces = [piece if len(piece) < block_size else padded[i * block_size:(i + 1) * block_size]
              for i, piece in enumerate(pieces)]
    offsets = [block_size]
    for piece in pieces:
        offsets.append(offsets[-1] + len(piece))
    head = bytearray(block_size)
    struct.pack_into('<iiiiqqQq', head, 0, 0x43534650, 0, 6, block_size,
                     block_size, 0x400, block_size, len(padded))
    struct.pack_into('<' + 'Q' * len(offsets), head, 0x400, *offsets)
    return head + b''.join(pieces)


def wrapped(data, *, encode=True):
    packed = pfsc(data) if encode else data
    # A one-file outer filesystem has user root inode 2 and payload inode 3.
    return make_pfs({'pfs_image.dat': packed}, compressed={3: len(data)} if encode else None)[0]


