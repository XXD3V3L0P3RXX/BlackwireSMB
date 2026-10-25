# Build Blackwire

The ordinary builder produces the standalone web resident and embedded folder/PFS/PFSC reader as `Blackwire.elf`. It builds corresponding runtime/dependency source offline and does not install, load or contact a console. The native Dashboard interface and assets are excluded.

## Toolchain

Use Linux or WSL with Python 3.10+, CMake/Make, LLVM 18 tools, and the public [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk). Dependency and selected CRT/libc source are retained in the checkout; the public SDK toolchain/imports remain explicit external build inputs. Consult [SDK reference](../metadata/sdk-reference.json), [runtime instructions](../vendor/sdk-runtime/README.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).

Use checkout and SDK paths without spaces or shell metacharacters because retained upstream Make recipes require simple paths. Run from the repository root:

```sh
python3 tools/build.py --check
python3 tools/build.py --host-tests --name host-01
python3 tools/build.py --build --host-tests --name web-01 --sdk /opt/ps5-payload-sdk
```

Use a new output name each time; an existing output is refused. `--check` also works on Windows. Compilation and host tests use Linux/WSL. The final asset is `build/web-01/dist/Blackwire.elf` for the example above, with objects, source/runtime receipts and checks under the same output. There is no deployment step in this builder.

The separate browser behavior suite requires Node.js:

```sh
python3 tools/test_ui.py
```

These commands describe the public recipe; they do not assert that every listed check was rerun for a particular release. Read its attached build manifest and the [hardware test record](TESTING.md).

## v0.3.0-beta.1 build record

The beta is a complete clean-export SDK build of frozen source commit `85c66956361e41ffaf98012f7e1c1f20bcb5944d`, with runtime/logging source commit `27a7565320c1d6951e7f846b6751eb7e0e039b4d`. All 164 commands and twelve host suites passed without `--allow-source-changes`, and source hashes matched across the build. The attached `Blackwire-Beta.elf` is the unchanged tested output renamed for download: 5,772,536 bytes, SHA-256 `98d35b094f4d2585525434e2af9fab03ed5c71b17e39bd6a1e1aed484ce8d6a9`. Its build ID is `blackwire-v0.3.0-beta.1`.

The reader remains 843,696 bytes, SHA-256 `39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441`. The release includes exFAT and the self-contained installer plus bounded setup diagnostics. The matching relink archive independently reproduces the packaged ELF byte for byte. [Beta observations](TESTING.md#v030-beta1), metadata and attached SHA-256 sums identify this exact artifact; prior preview results remain separate.

After that build, the standalone upgrade-test harness gained the missing production-header search path needed by its isolated diagnostic fixture. The corrected harness and full removal suite passed against the same unchanged payload sources. `metadata/beta-build-source-manifest.json` preserves the frozen build manifest; the current source manifest additionally pins that test-only harness correction. Release documentation and packaging do not patch or rebuild the tested ELF.

## Previous preview build record

`v0.2.0-preview.1` packages the exact `blackwire-two-sources-dev.11` binary built from runtime source commit `f490e1917a1f1393d0948b24b9d93d03eee9e95b`. A full source SDK build passed all 144 commands and verified unchanged build inputs from start to finish. The packaged ELF is 5,738,856 bytes, SHA-256 `b9189c87cdb88d3ad0fccc947d0d007185bb0cfa1b39e8edb585c96347411fd9`. The reader is 831,168 bytes, SHA-256 `c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025`.

The release commit adds documentation, catalog and provenance records after that runtime commit. It does not rebuild or patch the tested executable. The internal build ID therefore remains `blackwire-two-sources-dev.11`; use the release manifest and checksums to identify the distributed package. The reader is byte-identical to the combined-library development-7 reader; the subsequent changes are in the resident's embedded UI and build label.

Ninety UI checks passed against the actual embedded page. The exact page was read back from the console after loading this ELF. The user confirmed root Circle exit and Details dismissal; the separate Close browser button failed and remains a documented limitation. See [current console observations](TESTING.md#compressed-and-two-share-preview).

The public package includes corresponding source and relinking material for the current binary. Earlier release records remain available under their original Git tag. When building modified source, use a new output directory and record a new artifact identity rather than treating a rebuild as the tested binary.

## Earlier folder-only preview build

`v0.1.0-preview.1` used a complete clean source baseline followed by a focused interface-only closing-tag repair. Its backend and embedded reader were unchanged by that repair. Those earlier identities, modal checks and retained-file GTA recovery are preserved in the [folder-only test record](TESTING.md#folder-only-preview) and [prior recovery result](TESTING.md#gta-recovery-on-the-prior-resident).

## Source identity and relinking

The checkout preserves `src/blackwire`, `tools/build`, `vendor`, `tests` and `metadata`. The builder verifies pinned source/build inputs and refuses unexpected changes by default. Contributors can deliberately build modified source using `--allow-source-changes`; record the changes and generate a new release identity rather than presenting it as the original pinned artifact.

The resident embeds the selected reader, the shared console/LAN HTML page and the blue project logo. Its home-screen startup broker, registration policy and explicit Media SDK action belong to the standalone resident. The packaged reader retains the working STRICT/coalescer data path. `BlackwireDashboard.elf` is the separately planned native product, not an alternative output name for this preview.

Retain the complete corresponding source, build transformations, dependency/runtime pins, notices and practical relinking material when distributing a binary. The [relink manifest](../metadata/relink-manifest.json) describes the retained build products. No private development folder, earlier output, proprietary SDK, game content or external console library should be needed as a source-build input.

Before shipping, verify a clean-export build, exact source/binary hashes, packaged-byte privacy and license obligations. Host tests, compiler success and hardware qualification establish different things. A rebuild of tested source is not automatically a byte-identical or hardware-tested artifact.

Vendored AMPR Git attributes disable text conversion so clones and source archives preserve the recorded file hashes. This packaging-only change does not alter runtime source. Its original and packaged attribute hashes are recorded in the build manifest.

## Optional logging diagnostic

The optional `v0.2.0-preview.1-sendlog.1` release has [separate diagnostic instructions](DIAGNOSTICS.md), including the exact artifact identity, build command, log collection and rollback. The normal preview remains unchanged.
