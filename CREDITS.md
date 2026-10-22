# Credits

Blackwire contributors maintain the standalone web interface, startup preparation, resident lifecycle, SMB reader integration, tests and documentation. The native Blackwire Dashboard is a separate product in development.

Implementation, testing and documentation received assistance from **Codex (GPT-6)**. Human console testing and gameplay feedback provided the hardware observations recorded for this preview; AI assistance does not replace those checks.

| Upstream | Contribution and retained identity |
| --- | --- |
| [ampr_emu](https://github.com/drakmor/ampr_emu) | Early library/runtime; revision `cfa85df379f6eeeb165d7badf9b648e266fe77b7`; GPL-3.0-or-later. |
| [libsmb2](https://github.com/sahlberg/libsmb2) | SMB transport; version 6.1.0 with content-pinned source and documented local fixes; core LGPL-2.1-or-later. |
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | Public toolchain/imports and retained runtime source at `f5d40f02770e9b0001dd9e18056eca70b3da8b40`; per-file terms retained. |
| [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager) | Process-query pattern, with GPL notices and corresponding modified source; reference for the normal public application-install API. |
| [SQLite](https://sqlite.org/) | 3.54.1 amalgamation, check-in `555e07ef33f69a0957d24d8e555b702464a1`; public-domain dedication. |
| [Miniz](https://github.com/richgel999/miniz) | MIT inflate subset retained through PS5-Game-Compressor revision `5a040799eed7a8355ae9194608f353717b6634d5`. |
| [LZ4](https://github.com/lz4/lz4) | Ampr library support; v1.10.0, revision `ebb370ca83af193212df4dcbadcc5d87bc0de2f0`; library BSD-2-Clause. |
| [LLVM](https://github.com/llvm/llvm-project) | Compiler/C++ runtime support; Apache-2.0 WITH LLVM-exception and retained component terms. |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | External registration/mounting and per-game library selection; an external prerequisite. |

Exact retained dependency inputs and local transformations are recorded in [dependency metadata](metadata/dependencies.json), [SDK metadata](metadata/sdk-reference.json) and the component source. These links identify source or an external prerequisite; they do not imply sponsorship. Original per-file notices remain authoritative.

No game artwork/content or console system software is distributed here. Native renderer, fonts and OpenGL assets belong to the separate later Dashboard product. See [third-party notices](THIRD_PARTY_NOTICES.md) for distribution obligations.
