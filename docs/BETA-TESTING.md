# Blackwire v0.3.0-beta.3 — upgrade and test instructions

[Current release](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/releases/tag/v0.3.0-beta.3) · [Setup](SETUP.md) · [Exact results and limits](TESTING.md#v030-beta3)

Use the single **Blackwire.elf** download. Its SHA-256 is `8cebe80fa2510b581df5fa2602ce42bacdccc61cae82a21b3e699954248352a7` (6,015,888 bytes); `/health` must report **`blackwire-v0.3.0-beta.3`**. This is the standalone browser resident, not native Dashboard. Private startup tracing, launch timing and verbose Send logging are disabled.

## Upgrade through Payload Manager

1. Preserve original PC games, matching image companions and any unresolved failure evidence. Close the game normally and wait for pending operations to finish.
2. Stop the old Blackwire resident in Payload Manager's **Active processes**. Removing a stored file does not stop a running process. Keep only one Blackwire resident and one stored **`Blackwire.elf`** entry.
3. Refresh the Blackwire custom source, select **v0.3.0-beta.3**, and install/upload the verified `Blackwire.elf`. Start the normal compatible jailbreak, Kstuff and ShadowMountPlus environment, then load that stored card once. No direct-memory test sender, separate helper or manually copied global library pack is required.
4. Check `http://<PS5-address>:5918/health`. If its build ID differs, stop and resolve the stale resident rather than testing the wrong file. Fully close/reopen an older browser page.
5. Allow automatic Media setup to finish. It can run before SMB settings exist. If waiting, close the running game normally; do not reload another resident to force registration. Check the [Media status guidance](SETUP.md#automatic-media-shortcut-setup) if refused or uncertain.
6. Open Blackwire from Media or `http://<PS5-address>:5918/`. Review saved settings, use the actual Windows share names and check both share-level and file/NTFS Read access. A failed connection/catalog check preserves the previous settings. See the [folder diagrams](../README.md#pc-folder-structure).

An ordinary upgrade need not erase every preparation. For a deliberate fresh test, use the [clean-reinstall procedure](SETUP.md#deliberate-clean-reinstall-for-a-controlled-test): uninstall each owned streamed copy normally, verify removal, stop Blackwire, remove only its stored payload/data, then install the canonical ELF. Preserve saves, original PC files and unrelated apps. Do not erase an uncertain operation's records to bypass its refusal.

## Test one affected title

1. Keep the original share layout and source images unchanged. A flat folder of completed images is supported by discovery; separate folders are not a required workaround. Choose **Refresh library** once and record total/accepted/rejected image counts.
2. Select one affected game. Use **Finish setup** if valid startup files were retained, or **Send files** for a fresh copy. Wait for Ready or one clear failure. Record the exact setup step, stage and reason; do not keep resubmitting an uncertain action.
3. If Ready, launch its ordinary PS5 Games icon or web Play. Distinguish **Ready**, **menu/setup screen** and **controllable gameplay** in the result.
4. Close normally. If performing another clean test, uninstall the selected Blackwire-owned preparation through Details and verify its terminal result before switching sources/builds.

Known repairs include the old optional-global-pack step-7 dependency, supported contiguous MkPFS exFAT admission, original game-companion handling, startup ordering, retained compressed state and clearer registration/removal checks. They do not establish universal compatibility or a repair for every reported step-7/8/9 error. Only the recorded 13.00-family environment is console-qualified; firmware 10.01 remains unverified.

## Collect a small read-only result

The release includes **collect-diagnostics.py** as an optional PC tool; Python is needed only to run this collector, not to play games:

```text
python collect-diagnostics.py --ps5 <PS5-address> --output blackwire-beta3-summary.json
```

It reads `/health` and `/api/state`, summarizes build/reader identity, settings status, image admission counts/reasons and per-title preparation reasons, and excludes credentials, tokens and source bindings. Review game names and messages before sharing. Attach that summary plus the exact visible error and firmware/Kstuff/ShadowMountPlus versions to the issue. A screenshot or copied message is useful if the collector cannot run.

This **normal ELF does not create the private verbose `send-diagnostic.log`** used by earlier diagnostic builds. Do not delete settings or expect a nonexistent log. Never post `share.conf`, raw state dumps, passwords, tokens, game files, saves, console databases or proprietary libraries. If the summary cannot explain a failure, a separately identified diagnostic build may be needed; do not confuse it with this release.

## Rollback and limits

Previous immutable releases remain available. Stop the running resident before switching, retain its matching source/reader provenance, and do not manually downgrade owned reader files. A Media shortcut needs the resident running. The batch public-SDK fallback can refresh other registered apps' metadata; see [Media setup](SETUP.md#automatic-media-shortcut-setup). Unexpected shutdowns and intermittent removal uncertainty remain under investigation. Current exact-artifact tests reached menus/setup screens; gameplay and controlled speed comparisons remain separate checks.
