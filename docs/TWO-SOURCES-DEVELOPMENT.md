# Two shared folders

The development build `blackwire-two-sources-dev.7` can scan two SMB shares on
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

Keeping compressed games in one folder and extracted games in the other is a
useful arrangement. Moving a PC source changes its identity; this feature does
not relocate or delete any PC game files.

## Qualification

This is development work and is not part of the published folder-only preview.
The SDK build, reader host checks, two-source selection/settings checks, and
web form tests are recorded in the build outputs. Live qualification of the
combined library and the updated payload is still required before release.
