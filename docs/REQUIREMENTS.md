# Runtime requirements and tested scope

Use your usual working jailbreak environment with **Kstuff active** before loading Blackwire. Blackwire is not a jailbreak or Kstuff installer. The observed environment used Kstuff-lite `v1.11`, elfldr `v0.26`, and **stock ShadowMountPlus `1.7-beta5fix1`**. A modified ShadowMount or separate Media helper is not required for the verified working path.

The qualified process-layout profile is **`0x13000040`**. Version names alone do not establish compatible code, and other firmware profiles are not qualified by this preview.

Set ShadowMount's **Library scan depth to 2** (`scan_depth=2`) for the folder layout in the setup guide. This is the value confirmed in the tested console's configuration. Depth is relative to each configured scan root; see [ShadowMount discovery](SETUP.md#shadowmount-discovery).

## External ShadowMount libraries

The tested setup has these exact global files at `/data/shadowmount/fakelib`. Obtain them through the compatible runtime's normal distribution; these external console/runtime libraries are **not bundled with Blackwire**.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| libSceAmpr.sprx | 423350 | 69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a |
| libSceAppContent.sprx | 32982 | 53e485e22970c693ab70c05bd78b8f627442ed41ca9a49692b38615046494a6b |
| libSceGameUpdate.sprx | 9126 | 1e31ddbe4809b7c5ec117e9b7e1227befe757c31d1f5f73e6c7d5a3d2d238973 |
| libSceNpEntitlementAccess.sprx | 43286 | 3f1826f20e174ff0f7aaa8d7f3f7e386dfcca1136c9e4c0e72a49e041c0b1488 |
| libSceAmpr.sprx.orig | 423350 | 69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a |

Keep ShadowMount's API and per-game library overlay enabled. The observed setup used `debug=1` for readable lifecycle evidence, `auto_update_ampr=0`, and per-game library priority. Changing the global libraries or runtime configuration needs separate qualification. Blackwire installs its source-built reader into an exact owned game's per-game library directory; it does not replace the global set.

## PC, network and console space

- An awake PC with complete extracted game folders or supported PFS/PFSC images and a readable SMB share. A second share is optional; both must use the same server and credentials.
- An SMB account with a password and read permission at both sharing and filesystem layers. A Windows Hello PIN is not an account password.
- Wired Ethernet for the observed streaming results. Wi-Fi and other network arrangements are not qualified here.
- Console space for real startup data, preparation metadata and the working cache. Sparse files retain large logical lengths without allocating their entire contents.
- A running Blackwire resident and available PC share for web Play and direct Games-icon launches.

Python, Node, WSL, a native Dashboard and recording tools are development tools or separate products, not ordinary gameplay requirements. No additional Blackwire PC service is needed. See [setup](SETUP.md) for the Windows share instructions.

## Reader and format scope

The selected reader keeps STRICT identity validation, a contiguous-read coalescer, one worker and a 64 MiB payload cache. `v0.2.0-preview.1` adds an indexed cache and bounded PFSC batching without increasing that payload-cache budget. Extracted folders and supported unsigned contiguous D32 PFS/PFSC layouts are accepted. exFAT-based compressor output, signed/encrypted PFS, scattered extents, wide inode layouts and individually compressed inner files remain unsupported. See [format scope](COMPRESSED-DEVELOPMENT.md#formats).

The two-share configuration uses the same PC/server and sign-in details. Both shares need only read access; Blackwire does not need to write into the PC library. No compressor payload needs to run on the PS5 merely to stream an already supported compressed image.

Avoid unnecessary background workload when testing. A separate Game Compressor payload was observed consuming nearly a CPU core while idle. This is a measured CPU-contention finding, not a confirmed out-of-memory diagnosis. Firmware compatibility remains limited to the recorded profile; a successful test does not qualify every system or title.

Check [testing](TESTING.md) for the exact resident/reader identities and observed titles. A successful build, prepared copy, started process or main menu does not prove later gameplay for every game.
