# Blackwire v0.3.0-beta.2 — test instructions

This standalone browser beta repairs startup settings reporting and recovery after a resident replacement. It also retains the repairs for the two original faults reported in issue #2: MkPFS images containing exFAT were rejected, and setup step 7 incorrectly required an optional global library pack. The beta reads supported exFAT layouts and installs its embedded streaming reader without opening or changing `/data/shadowmount/fakelib`. This is not a native Dashboard release.

[GitHub download](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.3.0-beta.2) · [Project mirror](https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB)

## Install and test once

1. Download **Blackwire-Beta.elf**, **BETA-TESTING.md** and, optionally, **collect-diagnostics.py**. Keep your previous ELF available. Leave your original images, matching `.vhash` files, share permissions and folder layout in place. Do not erase existing preparations or repack the library for this test.
2. Close any running game normally and wait for pending operations to finish. Stop the old Blackwire resident through Payload Manager's Active processes before loading the beta. Run only one Blackwire resident. Use your existing jailbreak, Kstuff and ShadowMountPlus setup; no firmware change or manually copied global library pack is requested.
3. Load **Blackwire-Beta.elf** through your normal ELF loader. Open `http://<PS5-address>:5918/health` and confirm `buildId` is **blackwire-v0.3.0-beta.2**. The process can still appear as `blackwire.elf`; identify the version using this health check.
4. Reopen or reload the browser page after replacing the resident. Existing settings should appear while discovery runs. If the page reports that Blackwire restarted during a connection check, choose **Reload settings status** once; review the preserved draft before explicitly submitting it. Do not remove `share.conf` or delete preparations. Choose **Refresh library** once with the original flat share layout, for example `PS5Games/Game A.ffpfsc`, `PS5Games/Game B.ffpfsc`, and other completed images together. Record how many images are present and how many candidates are accepted. Supported compressed exFAT images are labeled `pfsc_exfat`; PFS images remain supported.
5. Select one affected game. If its startup files were retained, use **Finish setup**; otherwise choose **Send files**. Wait for Ready or one clear failure. The previous missing-global-folder step-7 failure should no longer require any external pack. Unknown/altered game companions still refuse safely rather than being overwritten.
6. If it becomes Ready, try launching from the regular PS5 Games tile or web Play. Report whether it reaches a menu and then controllable gameplay. If it fails, collect the evidence below before uninstalling, resetting records or repeatedly trying the same operation.

An `.ffpfsc` extension alone does not establish format support. The [supported exFAT layouts and checks](EXFAT-DEVELOPMENT.md#admitted-layout) remain bounded; rejected layouts should be reported with their diagnostic reason. Keep games and images unchanged while scanning, preparing or playing.

## Collect the result

The beta includes the bounded Send/setup logger. Download these files with your usual console file manager:

```text
/data/blackwire/send-diagnostic.log
/data/blackwire/library.log
```

The dedicated log records preparation progress, setup steps 2–13, directory checks and the first failing operation/path/error. Beta.2 also records settings-file refusal categories, connection-check start/end and failures during SMB connection, catalog parsing or artwork. The optional summary includes `settingsFile` and `storageStatus` without exposing credentials. It appends across restarts and is capped at about 1 MiB. If an older log has filled the cap, save it first, stop Blackwire while idle and rename only that log to an archive filename before a new capture. An unavailable logger must not grant retry or setup authority.

The optional collector uses Python on the PC and makes read-only GET requests:

```text
python collect-diagnostics.py --ps5 <PS5-address> --output blackwire-beta-summary.json
```

It selects diagnostic fields and excludes SMB settings, credentials, the browser token and source bindings. Python is needed only for this optional helper; normal Blackwire use still requires no PC service. Use a new output name for each capture.

Attach the summary and the two logs, after reviewing titles and paths, together with:

- The beta build ID, firmware, Kstuff and ShadowMountPlus versions.
- Packing tool/version/command, image count and the exact share layout.
- Which images appeared, whether Finish setup or Send files was used, and the exact final message.
- If Ready: menu/gameplay result. If it failed: the failing stage and approximate time.

Do not attach passwords, configuration files, raw `/api/state` dumps, game images or console library binaries. Existing [diagnostic collector details](DIAGNOSTICS.md#collect-the-evidence) also apply. The reporter's exact library and payload combination still need their own test; [recorded results](TESTING.md) distinguish fresh setup, initial menus and gameplay.

## If Save and Check fails

The connection is committed only after the library check succeeds. Image discovery or artwork failures can therefore prevent a new configuration file from being written. Keep existing settings and the failing evidence; do not repeatedly delete files or force a save. Report the exact message, whether fields remain disabled, whether you used a PC or PS5 browser, the build ID and the collector's `setup` fields with the logs above. This distinguishes a file-read refusal from a failed connection or image scan.

Game data is decompressed on the PS5. The PC serves SMB reads, so low PC CPU use does not by itself show that extraction failed. Measured traffic during metadata discovery or startup-file copying is not a raw link-speed test. This beta does not add a PC cache/service or claim a measured loading-speed improvement.

A separate message saying **“The exact frontend or preparation idle authority could not be retained”** is a refusal before preparation is accepted. It does not identify a corrupt game or establish that a game is actually running. Close any running game normally and check the payload inventory and build ID. If it repeats once, retain the reviewed Blackwire logs and ShadowMountPlus lifecycle evidence (`/data/shadowmount/debug.log`), with firmware/payload versions and whether the browser was on PC or PS5. Do not erase logs or preparations to force the idle check to pass. This exact refusal is still under investigation.

## Scope and rollback

Our recorded console environment is the 13.00 family with Kstuff-lite and stock ShadowMountPlus. The beta is intended to test the repaired faults on other existing setups, not to require that they copy our optional global pack. Keep ShadowMount's per-game overlay enabled and the configured library scan depth appropriate for the prepared-game layout; our setup uses `scan_depth=2`.

Close the game and stop the beta before switching residents. The previous preview downloads are preserved, but an older reader does not gain exFAT support and may not recognize preparations made with a newer reader. Keep the matching beta available and preserve preparation records; do not manually downgrade reader files or assume arbitrary interrupted operations are safe to repeat.
