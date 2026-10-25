# Runtime requirements and tested scope

Use a working jailbreak environment with **Kstuff active** before loading Blackwire. Blackwire is not a jailbreak or Kstuff installer. The [README's tested-environment table](../README.md#tested-environment) records the firmware, reported jailbreak chain, payload versions and helper tools together.

The only console-qualified process-layout profile is the **13.00 family (`0x13000040`)**. Firmware **10.01 and other profiles are unverified**. Matching a payload version or successfully copying startup files does not establish firmware compatibility.

The October 24, 2026 diagnostic retest used stock ShadowMountPlus **`1.7beta7`**. Its captured payload inventory records Kstuff-lite **`v1.11`** and elfldr **`v0.27`**. The operator reports **Byepervisor Reborn** as part of the jailbreak chain; its exact deployed build and the initial jailbreak entry point/version have not been recorded. Byepervisor Reborn is a hypervisor-exploit stage, so its name alone does not specify the complete jailbreak chain.

Earlier baseline observations used stock ShadowMountPlus **`1.7-beta5fix1`** and elfldr **`v0.26`**. Those historical results remain recorded in [testing](TESTING.md); they do not qualify other firmware profiles or every combination of runtime files. A modified ShadowMount or separate Media helper is not required for the verified working path.

Set ShadowMount's **Library scan depth to 2** (`scan_depth=2`) for the folder layout in the setup guide. This is the value confirmed in the tested console's configuration. Depth is relative to each configured scan root; see [ShadowMount discovery](SETUP.md#shadowmount-discovery).

## External ShadowMount libraries

**Older-preview dependency, removed in v0.3.0-beta.1:** the original test console had these exact optional global files at `/data/shadowmount/fakelib`. The older preview incorrectly requires that pack and can report `reader_directory_named_stat_failed` with `errno=2` at setup step 7 when the path is absent, after startup files transfer. These are not missing PS5 system files. Creating an empty folder does not repair the old installer.

The [development installer](RUNTIME-BUNDLE-DEVELOPMENT.md) uses the reader already embedded in the ELF and never opens or modifies the global directory. An ordinary fresh game needs no separate global pack for reader installation. Qualified optional companions in the game's prepared `fakelib` are copied and verified; recognized existing five-file private bundles remain supported. Unknown companions or altered owned files refuse without being overwritten. This repairs installation dependency handling; it does not qualify other firmware or arbitrary backports.

The following hashes document the old preview's test environment and the recognized legacy profile, not new mandatory downloads:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| libSceAmpr.sprx | 423350 | 69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a |
| libSceAppContent.sprx | 32982 | 53e485e22970c693ab70c05bd78b8f627442ed41ca9a49692b38615046494a6b |
| libSceGameUpdate.sprx | 9126 | 1e31ddbe4809b7c5ec117e9b7e1227befe757c31d1f5f73e6c7d5a3d2d238973 |
| libSceNpEntitlementAccess.sprx | 43286 | 3f1826f20e174ff0f7aaa8d7f3f7e386dfcca1136c9e4c0e72a49e041c0b1488 |
| libSceAmpr.sprx.orig | 423350 | 69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a |

Keep ShadowMount's API and per-game library overlay enabled. The observed setup used `debug=1` for readable lifecycle evidence, `auto_update_ampr=0`, and per-game library priority. Blackwire installs its source-built reader into an exact owned game's per-game library directory; it does not replace the global set. Optional runtime/backport compatibility still needs separate qualification.

## PC, network and console space

- An awake PC with complete extracted game folders or supported PFS/PFSC images and a readable SMB share. A second share is optional; both must use the same server and credentials.
- An SMB account with a password and read permission at both sharing and filesystem layers. A Windows Hello PIN is not an account password.
- Wired Ethernet for the observed streaming results. Wi-Fi and other network arrangements are not qualified here.
- Console space for real startup data, preparation metadata and the working cache. Sparse files retain large logical lengths without allocating their entire contents.
- A running Blackwire resident and available PC share for web Play and direct Games-icon launches.

Python, Node, WSL, a native Dashboard and recording tools are development tools or separate products, not ordinary gameplay requirements. No additional Blackwire PC service is needed. See [setup](SETUP.md) for the Windows share instructions.

## Reader and format scope

The selected reader keeps STRICT identity validation, a contiguous-read coalescer, one worker and a 64 MiB payload cache. `v0.2.0-preview.1` adds an indexed cache and bounded PFSC batching without increasing that payload-cache budget. Extracted folders and supported unsigned contiguous D32 PFS/PFSC layouts are accepted. `v0.3.0-beta.1` adds the [supported contiguous exFAT layouts](EXFAT-DEVELOPMENT.md#admitted-layout). Signed/encrypted PFS, fragmented/scattered allocations, wide PFS inode layouts and individually compressed inner files remain unsupported. See [PFS scope](COMPRESSED-DEVELOPMENT.md#formats) and [beta instructions](BETA-TESTING.md).

The two-share configuration uses the same PC/server and sign-in details. Both shares need only read access; Blackwire does not need to write into the PC library. No compressor payload needs to run on the PS5 merely to stream an already supported compressed image.

Avoid unnecessary background workload when testing. A separate Game Compressor payload was observed consuming nearly a CPU core while idle. This is a measured CPU-contention finding, not a confirmed out-of-memory diagnosis. Firmware compatibility remains limited to the recorded profile; a successful test does not qualify every system or title.

Check [testing](TESTING.md) for the exact resident/reader identities and observed titles. A successful build, prepared copy, started process or main menu does not prove later gameplay for every game.
