# Two shared folders

`v0.2.0-preview.1` packages the tested `blackwire-two-sources-dev.11` runtime.
It scans two SMB shares on the same PC with the same sign-in details. The
second share is optional. See the [changelog](../CHANGELOG.md) for all changes.

For separate extracted and compressed libraries, use this layout:

| Setting | Example share | Folder on the PC |
| --- | --- | --- |
| Shared folder name | `PS5Compressed` | `D:\Compressed` |
| Second shared folder | `PS5GAMES` | `D:\PS5Games` |

Enter share names in Blackwire Settings. The Windows drive paths are configured
on the PC when creating the shares. Both shares only need read access. No PC
server application or middleware is required.

Save and check reads both shares before saving the connection. If either share
cannot be read, the previous saved connection is kept. Clearing the optional
second field returns to a single share. Existing single-share configuration
files continue to work.

Both sources appear in the same library. Each copy keeps its own server, share,
relative path and preparation identity. Equal game titles or relative paths in
different shares are separate entries; preparation and removal act on the
selected copy. Details shows the share name. A game started from the PS5 Games
tab is matched to its exact registered prepared copy.

Compressed games read their own `sce_sys/icon0.png` from the validated image;
they do not need an extracted copy for artwork. Reads retain the image generation
checks, per-image 4 MiB limit and combined 16 MiB artwork-cache budget. Missing
or oversized artwork uses the existing placeholder.

Normal Finish setup recognizes the exact readers installed by compressed
development 5 and two-source development 6. Their bytes and sizes are pinned;
unknown or altered libraries remain refused. Existing startup files are kept.

When a game blocks Send files, the selected game now shows the running title
and the action needed, for example: "Close MX vs ATV Legends to send files."
Blocked-action explanations are visible automatically beside the selected
game's status, without opening the status badge. The existing session guard
still prevents preparation while another game is active.

Keeping compressed games in one folder and extracted games in the other is a
useful arrangement. Moving a PC source changes its identity; this feature does
not relocate or delete any PC game files.

## Refresh and disabled actions

Refresh library beside the main filters submits the existing authenticated rescan of both configured shares. It reports scanning, completion or refusal. Polling the page alone does not rediscover moved PC files. A PC source removed from a share disappears after the rescan; a separate full local-console copy may still be shown as its own entry.

While a game is running, Send files and Refresh remain blocked. Their visible reasons name that session, including a game launched from its PS5 Games icon. Close the named game normally and let status settle before starting preparation. The page does not repeat refused or uncertain operations automatically.

## PS5 controls

The PS5 page shows controller hints and keeps PC keyboard hints on PC browsers. D-pad navigation reaches library and header controls; Square maps to Details in the standard gamepad path. Circle was confirmed on console to dismiss Details and keep the library open, then to return from the library to the PS5 dashboard.

Development 9/10 swallowed native root Back and introduced a rearming synthetic history entry. Development 11 removes that root trap and leaves the native action uncancelled at the library. A panel Back is consumed only when dismissing that panel. Duplicate input channels preserve the original press's consumed status.

The separate Close browser button still failed when selected with X. It remains in this exact release binary; use Circle. Removal is planned for the next build. Physical Circle checks in Settings/Help remain outstanding even though those paths have offline coverage. If upgrading from a prior UI test in the same browser session, fully close and reopen the browser once; reload can retain the old history entries.

## Qualification

The current ELF passed a full 144-command source SDK build with unchanged inputs across the build. Ninety UI checks passed, including actual-page rescan behavior, removed-source results, refusal without replay, running-game reasons, console hints, initial history events, panel Back, duplicate-channel suppression and uncancelled root Back. Synthetic close-policy tests do not prove the Sony browser implements the same policy.

Live development-7 checks combined the extracted and compressed shares, restored seven known prepared readers through normal Finish setup without recopying valid startup files, and showed both compressed covers. GTA reached Continue and RE7 reached Start Game; full gameplay was not performed in those runs. The same reader is retained in the development-11 release. Earlier compressed development-5 gameplay belongs to a different reader and is attributed separately in [testing](TESTING.md#compressed-and-two-share-preview).

On development 11, the exact ELF and served page were read back, saved shares were retained, and the user confirmed the two Circle behaviors above. The failed Close button is recorded separately. No console restart was performed for this UI update. Controlled game-loading benchmarks, hardware compressed-copy removal and broader recovery qualification remain open.
