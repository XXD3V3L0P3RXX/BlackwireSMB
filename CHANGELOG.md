# Changelog

Changes are grouped by published preview. A build pass, initial menu and controllable gameplay are recorded separately in [testing](docs/TESTING.md).

## Unreleased — self-contained reader setup and exFAT

- Remove setup step 7's hidden dependency on a particular `/data/shadowmount/fakelib` pack. Install and hash-check the reader embedded in Blackwire without opening or changing the global directory.
- Preserve qualified optional game companions and recognized legacy five-file private bundles. Retain strict refusal for unknown or altered files, path changes, expired authority and uncertain operation outcomes.
- Cover a missing-global fresh installation, retained readers, upgrades and ordinary removal. See [repair details and verification](docs/RUNTIME-BUNDLE-DEVELOPMENT.md).
- On the 13.00-family test console, normal removal of the old NFS test copy and fresh exFAT preparation passed. The new copy contains only the embedded reader, reaches its Continue screen and closes normally; the other five preparations and global pack are unchanged. This does not qualify the reporter's firmware 10.01 environment.

- Add read-only support for clean, single-FAT exFAT images with fully initialized contiguous allocations, including the default MkPFS 0.0.9 and 1.1.0 PFS/PFSC wrappers. FAT-linked contiguous files and NoFatChain files are both supported. Other layouts still refuse explicitly.
- Preserve Unicode filenames and validate boot/backup checksums, allocation bitmap, FAT chains, up-case table, entry-set/name checksums, directory cycles, overlapping allocations and duplicate names.
- Avoid repeatedly traversing unrelated files' FAT chains during path lookup. File-token size and the 64 MiB payload-cache budget are unchanged; the exFAT adapter has a fixed 128 KiB Unicode map and bounded metadata workspace.
- Add independent exFAT corruption fixtures and include PFS/exFAT streaming/cancellation regressions in ordinary host builds.
- This is development work, not a change to the existing release downloads or firmware qualification. Format discovery and step-7 installation are separate faults with separate checks. See [exFAT development](docs/EXFAT-DEVELOPMENT.md).

## Documentation update — October 24, 2026

- Make the tested 13.00-family firmware profile, runtime payload versions, operator-reported jailbreak chain and optional helper tools visible in the README. Keep unrecorded build/entry-point details explicit.
- Distinguish the latest ShadowMountPlus 1.7beta7 retest from the earlier 1.7-beta5fix1 baseline and state that 10.01 and other firmware profiles remain unverified.
- Explain the external runtime-library prerequisite and the missing-directory step-7 failure before the Send-files instructions. This documentation update does not change either ELF or claim to fix the separate missing-image discovery report.

## v0.2.0-preview.1-sendlog.1 — optional diagnostic

- Offer the unchanged working ELF and optional logging ELF together on one download page; no pull request or merge is required to use either.
- Add bounded Send/setup logs with setup stages, directory paths and error numbers.
- Provide installation, collection and rollback instructions plus an optional read-only summary collector.
- Preserve the v0.2.0-preview.1 game reader and normal download/catalog. No compatibility or speed fix is claimed.
- Fresh RE4 setup passed on the diagnostic; the reported step-7 failure remains unreproduced with our three-file flat-share test. See [diagnostics](docs/DIAGNOSTICS.md).

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
