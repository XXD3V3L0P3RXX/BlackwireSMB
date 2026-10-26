# Set up Blackwire

The ordinary flow is **Send files → Ready → PS5 Games icon**. Keep the Blackwire resident running and the PC share online throughout play. Use the [tested runtime](REQUIREMENTS.md), and check the [preview's results and limits](TESTING.md).

The current test build is **v0.3.0-beta.2**. Both the direct download and the Payload Manager custom source offer that same runtime. It accepts extracted folders, the supported PFS/PFSC layouts and the documented contiguous exFAT images. Read the [PFS/PFSC limits](COMPRESSED-DEVELOPMENT.md#formats) and [exFAT scope](EXFAT-DEVELOPMENT.md#admitted-layout); a `.ffpfsc` suffix alone is insufficient.

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
2. Load the current test beta using your normal ELF loader: `Blackwire-Beta.elf` for a direct download, or `Blackwire.elf` from Payload Manager. They are the same binary. Stop the old resident first and load only one. Confirm `http://<PS5-address>:5918/health` reports `blackwire-v0.3.0-beta.2`. Repeat loading after reboot.
3. Open `http://<PS5-address>:5918/` in a LAN browser. The console page is `http://127.0.0.1:5918/`.
4. Configure the share and let library discovery finish. Choose a game, select **Send files**, and wait for that named job to finish with Ready status.
5. Open the game's ordinary icon in the PS5 **Games** area, or use web **Play**. Blackwire handles that game's startup handoff and tracks the session.

A completed Send includes the per-game reader and registration checks. If files are retained but setup is incomplete, the page can offer **Finish setup** or **Finish setup and play**. Follow the selected game's status; retained files do not automatically grant Ready. An unchanged, valid Ready game uses bounded startup checks rather than a complete new source-tree scan.

The selected game card shows its preparation progress, current session and any error. Game-operation notices stay with that card rather than appearing as floating notifications across the page.

## Refresh and console controls

Use **Refresh library** beside the filters after adding, moving or removing PC source files. It rescans both configured shares and reports scanning, completion or a refusal. It is separate from Refresh connection status, which checks connectivity. A local console game can still appear as a console entry after its PC source is removed.

If Send files or Refresh is blocked by a running game, the page names that game, for example **Close Resident Evil 4 to send files**. Close it normally and allow session status to settle. A disabled action is not permission to resend or remove its preparation manually.

The PS5 page shows controller hints; the PC page keeps keyboard hints. Circle was confirmed to close Details and, at the main library, return to the PS5 dashboard. The extra Close browser button does not work in the tested console browser; use Circle. It is scheduled for removal in the next build. A browser that retained development-9/10 history must be fully closed and reopened once; reloading the page alone does not clear those entries.

## Background payloads during testing

Keep unnecessary background payloads closed while comparing behavior. In the recorded test, the separately running Game Compressor used about 96% of one CPU core while idle. After it was stopped, unchanged-reader comparison launches completed without the earlier common-dialog watchdog error. This supports CPU contention as a contributor; it does not establish RAM exhaustion or a universal fix. See the [comparison record](COMPRESSED-DEVELOPMENT.md#background-compressor-comparison).

## Optional Media shortcut setup

The blue **Blackwire** entry in the PS5 **Media** area opens the resident's page. It does not load `Blackwire.elf`; load the resident first. Creation through the explicit action below was verified with the single Blackwire ELF and stock ShadowMountPlus. Automatic creation during fresh resident startup is not included in this preview.

Finish ordinary configuration and game verification first, then close the game and wait for all copy, setup and registration work to finish. From PowerShell on the trusted LAN, run this **once**. It reads the current resident action token into memory; no token or SMB password needs to be pasted into a command or saved in a file.

```powershell
$consoleAddress = Read-Host 'PS5 address'
$blackwireUrl = "http://${consoleAddress}:5918"
$state = Invoke-RestMethod -Uri "$blackwireUrl/api/state"
if ($state.setup.token -notmatch '^[0-9a-f]{32}$') {
    throw 'The resident has not supplied a current action token.'
}
$headers = @{ 'X-Blackwire-Token' = $state.setup.token }
Invoke-RestMethod -Method Post -Uri "$blackwireUrl/api/media/register-public-sdk" `
    -ContentType 'application/json' -Headers $headers -Body '{}'
```

Read the result without submitting another action:

```powershell
Invoke-RestMethod -Uri "$blackwireUrl/api/media/public-sdk-status"
```

The first response only acknowledges the worker. Look for `phase: verified`, `registered: true`, `running: false` and `reservationHeld: false` in subsequent status reads. A busy or refused response is not success. If the outcome is unconfirmed or a reservation is still held, inspect status and report the reason; do not reload or repeat the action to force another install.

This uses the normal public SDK. It chooses the per-title export if available, otherwise `sceAppInstUtilAppInstallAll(NULL)`. The fallback is a **batch scan**, not a targeted install: it can refresh metadata for existing registered applications. The verified run used `mode: public_batch` and updated other rows' time/JSON metadata. Blackwire first requires a stable application-directory inventory, positive registration coverage for every other application, and an idle console. It refuses incomplete or changing observations. It makes one registration call, then observes its own row for up to 30 seconds after the call returns. The platform call itself has no cancellable timeout. An existing Blackwire row skips SDK installation. Blackwire does not edit the metadata database directly. No custom ShadowMount binary, helper ELF or new hook is used.

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
