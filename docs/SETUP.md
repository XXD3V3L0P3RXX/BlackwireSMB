# Set up Blackwire

The ordinary flow is **Send files → Ready → PS5 Games icon**. Keep the Blackwire resident running and the PC share online throughout play. Use the [tested runtime](REQUIREMENTS.md), and check the [preview's results and limits](TESTING.md).

The current published test build is **v0.3.0-beta.2**. Both the direct download and the Payload Manager custom source offer that same runtime. The unreleased development candidate adds **Payload Manager load → automatic Media setup → SMB configuration** for a fresh install. Focused current-source host checks passed for that automatic path; it remains unverified on the console and is not included in the published download. Supported sources remain extracted folders, the documented PFS/PFSC layouts and contiguous exFAT images. Read the [PFS/PFSC limits](COMPRESSED-DEVELOPMENT.md#formats) and [exFAT scope](EXFAT-DEVELOPMENT.md#admitted-layout); a `.ffpfsc` suffix alone is insufficient.

## Windows shared folder

1. Put complete extracted games in separate folders below a parent such as `D:\PS5Games\<Game>`. Supported compressed images can stay in a separate parent such as `D:\Compressed\<Game>\<Title>.ffpfsc`. Preserve the source files and directory structure.
2. On your trusted LAN, use the Windows Private network profile and enable File and printer sharing. Connect the PC and PS5 by wired Ethernet where possible. Keep the PC awake.
3. Choose an account with an account password. A Windows Hello PIN is not an SMB password. Blackwire does not create a Windows account.
4. Open the parent folder's **Properties → Sharing → Advanced Sharing**, enable sharing, and choose a share name such as `PS5Games`. Enter that share name in Blackwire, rather than the Windows drive path.
5. Give the chosen account **Read** in share permissions. In the **Security** tab, give it **Read**, **List folder contents**, and **Read & execute**. Both the share and filesystem permissions apply; Blackwire does not need PC write access.
6. Run `ipconfig` on the PC and find the Ethernet adapter's IPv4 address. Enter that PC address, the share name, username and account password in Blackwire Settings. The browser URL uses the PS5 address; the PC address field uses the PC address.
7. Choose **Save and check connection**. **Refresh connection status** observes that check without saving again. The displayed connection result records the last check; it is not a continuous reachability guarantee. Settings are kept in `/data/blackwire/share.conf`. Save and Check completes library discovery before writing them; a failed image scan can prevent a save. Preserve the file and collect the exact message rather than deleting settings. In beta.2, if a resident replacement interrupts the browser's pending check, use **Reload settings status** and review the draft before explicitly saving again.

There is no Blackwire Python, Node or background service to install on the PC. Fresh configuration fields are blank. Saved nonsecret values can be displayed later, with a fixed `********` marker for a saved password. Password bytes are not returned to the browser.

### A listed image still needs file Read access

Windows applies two permission layers to the same SMB account: the shared folder's **Sharing → Permissions** and the file's **Security** permissions. Share-level **Read** can allow a directory listing while an individual `.ffpfsc` image remains unreadable because of its NTFS access rules. Files moved from another folder can retain different permissions; check the exact file named in the error, rather than assuming every image inherited its current parent's access.

In that file's **Properties → Security → Advanced → Effective Access**, check the account entered in Blackwire. It needs Read access to the image and access through its parent folders; Blackwire does not need Full control or PC write access. Review any denied or missing access with the folder owner before making a narrowly scoped change. Blackwire does not automatically change Windows permissions.

The unreleased diagnostics distinguish SMB sign-in/share access from a connected share whose image open, metadata or read operation fails. Keep the displayed share, file, stage, operation and transport result. An access refusal is not proof that the image needs repacking or that console libraries need deleting. After resolving the exact file access, explicitly check or refresh once and inspect the result. A failed connection/library check keeps the previously saved settings.

## Optional second share

Share both parent folders on the same PC and give the same account read access to both. For example:

| Blackwire setting | Share name | Example PC folder |
| --- | --- | --- |
| Shared folder name | `PS5Games` | `D:\PS5Games` |
| Second shared folder | `PS5Compressed` | `D:\Compressed` |

Enter share names, not drive paths. Save and check connection reads both shares before saving; a failed check keeps the previous configuration. Leave the second field blank to use one share. Existing single-share settings still work. Separate servers or separate credentials per share are not supported.

Each copy retains its own share, path and preparation identity. Equal titles in different shares remain separate sources. A sibling folder outside the configured shares is not automatically reachable. Moving an existing source changes its identity; refresh the library and follow the selected copy's preparation status.

## Load the console resident

1. Check the [tested firmware and runtime versions](../README.md#tested-environment) and whether your ELF has the [released-preview dependency or development repair](REQUIREMENTS.md#external-shadowmount-libraries), before preparing a game. Start the working jailbreak chain with Kstuff and compatible stock ShadowMountPlus active. Only the 13.00-family profile `0x13000040` is console-qualified; 10.01 and other firmware profiles are unverified.
2. Stop the old resident first and load only one. Upload the chosen verified ELF through Payload Manager, using the canonical stored name **`Blackwire.elf`**, then launch that stored card. The published beta direct download is named `Blackwire-Beta.elf`; save it as `Blackwire.elf` before uploading. Confirm `http://<PS5-address>:5918/health` matches the selected build; the published beta reports `blackwire-v0.3.0-beta.2`. The unpublished development candidate is identified separately in [testing](TESTING.md#october-27-final-publication-candidate). Repeat the normal Payload Manager load after reboot.
3. In the development candidate, let automatic Media setup finish. It checks the existing shortcut or registers the blue **Blackwire** entry without requiring saved SMB settings. If it is waiting for an idle console, close any running game normally and wait; do not reload the ELF to force another attempt. See [status and limits](#automatic-media-shortcut-setup).
4. Open **Blackwire** from PS5 **Media**, or open `http://<PS5-address>:5918/` in a LAN browser. The console page is `http://127.0.0.1:5918/`.
5. Configure the share and let library discovery finish. Choose a game, select **Send files**, and wait for that named job to finish with Ready status.
6. Open the game's ordinary icon in the PS5 **Games** area, or use web **Play**. Blackwire handles that game's startup handoff and tracks the session.

A completed Send includes the per-game reader and registration checks. If files are retained but setup is incomplete, the page can offer **Finish setup** or **Finish setup and play**. Follow the selected game's status; retained files do not automatically grant Ready. An unchanged, valid Ready game uses bounded startup checks rather than a complete new source-tree scan.

An existing title can retain console app metadata even after its source folder disappears from ShadowMount. That state must be resolved through normal uninstall before a fresh registration; it is not fixed by adding replacement files. The unreleased revision keeps its refusal visible and can separately remove a verified Blackwire startup copy without touching the existing installation. See [incomplete setup and removal](PREPARATION.md#existing-console-metadata-and-incomplete-setup).

The selected game card shows its preparation progress, current session and any error. Game-operation notices stay with that card rather than appearing as floating notifications across the page.

## Deliberate clean reinstall for a controlled test

These steps reset Blackwire, rather than merely replacing a running resident. Keep original PC games and saved data.

1. Close the running game normally and let pending operations finish. In Blackwire, use each selected streamed copy's **Details → Uninstall**, then wait for its terminal result. An empty ShadowMount list alone does not prove removal. If Uninstall refuses or its outcome is unknown, retain that result and diagnose it before deleting Blackwire's records.
2. Verify that the selected owned preparation and its game registration are gone. Do not delete a different installation with the same title, a saved-game directory, or the console database. **Remove startup files only** deliberately preserves an existing installation and is not equivalent to full Uninstall.
3. Stop the old resident through Payload Manager's **Active processes**. Remove the Blackwire stored payload entry through Payload Manager; preserve unrelated payloads. For the development candidate, use one canonical stored **`Blackwire.elf`**, not a growing set of differently named test ELFs.
4. After owned game removal is confirmed and Blackwire is stopped, remove its own `/data/blackwire` test directory through the file manager if a complete settings/history reset is intended. Keep any requested diagnostics on the PC first. This clears the SMB setup and Blackwire history; it does not repair an unrelated platform registration or a refusal whose outcome is unresolved.
5. Upload the chosen verified `Blackwire.elf` in Payload Manager and launch its stored card once. For the development candidate, wait for automatic Media setup, then open the shortcut or browser and check the exact build/artifact identity. Enter the existing SMB settings and use normal **Send files → Ready → Play**. Do not create replacement libraries, mount links or registration receipts by hand.

When the resident is deliberately stopped, its port 5918 page is expected to be offline. Check Payload Manager or the file manager independently before treating that as a console failure. Private startup traces, launch timers and verbose Send logs are test-build options; the October 27 normal candidate excludes them.

## Refresh and console controls

Use **Refresh library** beside the filters after adding, moving or removing PC source files. It rescans both configured shares and reports scanning, completion or a refusal. It is separate from Refresh connection status, which checks connectivity. A local console game can still appear as a console entry after its PC source is removed.

If Send files or Refresh is blocked by a running game, the page names that game, for example **Close Resident Evil 4 to send files**. Close it normally and allow session status to settle. A disabled action is not permission to resend or remove its preparation manually.

The PS5 page shows controller hints; the PC page keeps keyboard hints. Circle was confirmed to close Details and, at the main library, return to the PS5 dashboard. The published build's extra Close browser button does not work in the tested console browser; use Circle. The unreleased source removes that button, with current-candidate hardware qualification still pending. A browser that retained development-9/10 history must be fully closed and reopened once; reloading the page alone does not clear those entries.

## Background payloads during testing

Keep unnecessary background payloads closed while comparing behavior. In the recorded test, the separately running Game Compressor used about 96% of one CPU core while idle. After it was stopped, unchanged-reader comparison launches completed without the earlier common-dialog watchdog error. This supports CPU contention as a contributor; it does not establish RAM exhaustion or a universal fix. See the [comparison record](COMPRESSED-DEVELOPMENT.md#background-compressor-comparison).

<a id="optional-media-shortcut-setup"></a>

## Automatic Media shortcut setup

The development candidate schedules one automatic setup worker after the ordinary browser-open step returns. Load the single stored **`Blackwire.elf`** through Payload Manager, let Media setup finish, then open the blue **Blackwire** entry and configure SMB. Fresh missing SMB settings do not prevent this setup. No PowerShell command, helper ELF or manually created file is required. Focused current-source host checks passed for this new automatic flow. Actual automatic registration and Media visibility remain unverified on the console; the earlier explicit-action result below does not qualify them.

The worker checks for a safely idle console every 250 ms within a 90-second startup window. While it waits, `/health` reports `mediaSetupStatus: automatic_setup_waiting`; after it obtains the reservation, it reports `automatic_setup_running`. Close a running game normally and wait for status to settle. Do not repeatedly reload Blackwire or submit another registration request. If the bounded wait refuses, or the outcome is unconfirmed or ownership is held, preserve the reason and resolve that state before another attempt. A timeout does not prove that registration failed or make an uncertain platform call safe to repeat.

`GET /api/media/public-sdk-status` observes the same worker without starting another action. Success is `phase: verified`, `registered: true`, `running: false` and `reservationHeld: false`; confirm the shortcut is visible in PS5 **Media** as a separate console check. An accepted worker or a pending status is not successful registration. The shortcut opens the running resident's page; it does not load an absent `Blackwire.elf`. Load the resident through Payload Manager again after reboot.

The existing public-SDK setup first requires a stable application-directory inventory, positive registration coverage for every other application and confirmed idle status. It refuses incomplete or changing observations. An existing Blackwire row skips installation. Otherwise it chooses the per-title export if available, or `sceAppInstUtilAppInstallAll(NULL)`. This fallback is a **batch scan**, not a targeted install: the earlier explicit-action test used `mode: public_batch` and refreshed other registered applications' time/JSON metadata. Setup makes one registration call, then observes its own row for up to 30 seconds after the call returns. The platform call itself has no cancellable timeout. Blackwire does not edit the metadata database directly or replace ShadowMountPlus.

### Maintenance action for earlier builds

The authenticated `POST /api/media/register-public-sdk` action remains available for deliberate maintenance on builds that provide it. It is not a required installation step for the automatic startup candidate. It requires an idle resident and the current action token; do not submit it while automatic setup is pending or running, or replay it after an uncertain result. Its earlier same-ELF console proof establishes the public-SDK path, not the new automatic startup ordering.

## Existing full console games

ShadowMount discovers full console game folders through its configured scan roots. Use the discovery depth below for the folder layout in this guide.

## ShadowMount discovery

Set ShadowMount's **Library scan depth to 2** (`scan_depth=2`) for the folder layout used here. This value was read back from the tested console's configuration.

Discovery depth is relative to each configured scan root: depth 2 reaches `/data/homebrew/Games/<Game>` from `/data/homebrew`. A root already pointing at `/data/homebrew/Games` has a different depth requirement. Preserve unrelated roots and games, and change runtime settings only while idle. This is local-folder discovery, separate from Blackwire's SMB preparation and registration.

## If something is incomplete

Keep the selected game, operation status and detailed setup stage/reason. If a copy's valid files remain, use **Finish setup** when offered; the updated resident has completed that GTA recovery without copying the files again. If the prepared folder is positively gone, normal status checks can return the source to **Send files** when safe. Unknown or interrupted operations still need their actual status resolved.

Refresh can rediscover files and observe a known preparation; it cannot turn missing files, an uncertain mutation or an unresolved reader update into a safe success. Do not delete journals or registrations to bypass a refusal. Ordinary streamed-copy Uninstall preserves the PC originals and saved data; see [preparation and removal](PREPARATION.md#uninstall-and-restore). Hardware qualification of the updated old-registration Uninstall recovery is still pending.

## Diagnostic preparation failures

The optional `v0.2.0-preview.1-sendlog.1` release has [separate diagnostic instructions](DIAGNOSTICS.md), including the exact artifact identity, build command, log collection and rollback. The normal preview remains unchanged.
