# Changelog

Changes are grouped by published preview. A build pass, initial menu and controllable gameplay are recorded separately in [testing](docs/TESTING.md).

## v0.2.0-preview.1

### Added

- Streaming from supported unsigned contiguous D32 PFS, PFSC and one-file PFS wrappers, including the tested `.ffpfsc` images. The image stays on the SMB share; no PC decompression service is needed.
- Two SMB shares on the same server, using the same credentials, in one library. The second share is optional and both are checked before settings are saved.
- Covers read directly from validated compressed images, with bounded artwork caching.
- Refresh library beside the library filters, with scanning, completion and refusal messages.
- PS5 controller hints and controller navigation to library/header controls, while keeping PC keyboard controls in PC browsers.

### Fixed

- Known previous Blackwire reader versions can complete the guarded reader upgrade without recopying valid startup files. Unknown or altered readers remain refused.
- A completed Removed record for one source no longer blocks a different prepared copy solely because its title matches.
- An explicitly cancelled compressed read no longer permanently poisons the next request on the same valid source. Real identity, transport and decoder failures retain their refusal behavior.
- Disabled Send files and Refresh actions visibly name the running game that must be closed, including sessions adopted from the PS5 Games dashboard.
- Native Circle/Back at the library is no longer swallowed or trapped by synthetic root history entries. Circle was confirmed on PS5 for Details dismissal and return to the dashboard.

### Changed

- The existing 64 MiB payload cache uses an index instead of repeatedly scanning all cache slots.
- Adjacent PFSC blocks within a requested range are grouped into bounded reads of at most 256 KiB.
- Unchanged prepared compressed sources use bounded launch verification instead of rehashing every remote startup body on Play. Required local startup checks remain in place.
- Recommend launching Ready games from their ordinary PS5 Games icons. Blackwire and the SMB share must remain available.

### Known issues and scope

- The redundant Close browser button did not close the tested PS5 browser. Use Circle; removal of the button is planned for the next build.
- Supported compressed formats are limited; the file extension alone does not establish compatibility.
- GTA V and RE7 gameplay passed earlier compressed-reader tests. The current combined-library reader has initial-menu observations for both; full gameplay was not repeated for this exact package.
- A separate idle Game Compressor payload consumed about 96% of one CPU core. Stopping it was followed by clean comparison launches. CPU contention is a supported contributor to the earlier system-dialog failures; a sole cause or RAM exhaustion has not been established.
- Controlled PS5 load-time comparisons, broader firmware/game coverage, hardware compressed-copy Uninstall and interrupted-operation recovery remain open.

The binary is the tested `blackwire-two-sources-dev.11` artifact. This release updates documentation and packaging without rebuilding or changing that runtime. See [build provenance](docs/BUILD.md#current-preview-build-record).

## v0.1.0-preview.1

- Initial standalone web preview for streaming extracted folders from an SMB share.
- Startup-file preparation, sparse bulk-file placeholders, per-game reader setup and PS5 Games-icon registration.
- Direct Games-icon startup handoff while the Blackwire resident and share remain available.
- Revised library layout, game-card progress and repaired modal Back/outside-click controls.
- Guarded retained-file setup recovery and an explicit optional blue Blackwire Media shortcut setup action.
- Source, checksums, license notices and relinking material supplied with the preview.
