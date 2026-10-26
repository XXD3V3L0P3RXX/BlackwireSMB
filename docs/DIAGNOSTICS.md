# Send-files diagnostic build

**Testing the repaired formats and step 7? Use [v0.3.0-beta.2 and its test instructions](BETA-TESTING.md).** That beta includes these bounded setup logs, both original repairs and connection-settings recovery diagnostics. The older SendLog build documented below preserves the original reader and original failure behavior for comparison.

This optional test build investigates missing compressed candidates and the reported **Send files / Finish setup step-7 failure**. It adds detailed preparation logging to the v0.2.0-preview.1 standalone web resident. It does **not** claim to fix those reports, add compressed formats, or improve loading speed. The normal release and Payload Manager catalog remain on v0.2.0-preview.1.

- [GitHub diagnostic release](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.2.0-preview.1-sendlog.1)
- [Etawen diagnostic release](https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB/releases/tag/v0.2.0-preview.1-sendlog.1)
- Choose **Blackwire-SendLog.elf** for this diagnostic. **Blackwire.elf** on the same download page is the unchanged working release for normal use or rollback. Load only one resident. Source and relinking materials are provided or linked in the release notes, with SHA-256 checksums for the downloads.

## Install and reproduce once

1. Keep your normal `Blackwire.elf` available for rollback. Use your usual working jailbreak, Kstuff and ShadowMountPlus environment; the [normal requirements](REQUIREMENTS.md) still apply.
2. Close the running game normally. Wait for any Blackwire operation to finish. In Payload Manager's **Active processes**, stop the existing Blackwire resident before loading this diagnostic. Only one Blackwire resident may run at a time. The process can still appear as `blackwire.elf` even though the uploaded filename is `Blackwire-SendLog.elf`.
3. Upload and load `Blackwire-SendLog.elf` through your usual payload loader. Open `http://<PS5-address>:5918/health` in a LAN browser and check that `buildId` is **blackwire-v0.2.0-preview.1-sendlog.1**.
4. Keep the share and folder layout that produces the problem. To investigate several images at a share root, use one share with the optional second share blank, for example:

   ```text
   PS5Games/
     Game A.ffpfsc
     Game B.ffpfsc
     Game C.ffpfsc
   ```

   Keep any existing `.vhash` companions with their images. Do not move or replace files while they are being compressed, scanned, prepared or read by a running game.
5. Open `http://<PS5-address>:5918/`, choose **Refresh library**, and note how many images are present versus how many games appear. For a visible affected game, choose **Send files** once, or **Finish setup** if its startup files were already kept.
6. Wait for **Ready** or a failure. Record the exact message and approximate time. If an action is unavailable or fails, collect the evidence before uninstalling, restoring, deleting preparation records or repeatedly trying again. Those changes can erase the useful failure state.

There is no requirement to erase your library or reinstall games for this diagnostic.

## Collect the evidence

Use BFpilot, FTP or your usual console file manager to download:

```text
/data/blackwire/send-diagnostic.log
/data/blackwire/library.log
```

The first file records job progress, setup steps 2–13, directory-open paths and identities, and the exact syscall/path/error if a reader directory check fails. The second retains the ordinary resident log. The dedicated log appends across resident restarts and stops accepting entries at approximately **1 MiB**; it does not automatically rotate. If it reaches that size, save it, stop Blackwire while idle, rename only `send-diagnostic.log` to an archive name, then load the diagnostic again for a new capture.

For missing games, the optional **collect-diagnostics.py** release asset collects a compact read-only summary from the LAN interface. With Python 3 installed on the PC:

```text
python collect-diagnostics.py --ps5 <PS5-address> --output blackwire-diagnostic-summary.json
```

It only makes GET requests and selects diagnostic fields from `/health` and `/api/state`. It excludes saved SMB settings, credentials, the browser token and source bindings. It refuses to overwrite an existing output; use another filename for another capture. Python is needed only for this optional collector, not for using Blackwire.

Please include:

- The two logs and, if available, the collected summary.
- PS5 firmware, ShadowMountPlus version and Kstuff version.
- The version or download source of `mkpfs.exe` / Game Compressor, plus the packing command.
- The number of `.ffpfsc` images at the share root, whether they have `.vhash` companions, and whether the same files behave differently in separate subfolders.
- The exact error text or screenshot, whether startup copying finished, and whether this was Send files or Finish setup.

Review attachments before posting: logs and the summary can contain game titles, filenames and console paths. Do not attach passwords, configuration files, raw `/api/state` dumps, game images or proprietary console libraries. Report the diagnostic build ID with your results.

This build adds detailed **Send/setup** logging. It does not add a new scan parser or log every compressed-format parsing decision. The summary helps distinguish candidates rejected by the reader from entries shown in the dashboard.

## Return to the normal release

Close the running game, wait for pending operations, stop the diagnostic resident through Payload Manager, then load your saved v0.2.0-preview.1 `Blackwire.elf`. Settings and prepared games are kept. Its `/health` build ID is `blackwire-two-sources-dev.11`. Do not load both residents together or change autoload to run both.

## Verified scope

On October 24, 2026, three supported compressed images were detected together at a single SMB share root. Fresh RE7 and GTA V preparations completed on the unchanged public release. GTA V took about 93 seconds. Fresh RE4 preparation completed on this diagnostic in about 177 seconds, with both startup and dashboard readiness true; its internal step 7 took about 0.45 seconds. These are individual setup observations, not throughput benchmarks or gameplay qualification.

The reported step-7 failure did not reproduce with those three files. The test environment retained working ShadowMountPlus 1.7beta7 prerequisites. Larger collections, other environments and the reporter's exact files remain unverified.

The diagnostic ELF is **5,738,984 bytes**, SHA-256:

```text
f1c7d855dbd2392d20c38cad30819da58dd06ca5ffa0d926c24f0a9344680487
```

The checksum in `SHA256SUMS.txt` is authoritative. The embedded reader remains **831,168 bytes**, SHA-256 `c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025`, identical to v0.2.0-preview.1. No reader, ownership, directory-safety or retry policy was relaxed.

## Corresponding source and rebuilds

The diagnostic source is at tag `v0.2.0-preview.1-sendlog.1` and in the release's source ZIP. The five diagnostic runtime/build-file changes are inventoried in `metadata/diagnostics/sendlog-1.json`. The original release's source manifest is retained because its identity is embedded in the tested ELF. Build the explicitly recorded derivative with:

```sh
python3 tools/build.py --build --host-tests --name sendlog-local-01 --sdk /opt/ps5-payload-sdk --allow-source-changes
```

This uses the public toolchain described in [BUILD.md](BUILD.md). The opt-in flag admits the recorded logging changes; the builder still checks dependency inputs and verifies that inputs remain unchanged during compilation. New builds are separate artifacts until verified. The accompanying relinking archive was independently checked to reproduce the attached tested ELF byte for byte. No private logs, settings, credentials or game content are included in the release.
