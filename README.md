# Blackwire

Run supported PS5 games stored on an ordinary PC SMB share, with essential startup files kept on the console. The game runs on the PS5; bulk data stays on the PC. Supports extracted folders and the documented PFS/PFSC and contiguous exFAT layouts, including supported `.ffpfsc` images. No Blackwire PC server, Python or Node service is needed.

## Current release — v0.3.0-beta.3

**[Download Blackwire.elf](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/download/v0.3.0-beta.3/Blackwire.elf)** · [Release notes and checksums](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.3.0-beta.3)

This is the exact normal ELF tested through Payload Manager: **6,015,888 bytes**, SHA-256 **`8cebe80fa2510b581df5fa2602ce42bacdccc61cae82a21b3e699954248352a7`**, build ID **`blackwire-v0.3.0-beta.3`**. There is one current runtime download, named **`Blackwire.elf`**. Source, relinking materials and the optional read-only PC summary collector are supporting files.

This update adds automatic **Blackwire Media shortcut setup** after loading the ELF, startup/preparation and removal repairs, clearer SMB access errors, and preparation/read optimizations. The broken top-right **Close browser** button is removed; use Circle on the console. Private startup tracing, launch timers and verbose Send logging are **disabled** in this release; useful on-screen errors remain.

On the recorded 13.00-family setup, the user loaded this exact ELF normally, prepared two extracted games, reached their menus/setup screens and uninstalled one. Automatic Media registration reported verified; the physical shortcut's visibility/opening still needs separate confirmation. Controllable gameplay has not been confirmed for this exact artifact. Earlier unexpected shutdowns remain unexplained and are not claimed fixed. This is a beta, with [precise results and limits](docs/TESTING.md#v030-beta3).

[Setup](docs/SETUP.md) · [Upgrade/test instructions](docs/BETA-TESTING.md) · [Requirements](docs/REQUIREMENTS.md) · [Changelog](CHANGELOG.md) · [Build from source](docs/BUILD.md) · [Project mirror](https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB)

## What changed since beta.2

- Set up or verify the Media shortcut automatically after a normal Payload Manager load, even before SMB settings exist. Busy or uncertain registration stays visible instead of being blindly retried.
- Repair startup discovery ordering and callback-owner lifetime handling. Keep preparation ownership checks before copying, and preserve valid retained compressed preparations during refresh/reload.
- Keep original game companions separate from the embedded reader; verify source Ampr and supported `fakelib`/`fakelib2` layouts rather than replacing them. Preserve strict refusal of unknown or changed files.
- Improve registration/removal error reporting and recover an independently verified completed removal without repeating platform Uninstall. Conflicting installations remain protected.
- Reduce compressed metadata requests with a bounded read window, reduce repeated preparation work and use the existing larger copy buffer. Payload cache size remains **64 MiB**.
- Name the failing SMB share, operation and stage. A missing share and a connected share with unreadable game files now give different guidance; failed checks keep saved settings.
- Retain supported MkPFS exFAT discovery, two shares, compressed artwork, library refresh, running-game warnings and the self-contained step-7 reader installer.

An earlier private same-source Alan Wake 2 comparison observed **151.772 → 40.328 seconds** for its copy phase (about **73% less time**). That is a diagnostic workload result, not a measured improvement for this exact normal ELF, whole preparation, Ethernet throughput or gameplay loading. See [performance evidence](docs/TESTING.md#unreleased-parser-only-preparation-profile).

## Tested environment

**Verified firmware scope: the 13.00 family, process-layout profile `0x13000040`.** This is the only firmware profile covered by our console testing. Firmware 10.01 and other profiles are currently unverified. A matching ShadowMountPlus version alone does not establish firmware compatibility.

The recorded reference environment is below; individual test dates and exact binaries are listed in [testing](docs/TESTING.md).

| Component | Recorded setup |
| --- | --- |
| PS5 firmware | 13.00 family (`0x13000040`) |
| Kstuff | Kstuff-lite `v1.11` |
| ShadowMountPlus | Stock `1.7beta7`, used in the latest diagnostic retest |
| ELF loader | elfldr `v0.27` in the latest captured payload inventory |
| ShadowMount library scan depth | `scan_depth=2` |
| Legacy external runtime files | The current beta installs its embedded reader. The older v0.2.0 preview required the optional [five-file global pack](docs/REQUIREMENTS.md#external-shadowmount-libraries). |
| Network | Wired Ethernet with an SMB share on an awake PC |

Blackwire requires a working jailbreak with Kstuff active before it is loaded.

Earlier baseline tests used ShadowMountPlus `1.7-beta5fix1` and elfldr `v0.26`; their results remain attributed to that earlier environment. The latest test inventory also records PS5 Payload Manager `0.6.3`, BFpilot `v0.4.4` and shsrv `v0.20`. Those are loading, file-management and development helpers, not additional game-streaming requirements.

The older v0.2.0 preview has a setup bug: it requires a particular optional global pack at `/data/shadowmount/fakelib`, even though Blackwire already embeds its reader. A missing pack can cause setup step 7 to fail after startup files transfer. This does **not** mean PS5 system files are missing. The current beta installer writes and verifies its embedded reader directly, preserves recognized existing per-game companions, and does not read or modify that global directory. See [repair scope](docs/RUNTIME-BUNDLE-DEVELOPMENT.md), [requirements](docs/REQUIREMENTS.md) and [test results](docs/TESTING.md).


## PC folder structure

The reference layout keeps extracted games and completed compressed images in two separate shared folders on the **same PC**, with the same SMB account:

```text
D:\
├── PS5Games\                    Windows share name: PS5Games
│   ├── Game A\
│   │   ├── eboot.bin
│   │   ├── sce_sys\param.json
│   │   └── ...complete extracted game files...
│   └── Game B\
│       └── ...complete extracted game files...
└── Compressed\                  Windows share name: Compressed
    ├── Grand Theft Auto V\
    │   ├── PPSA04263.ffpfsc
    │   └── PPSA04263.ffpfsc.vhash       keep the matching companion, if produced
    └── Resident Evil 7\
        ├── PPSA04405.ffpfsc
        └── PPSA04405.ffpfsc.vhash
```

The title IDs and filenames above illustrate the tested folder arrangement; preserve the actual filenames produced for your own images. Keep each matching `.vhash` beside its image when your compressor supplies it. Blackwire streams a completed supported image; do not point it at temporary compression outputs or move/change a source while it is being scanned, prepared or played.

| Blackwire Settings field | Value for this example |
| --- | --- |
| PC address | Your PC's LAN IPv4 address |
| Shared folder name | `PS5Games` |
| Second shared folder | `Compressed` |
| Username / password | The same Windows SMB account for both shares |

**Enter the Windows share name**, not `D:\Compressed`, a UNC path or the name of an unshared sibling folder. `Compressed` is the share name in this example; it is not a mandatory hard-coded name. A share called `PS5Compressed` must be entered as `PS5Compressed`. Sharing `PS5Games` alone does not expose its sibling `Compressed`.

A flat compressed share is also accepted by discovery; separate game folders are not a format requirement:

```text
Compressed\                      share this folder itself
├── Game A.ffpfsc
├── Game A.ffpfsc.vhash
├── Game B.ffpfsc
└── Game B.ffpfsc.vhash
```

An image's internal filesystem must still match the [supported formats](docs/EXFAT-DEVELOPMENT.md#admitted-layout). An `.ffpfsc` extension alone does not prove support. Discovery is read-only; Blackwire does not need PC write access. The SMB account needs **Read** under Sharing permissions and **Read/List/Read & execute** under Security/NTFS permissions, including the individual images and their parent folders. A directory can be listed while its images remain unreadable. See [Windows setup and access errors](docs/SETUP.md#windows-shared-folder).

## Console folder structure

Blackwire creates its own startup preparations; do not copy reader libraries or make replacement files by hand:

```text
/data/blackwire/
├── share.conf                   private SMB settings; never share publicly
├── games/                       Blackwire-owned startup copies and sparse files
└── ...owned indexes, operation records and state...

/data/homebrew/Games/            separate full local games, when configured in ShadowMount
└── <Game>/
    └── ...complete local game files...
```

The Payload Manager entry stores one **`Blackwire.elf`**; use its normal upload/install workflow and keep only one running Blackwire resident. Its storage location is managed by Payload Manager. `/data/homebrew/Games` is a separate local-game scan location, not the destination to manually fill Blackwire's streaming preparations. ShadowMount's depth is relative to its configured root: the recorded `scan_depth=2` reaches `Games/<Game>` from `/data/homebrew`; a root already pointing at `/data/homebrew/Games` differs. Preserve your unrelated roots and games.

## Install with PS5 Payload Manager

1. In Payload Manager, open **Settings → Manage Sources → Add Source**, then add:

   ```text
   https://raw.githubusercontent.com/XXD3V3L0P3RXX/BlackwireSMB/main/payloads.json
   ```

2. Refresh the source and select the single **Blackwire** entry, version **v0.3.0-beta.3**. Install/upload it as **`Blackwire.elf`**.
3. Close the running game normally, let pending operations finish, and stop the previous Blackwire resident in **Active processes** before loading the replacement. Deleting a stored ELF does not stop an already running resident. Keep one resident and one canonical payload entry.
4. Start your working jailbreak/Kstuff/ShadowMountPlus environment, then load the stored **`Blackwire.elf`** card once. Confirm `http://<PS5-address>:5918/health` reports **`blackwire-v0.3.0-beta.3`**.
5. Allow automatic Media setup to finish. Open **Blackwire** from PS5 Media, or use `http://<PS5-address>:5918/` in a LAN browser. The Media shortcut opens the running resident; it cannot load a missing ELF. Load the ELF again after reboot.

The direct [release download](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.3.0-beta.3) contains the same ELF and SHA-256 as the custom catalog. Dependencies are supplied separately. [Clean-reinstall instructions](docs/SETUP.md#deliberate-clean-reinstall-for-a-controlled-test) explain how to remove owned preparations without deleting original PC games, saves or unrelated applications. A clean reset is optional, not a required workaround for every error.

## Quick start

1. Share the complete extracted library and/or completed supported images using the [folder structure](#pc-folder-structure) above. Give the configured SMB account read access and keep the PC awake.
2. Load `Blackwire.elf` normally through Payload Manager. Let automatic Media setup finish; see its [waiting/refusal behavior](docs/SETUP.md#automatic-media-shortcut-setup).
3. Open Blackwire in Media or a LAN browser. Enter the **PC** address, share name, optional second share and SMB account in Settings. Choose **Save and check connection**. Both shares must pass before a two-share configuration replaces saved settings.
4. Select one game and choose **Send files**. Wait until startup preparation, reader verification and registration finish with **Ready**.
5. Return to the regular **PS5 Games dashboard** and launch the game's icon. Web **Play** is also available. Keep Blackwire running and the PC/share online throughout play.

Use **Refresh library** after adding, moving or removing source files. If Send or Refresh is blocked, read the named running-game warning and close that game normally. On PS5, Circle closes Details and returns from the main library to the dashboard; LAN browsers show keyboard hints. Fully close and reopen an old cached page after an upgrade.

## Preparation and storage

Send copies real executables and required startup data, builds the index and installs/verifies the embedded reader. Larger streamed files use sparse placeholders with their original logical lengths. Their apparent size is not allocated console storage; inspect allocated space/free space. Downloading a placeholder through a normal file manager can materialize zeros and is not a replacement for the PC original. See [preparation and removal](docs/PREPARATION.md).

No manually created `/data/shadowmount/fakelib` pack is required by the current installer. The old preview's missing-global-pack step-7 bug did not mean Sony system files were missing. Do not create empty libraries or overwrite game modules to bypass setup checks.

## Current limits

- Only the recorded **13.00-family** environment is console-qualified. The issue reporter's **10.01** environment remains unverified; no claim is made that every step-7/8/9 failure is repaired.
- The documented contiguous exFAT and unsigned D32 PFS/PFSC layouts are supported. Signed/encrypted PFS, fragmented/scattered allocations, wide PFS inodes and individually compressed inner files remain unsupported. Report an image's admission reason rather than assuming all `.ffpfsc` files share one format.
- Both shares use the same server and account. Share names and file access must match Windows configuration.
- Ready, a menu and controllable gameplay are different results. Earlier GTA V/RE7 gameplay applies to those earlier artifacts; current exact-artifact observations are recorded separately.
- Unknown/interrupted operations require their actual state to be resolved. Do not delete journals, registration records or unrelated installations to force a retry.
- Earlier unexpected poweroffs and intermittent removal refusals are not universally explained or fixed. No universal speed, stability or all-title guarantee is made.

<details>
<summary>Previous releases and rollback</summary>

[v0.3.0-beta.2](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.3.0-beta.2) remains available for rollback with its original `Blackwire-Beta.elf`, source and diagnostics. Older immutable releases remain on the [release history](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases). Stop the current resident before switching and do not manually downgrade an owned game's reader. Preserve original PC games and any unresolved operation evidence.

</details>

## Video and native Dashboard

[Watch the earlier Blackwire demonstration](https://youtu.be/xIPHo9kespo).

**Blackwire Dashboard** is a separate native interface under development. This release is the standalone browser resident; it does not install the native Dashboard or emulators. See the [roadmap](docs/ROADMAP.md#blackwire-dashboard).

## Source and contributions

[Build instructions](docs/BUILD.md) · [Contributing](CONTRIBUTING.md) · [Credits](CREDITS.md) · [Third-party notices](THIRD_PARTY_NOTICES.md) · [Security reporting](SECURITY.md)

## Disclaimer

Blackwire is intended for interoperability, research, preservation and compatibility purposes on systems and content that users are authorized to access.

Blackwire does not distribute retail game payloads, console firmware, cryptographic keys or proprietary console system libraries. A compatible jailbreak, Kstuff and ShadowMountPlus runtime must be supplied separately. The optional global library pack required by the older preview is no longer a reader-installation prerequisite in v0.3.0-beta.1. Users are responsible for ensuring that software, binaries and content used with this project are obtained and used in accordance with applicable laws and their respective license terms.

Blackwire is an independent project and is not affiliated with, authorized by or endorsed by Sony Interactive Entertainment. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.

## License

Original Blackwire integration and reader code are licensed under the **GNU General Public License version 3 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE).

Imported components retain their respective license terms; see [third-party notices](THIRD_PARTY_NOTICES.md). Preserve corresponding source, notices and practical static-library relinking material when distributing binaries.
