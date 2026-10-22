"""Minimal synthetic BWMAP002 encoder for offline lifecycle fixtures only."""
from dataclasses import dataclass
import struct

LOCAL, STREAM = 1, 2


@dataclass(frozen=True)
class File:
    path: str
    size: int
    mtime: int
    flags: int


def ascii_key(text):
    return bytes(c + 32 if 65 <= c <= 90 else c for c in text.encode('utf-8'))


def encode(files):
    files = tuple(files)
    assert 1 <= len(files) <= 131072
    seen, pool, records = set(), bytearray(), bytearray()
    for row in files:
        assert row.path and not row.path.startswith('/') and '\\' not in row.path
        assert all(part not in ('', '.', '..') for part in row.path.split('/'))
        key = ascii_key(row.path)
        assert key not in seen and row.flags in (LOCAL, STREAM)
        seen.add(key)
        raw = row.path.encode('utf-8')
        records.extend(struct.pack('<IIIIQQ', len(pool), len(raw), row.flags, 0, row.size, row.mtime))
        pool.extend(raw + b'\0')
    body, digest = records + pool, 14695981039346656037
    for byte in body:
        digest = ((digest ^ byte) * 1099511628211) & 0xffffffffffffffff
    return struct.pack('<8sIIIIQQ', b'BWMAP002', 2, 32, len(files), 0, len(pool), digest) + body
