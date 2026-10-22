# Miniz inflate subset

These five files are retained byte-identically from
`juma-sayeh/PS5-Game-Compressor`, commit
`5a040799eed7a8355ae9194608f353717b6634d5`, under `src/`.
Only the independently licensed Miniz inflate implementation is included.
Its MIT permission/copyright notice remains at the top of `miniz_tinfl.c`.
No application code from that project is included here.

Build with `MINIZ_NO_MALLOC` and `MINIZ_NO_STDIO`. The Blackwire adapter uses
the low-level bounded decoder and accepts only an entire zlib stream producing
exactly 65,536 bytes. Codec state and I/O workspace are separate from the
existing game's data cache. This code has no process or kernel operations.
