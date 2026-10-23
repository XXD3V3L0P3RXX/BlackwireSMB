# Blackwire

Stream PS5 games from an ordinary PC SMB share, with a smaller startup copy on the console. Supports extracted folders and the tested PFS/PFSC compressed layouts, including supported `.ffpfsc` images.

[Download v0.2.0-preview.1](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.2.0-preview.1) · [Etawen mirror](https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB) · [Changelog](CHANGELOG.md) · [Video](#video) · [Setup](docs/SETUP.md) · [Requirements](docs/REQUIREMENTS.md) · [Test results](docs/TESTING.md) · [Build from source](docs/BUILD.md)

Blackwire runs as a single `Blackwire.elf` on a compatible jailbroken PS5. Its web interface works in the PS5 browser and a LAN browser. The game runs on the PS5; bulk game files stay on the PC. An ordinary Windows shared folder is enough—there is no Blackwire PC service to install.

## v0.2.0-preview.1

- Stream supported PFS/PFSC compressed images without a PC decompression service.
- Scan **two SMB shares on the same PC** with the same sign-in details, keeping extracted and compressed games in separate folders.
- Read game artwork directly from supported compressed images.
- Finish setup with known earlier Blackwire readers while retaining valid startup files. Unknown reader binaries remain refused.
- Use **Refresh library** to rescan the shares after adding or moving games.
- See which running game must be closed when Send files or Refresh is blocked.
- Use PS5 controller hints on the console. Circle was confirmed to close Details and return from the main library to the PS5 dashboard.

GTA V and Resident Evil 7 reached controllable gameplay from compressed sources in earlier tests. GTA V also passed a direct Games-icon driving test. The current combined-library reader reached both initial menus; its later gameplay has not been repeated for this exact package. These are separate observations, recorded in [testing](docs/TESTING.md#compressed-and-two-share-preview).

The update reduces unchanged-source launch verification work, indexes the existing 64 MiB payload cache and batches adjacent PFSC reads. Controlled PS5 loading-time benchmarks are still pending. Background Game Compressor CPU contention contributed to earlier failures; it has not been established as the sole cause. See [compressed-source results](docs/COMPRESSED-DEVELOPMENT.md).

**This is an early preview.** The extra Close browser button still stayed open in the console test; use Circle instead. That redundant button is scheduled for removal in the next build. Broader compatibility, interrupted-operation recovery and hardware Uninstall qualification remain open.

## Video

[Watch the Blackwire demonstration on YouTube](https://youtu.be/xIPHo9kespo).

## Blackwire Dashboard — in development

We are already working on a full native **Blackwire Dashboard**: a cover browser with controller navigation and a shared library for streamed PC games and existing local console games. Planned settings, preparation progress and launch controls will bring those workflows into the native interface.

Dashboard is a separate product under development. This standalone preview uses the web interface; it does not include the native Dashboard installer or interface. Follow the [roadmap](docs/ROADMAP.md#blackwire-dashboard) for the planned work. No release date is announced.

## Install with PS5 Payload Manager

Blackwire is available through this project's custom source. Add it once in [PS5 Payload Manager](https://github.com/itsPLK/ps5-payload-manager):

1. Open **Settings → Manage Sources → Add Source**.
2. Paste this direct catalog URL and choose **Add**:

   ```text
   https://raw.githubusercontent.com/XXD3V3L0P3RXX/BlackwireSMB/main/payloads.json
   ```

3. Select **Blackwire** from the added source, install it, then load `Blackwire.elf` after starting the required jailbreak, Kstuff and ShadowMountPlus environment.
4. Open `http://<PS5-address>:5918/` and follow the [quick start](#quick-start) to connect your SMB share and prepare games.

The catalog points to the published preview ELF and supplies its SHA-256 checksum for Payload Manager to verify. This is an optional custom source, separate from Payload Manager's default catalog. It installs Blackwire; the [required dependencies](docs/REQUIREMENTS.md) must be supplied separately. The [release download](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.2.0-preview.1) remains available for manual loading.

## Quick start

1. Share a parent folder containing complete extracted game folders or supported compressed images on your PC. You can share a separate compressed folder and enter it as the optional second SMB share. Give the same SMB account read access to both and keep the PC awake. Follow the [share guide](docs/SETUP.md#windows-shared-folder).
2. Start your usual working jailbreak environment with Kstuff and stock ShadowMountPlus `1.7-beta5fix1`. Set ShadowMount's **Library scan depth to 2** (`scan_depth=2`) for the layout in this guide, then load `Blackwire.elf`. See the [scan-root explanation](docs/SETUP.md#shadowmount-discovery) and [requirements](docs/REQUIREMENTS.md).
3. Open `http://<PS5-address>:5918/` in a LAN browser, or `http://127.0.0.1:5918/` in the PS5 browser. In Settings, enter the PC address, share name, optional second share and SMB account, then choose **Save and check connection**. Both shares must pass the check before a two-share configuration is saved.
4. Select a game and choose **Send files**. Wait for its preparation and registration to finish and show **Ready**.
5. **Recommended:** return to the regular PS5 **Games** dashboard and start the prepared game from its icon. Web **Play** is also available. **Blackwire must remain running, and the PC share must stay online.**

Load the ELF again after a console reboot. Fresh settings start blank. Previously saved address, share and username can be shown; a fixed marker indicates a saved password without returning its bytes to the page.

If Send files is blocked, read the message beside the selected game and close the named running game normally. Use **Refresh library** after changing the contents of a share. On PS5, press Circle to leave Details, then Circle at the main library to exit the browser. When upgrading from an older UI test build, fully close and reopen the browser once so its old navigation history is discarded; no console restart is required for that UI reset.

The optional blue **Media** shortcut opens the running Blackwire page. Its authenticated, same-ELF setup action is qualified on the tested console, but automatic creation on a fresh startup is not integrated in this preview. See [optional Media shortcut setup](docs/SETUP.md#optional-media-shortcut-setup).

## Preparation and storage

The first Send copies real executables and required startup data, builds an index, and prepares the reader and game registration. Size, file count, PC storage and network conditions determine the wait; a large title can take several minutes or longer. The game's own loading time is a separate wait.

Bulk files are represented by sparse placeholders with their original logical lengths. A console folder can appear very large while using far fewer allocated blocks. Check allocated storage or free space, rather than adding logical file sizes. Downloading a placeholder through a normal file manager can materialize a full-sized file of zeros; it is not a replacement for the PC original. Read [how preparation works](docs/PREPARATION.md).

## Current limits

No title-specific compatibility blocker has been identified in testing so far. Coverage remains limited; individual startup, menu and gameplay observations are recorded separately.

- Extracted folders and supported unsigned contiguous D32 PFS/PFSC layouts are accepted. An `.ffpfsc` extension alone does not establish support. exFAT-based compressor output, signed/encrypted PFS, scattered extents, wide inode layouts and individually compressed inner files remain unsupported.
- The two shares must use the same PC/server and credentials. A folder outside either shared root is not exposed automatically.
- Wired Ethernet is the tested network path. No minimum speed guarantees or universal loading-time improvement are claimed.
- Some tested games have slow menu or scene loading even when gameplay is normal. No universal speed improvement or all-game compatibility is claimed.
- Ready confirms the selected startup preparation and registration checks; it does not prove that every stage of a game works.
- The resident and SMB share are required for game-icon launches. A Media shortcut cannot load an absent resident.
- Unknown or interrupted operations need their actual status checked. Refresh does not grant permission to discard ownership records or repeat an uncertain action.

[Current status](docs/STATUS.md), [recorded results](docs/TESTING.md) and the [roadmap](docs/ROADMAP.md) distinguish observed behavior from planned work.

## Source and contributions

[Build instructions](docs/BUILD.md) · [Contributing](CONTRIBUTING.md) · [Credits](CREDITS.md) · [Third-party notices](THIRD_PARTY_NOTICES.md) · [Security reporting](SECURITY.md)

## Disclaimer

Blackwire is intended for interoperability, research, preservation and compatibility purposes on systems and content that users are authorized to access.

Blackwire does not distribute retail game payloads, console firmware, cryptographic keys or proprietary console system libraries. A compatible console runtime, including the [external libraries listed in the requirements](docs/REQUIREMENTS.md#external-shadowmount-libraries), is required and must be supplied separately. Users are responsible for ensuring that software, binaries and content used with this project are obtained and used in accordance with applicable laws and their respective license terms.

Blackwire is an independent project and is not affiliated with, authorized by or endorsed by Sony Interactive Entertainment. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.

## License

Original Blackwire integration and reader code are licensed under the **GNU General Public License version 3 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE).

Imported components retain their respective license terms; see [third-party notices](THIRD_PARTY_NOTICES.md). Preserve corresponding source, notices and practical static-library relinking material when distributing binaries.
