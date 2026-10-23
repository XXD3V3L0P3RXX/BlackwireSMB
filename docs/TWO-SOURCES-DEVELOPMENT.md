# Two shared folders

The development build `blackwire-two-sources-dev.8` can scan two SMB shares on
the same PC, using the same sign-in details. The second share is optional.

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

## Qualification

This is development work and is not part of the published folder-only preview.
The SDK build, reader host checks, two-source selection/settings checks, and
web form tests are recorded in the build outputs. Live qualification of the
combined library and the updated payload is still required before release.

## Library refresh and console controls (development 9)

The main library has a visible Refresh library button. It uses the existing authenticated POST /api/refresh operation to rescan both configured shares; merely polling the combined catalog does not discover moved source files. The button reports scanning, completion and refusal, and stays blocked with a visible explanation while a game or file operation is active. No source files or console registrations are moved or removed by the UI.

The PS5 browser is identified by its PlayStation user agent or the console loopback Media URL. It shows controller hints instead of PC keyboard instructions. Square opens Details through the standard gamepad mapping. Native Circle history Back dismisses a confirmation, Details, Settings or Help before requesting window.close at the library. A same-page history entry avoids navigating away before panels close; an initial WebKit popstate does not close the page. Native Circle owns Back when that history path is available, avoiding duplicate gamepad/history consumption. Controller navigation can reach the visible refresh and header controls. PC keyboard behavior stays available in PC browsers.

Offline checks exercise the actual embedded page, authenticated one-shot refresh, removed-source results, refused refresh without replay, blocked-game explanations, console/PC hints, initial popstate, panel Back, root close and duplicate Circle avoidance. Physical PS5 Circle behavior still needs an on-console check of the deployed build.

## Circle repair (development 10)

The on-console development-9 test reported that X selected items but Circle did not exit. That build incorrectly dropped the standard gamepad Back action whenever history support existed. The repair handles direct gamepad Circle, keyboard Back/Escape (including legacy keyCode events), and native browser history independently. Distinct #blackwire-back and #blackwire fragment URLs also let native Back work when WebKit omits event.state. Duplicate channels from one press are consumed once; another press on the same channel is a new action. Circle can dismiss Settings even when an input has focus.

A console-only Close browser button provides an explicit click action. It closes the browser client, leaving resident operations running. PC pages do not show that control. Offline checks cover direct Circle without a history event, native Back with null state, legacy keyboard Back, duplicate gamepad/history events, and the Close browser button. Physical Circle exit must be checked again after loading development 10; development 9 is not qualified for that behavior.
