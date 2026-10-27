# Changelog

Changes are grouped by published preview. A build pass, initial menu and controllable gameplay are recorded separately in [testing](docs/TESTING.md).

## Unreleased — verification in progress

The latest ownership and idle-admission follow-up is host-tested source only. Its complete host run, normal SDK build, packaged-byte checks and console tests are pending. The earlier `c47da742…` candidate and its preparation measurements below do not qualify these new changes.

- Check fresh registration ownership before creating a Send job or copying files. An already owned copy of the same title from another source must be uninstalled normally before switching sources. Same-source retained preparation and independently completed cleanup remain eligible for their normal checks. Early Send refusals distinguish pending cleanup from ownership that could not be verified; neither starts a copy or repeats registration.
- Stop offering **Remove startup files only** when an existing Blackwire owner or unverified ownership inventory would make its action guard refuse. Details distinguishes an unconfirmed registration observation or release from ordinary unverified ownership and advises refreshing status. Keep the actual deletion guards unchanged and preserve existing copies.
- Report the first failing preparation check and its same-call idle reason, including the first or second idle observation and a known error number. Missing, unreadable or unmatched lifecycle history is a refusal, not proof that a game is running.
- Refuse incomplete lifecycle-log records and unsuccessful log closes. A secondary close failure no longer hides the first read, parse or busy-history failure.
- Carry only the exact recognized ownership refusal messages through the normal browser action route. Preserve generic and uncertain handling for other replies without forwarding arbitrary backend text.
- Keep the username draft during an accepted connection check. Correcting a failed secondary-share check can reuse the saved sign-in for the same PC and account; passwords still clear, and changing the PC or account still prevents reuse.
- Keep inactive, completed Send history from blocking Play after a library refresh when the exact current catalog entry allows launch. Historical byte-verification flags remain unchanged; startup files are checked again before launch.
- Publish verified temporary-file bodies using the existing 256 KiB buffer. Recheck the exact held parent, temporary file and newly created final file after callbacks before every final write or retry. Source-consumer and generic-control writes keep their 64 KiB limits; complete hashes, readback and synchronization remain. The normal one-title A/B/A comparison is recorded below.
- Reduce duplicate preparation checks inside each native LOCAL write attempt. Every write and retry still refreshes the full current-operation authority and checks its held parent after callbacks; staged copying, source checks, hashes, readback and synchronization remain. Host callback counts are lower; a PS5 elapsed-time improvement is not yet established.
- Preserve a validated compressed preparation when reloading or refreshing the library. Keep the sealed projection on its current inspection path instead of sending it through a legacy-only hint; reloading metadata still does not grant verified startup bytes or launch authority.
- Preserve an independently detected upgrade requirement on an older compressed preparation when the legacy hint reports no additional change.
- Show ordinary removal guidance in Details instead of the internal confirmation and metadata-status codes. Blocked removal reasons remain visible.
- Keep the reason for a refused Uninstall visible in Details. Add a separate **Remove startup files only** action for a verified Blackwire preparation when an existing console registration prevents ordinary Uninstall. This action preserves that existing installation and does not mark the PC source Removed.
- Refuse startup-only removal when an earlier relevant cleanup remains unfinished, including after a resident restart. Check mount links across the app directory so another title's leftover app cannot retain a link into the startup copy or its private preparation record. Completed older cleanup and unrelated valid app links remain eligible for their normal checks.
- Distinguish registration database observations from missing or conflicting app metadata at setup step 8. Preserve the ownership refusal instead of treating an unknown registration as absent.
- Report the failing SMB share and connection, catalog, source-validation or artwork stage in the setup form. Failed checks preserve saved settings.
- Include authoritative compressed-image counts and each item's admission error, stage and namespace observations in the read-only PC diagnostic summary. Keep credentials, tokens and source bindings excluded; missing values remain distinct from zero.
- Distinguish a refused SMB sign-in/share connection from a connected share whose exact game image cannot be read. Report the image path, failing operation and available transport result; directory listing alone does not prove file Read access.
- Use a bounded 128 KiB read window for compressed-image metadata. Source-generation and cancellation checks remain active, and requests of at least 64 KiB bypass the window. A five-image PC SMB comparison reduced metadata read requests from 1,790 to 56; one same-PC timing changed from 0.386 to 0.206 seconds. These are not PS5 loading or preparation benchmarks.
- Preserve and verify a game's original Ampr module against its prepared source receipt while installing Blackwire's embedded reader separately. Use a distinct local-only verification path for uninstalling owned preparations.
- Preserve source `fakelib`/`fakelib2` precedence, including an empty preferred directory. Keep source companion files separate from Blackwire's reader and private launch inputs, with source-bound hashes checked throughout preparation, launch and removal.
- Allow normal removal of a verified older partial preparation containing source-owned `fakelib2` files. Unrelated or changed files still refuse removal.
- Keep an existing supported legacy `fakelib` preparation eligible for its normal verification checks. Only conflicting or unsupported companion layouts request a fresh preparation.
- Release an unused launch reservation after a definite refusal before any private input file is created. Preserve existing files and avoid falsely blocking subsequent actions; partial writes and uncertain closes remain blocked.
- Recognize the exact official PlayGo 0.4 log and no-log modules alongside the supported newer artifacts.
- Keep ordinary Ready checks local so refreshing a prepared library does not add an SMB reconnect for every game.
- Remove the top-right **Close browser** button. Preserve Circle and dialog Back controls.
- Make startup tracing, launch timing and verbose setup logging explicit private diagnostic build options. Ordinary builds retain user-facing errors.

An old/new/old normal-build Alan Wake 2 comparison using the earlier `c47da742…` candidate observed 26–28% less copy/verification time and 11–12% less total preparation time for one exact compressed source. Fresh setup, retained Refresh/Play, visible Continue, Close and Uninstall passed; all controlled console test data were then removed. Registration time, gameplay loading, the latest source changes and other environments are not qualified by this result.

The earlier `c47da742…` publication-copy candidate passed all 54 host suites, 62 host commands and 146 SDK commands with matching frozen source snapshots and private diagnostics excluded from its packaged ELF. Its clean Payload Manager tests passed fresh compressed Alan Wake 2 preparation, Uninstall/Restore/new Send, retained readiness after a same-build resident restart, Refresh and repeat launch/Close. A fresh extracted-folder installation also passed preparation, four actual module-hash checks, initial launch, Close and Uninstall. Failed-share correction and nonfatal invalid-image rejection/recovery passed. Launches visibly reached Game Saving/Continue; controllable gameplay remains unverified. The PC-only collector update separately passes seven tests and genuine Claude/Fable review; it does not change that packaged ELF.

Nothing in this section is a published release. The latest follow-up passes focused host checks for ownership, initial idle admission and the joined browser/backend refusal route; its new full-build and console qualification remains pending. The earlier loss of console access remains unexplained. The reporter's exact idle-authority refusal has no reproduced reporter-specific cause; the new diagnostics and conservative parser repairs do not establish that report fixed. See the [new host evidence and limits](docs/TESTING.md#unreleased-ownership-and-idle-admission-follow-up), [earlier candidate record](docs/TESTING.md#october-27-final-publication-candidate) and [qualification limits](docs/REQUIREMENTS.md).

## v0.3.0-beta.2 — connection settings recovery

- Read and publish saved connection settings before startup library discovery. A slow scan no longer leaves a valid saved connection unreported and opens a blank, disabled setup form.
- Recover the browser's pending connection check when the resident restarts. Keep the draft, release the obsolete operation watch and offer **Reload settings status**. Require an explicit action before saving again; never replay the old request automatically.
- Add credential-free settings-file status and bounded diagnostics distinguishing settings-file checks, SMB connection, catalog parsing and artwork failures. Include selected setup fields in the optional read-only collector.
- Retain strict settings-file identity checks and the rule that a failed connection/library check does not replace saved settings. The file is `/data/blackwire/share.conf`; do not delete it to change residents.
- Preserve beta.1's reader, exFAT scope and self-contained installer. This is not a loading-speed claim or a native Dashboard release. The reporter's exact remaining failure still needs their status/log capture.

## v0.3.0-beta.1 — MkPFS exFAT and self-contained reader setup

- Publish the standalone test beta with bounded Send/setup diagnostics and [one-attempt test instructions](docs/BETA-TESTING.md). Keep existing preview downloads and the normal Payload Manager catalog unchanged.

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
