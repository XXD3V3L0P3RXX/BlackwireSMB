# Build Blackwire

The ordinary builder produces the standalone web resident and embedded FOLDER reader as `Blackwire.elf`. It builds corresponding runtime/dependency source offline and does not install, load or contact a console. The native Dashboard interface and assets are excluded.

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

## Current preview build record

A complete clean source build passed for the backend and approved web interface. The current release ELF subsequently received a focused interface-only repair to the closing `main` tag so modal controls remain interactive. The backend and embedded reader are unchanged by that repair. The current ELF has its own recorded identity; it is not the same binary as the earlier clean-build artifact or the prior resident's GTA recovery observation. See the [current checks](TESTING.md#current-update) and [prior recovery result](TESTING.md#gta-recovery-on-the-prior-resident).

## Source identity and relinking

The checkout preserves `src/blackwire`, `tools/build`, `vendor`, `tests` and `metadata`. The builder verifies pinned source/build inputs and refuses unexpected changes by default. Contributors can deliberately build modified source using `--allow-source-changes`; record the changes and generate a new release identity rather than presenting it as the original pinned artifact.

The resident embeds the selected reader, the shared console/LAN HTML page and the blue project logo. Its home-screen startup broker, registration policy and explicit Media SDK action belong to the standalone resident. The packaged reader retains the working STRICT/coalescer data path. `BlackwireDashboard.elf` is the separately planned native product, not an alternative output name for this preview.

Retain the complete corresponding source, build transformations, dependency/runtime pins, notices and practical relinking material when distributing a binary. The [relink manifest](../metadata/relink-manifest.json) describes the retained build products. No private development folder, earlier output, proprietary SDK, game content or external console library should be needed as a source-build input.

Before shipping, verify a clean-export build, exact source/binary hashes, packaged-byte privacy and license obligations. Host tests, compiler success and hardware qualification establish different things. A rebuild of tested source is not automatically a byte-identical or hardware-tested artifact.

Vendored AMPR Git attributes disable text conversion so clones and source archives preserve the recorded file hashes. This packaging-only change does not alter runtime source. Its original and packaged attribute hashes are recorded in the build manifest.
