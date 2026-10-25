# exFAT reader development

This branch adds an experimental read-only exFAT adapter to the standalone Blackwire resident. Existing `v0.2.0-preview.1` and SendLog downloads are unchanged and do not contain this adapter. Native Dashboard development is separate.

## Why

The command supplied in issue #2 produces PFS/PFSC-wrapped exFAT with MkPFS 0.0.9 and 1.1.0 in disposable reproductions. The released reader rejects that inner filesystem. The reporter's diagnostic summary enumerates 52 images and rejects 51 during format validation; the summary alone does not prove that every rejected image uses exFAT.

The new adapter uses the existing PFSC decoder, immutable SMB generation checks and per-file manifest bindings. It does not mount or extract the image on the console, write to the share, or require a PC server process. The on-disk reference is the [Microsoft exFAT specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification).

## Admitted layout

- Clean exFAT revision 1.00 volumes, one FAT, bounded sector/cluster geometry and fully initialized files (`ValidDataLength == DataLength`).
- Contiguous directory and file allocations, either with a matching FAT chain or with NoFatChain set. Fragmented allocations, TexFAT, unknown critical entries and other layouts refuse.
- Bare exFAT, PFSC containing exFAT, and the supported one-file PFS wrappers. Diagnostic format labels are `exfat`, `pfsc_exfat` and `nested_exfat`.
- UTF-16 filenames converted to validated UTF-8 paths, with the image's checked Unicode up-case table used for name hashing, case-insensitive lookup and sibling-collision checks.

Both boot checksums, geometry, FAT chains, allocation bits, up-case checksum, entry-set/name checksums, path limits, cycles and overlapping allocations are checked. The existing 32-byte file tokens and 64 MiB payload-cache budget remain unchanged. A fixed 128 KiB Unicode map is added to the container workspace; temporary namespace metadata is bounded and released after walking.

## PC checks

Run from Linux/WSL with LLVM 18:

```sh
python3 tests/compressed/check_exfat.py --output build/exfat-check-01
python3 tests/compressed/check_synthetic.py --output build/stream-check-01
python3 tools/build.py --build --host-tests --allow-source-changes --name exfat-build-01 --sdk /opt/ps5-payload-sdk
```

Use absent output directories. These scripts do not contact a console. The ordinary host build now includes the exFAT and compressed streaming suites.

Recorded development results:

- Both MkPFS versions' default outputs now pass the actual reader in disposable tests. All five fixture files and all 1,180,029 bytes match their independent originals; PFS controls also pass.
- Independent exFAT fixtures pass five supported variants and refuse sixteen malformed/unsupported cases under AddressSanitizer and UndefinedBehaviorSanitizer. Every supported fixture compares all six files and all 2,097,545 bytes.
- Thirteen actual-reader PFS/exFAT transport cases pass 18,188 checks, including positive short reads, cancellation recovery, cached-read generation checks and transport cleanup.
- A separate MkPFS 0.0.9 Red Dead Redemption image has all 173 files checked, with 535 sampled/sequence ranges and 46,018,942 compared bytes. Removing repeated FAT-chain traversal reduces the same comparison from 72,426 to 3,579 physical-read callbacks (658,756,117 to 67,285,635 requested bytes).
- A separate MkPFS 0.0.9 Need for Speed Unbound image has all 297 files checked, with 904 ranges and 56,099,677 compared bytes. These real-image comparisons are sampled file data plus the first 16 MiB of the largest file, not exhaustive image-byte checks.
- The optimized standalone SDK build passes 162 commands with unchanged source hashes from beginning to end. No native frontend or native asset bundle is included.

Host request counts and elapsed times do not establish PS5 gameplay loading times. Console discovery, setup and gameplay must be recorded separately for the exact ELF and reader hashes.

## Separate step-7 issue

Adding exFAT does not supply missing runtime libraries. The existing preview still expects its qualified five-file set at `/data/shadowmount/fakelib`. An empty directory or dummy file is insufficient. A console test where those libraries already exist cannot establish that the missing-directory case is repaired. The reporter's firmware 10.01 and Kstuff combination remain unqualified.
