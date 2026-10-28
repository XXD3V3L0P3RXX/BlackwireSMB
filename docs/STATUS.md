# Preview status

## Current published test beta — v0.3.0-beta.2

The current published beta fixes saved settings appearing blank during startup and a browser settings check left locked by a previous resident. It retains beta.1's supported MkPFS exFAT reader and self-contained step-7 installer, plus bounded diagnostics. The direct download and Payload Manager custom source offer the same beta.2 runtime. Use the [beta download and instructions](BETA-TESTING.md); exact identities and observations are recorded in [testing](TESTING.md#v030-beta2). Native Dashboard remains a separate product.

## Unreleased verification

The newest development candidate schedules automatic Media setup once after the ordinary ELF startup, before SMB configuration is required. Its intended fresh-install flow is **Payload Manager load → automatic Media setup → configure SMB**. Busy startup waits preserve the existing idle and ownership checks; reloading is not a retry mechanism. Focused current-source host checks passed. Actual automatic registration and Media visibility remain unverified on the console; the earlier explicit Media action does not qualify this new startup flow. See [setup](SETUP.md#automatic-media-shortcut-setup).

The latest source repairs initial discovery ordering and retains callback owners when failed construction cannot confirm their release. Post-unmount refusals now distinguish registration-before-sources, source checks and registration-after-sources without skipping checks or repeating mutations. Focused host checks passed; the new full normal build, packaged-input checks and console validation are pending. The earlier poweroff and intermittent Uninstall Unknown remain unexplained, and publication stays on hold. See [the current scope](TESTING.md#unreleased-startup-ordering-and-owner-lifetime).

A subsequent private parser-only profile reduced one compressed Alan Wake 2 copy phase from **151.772 to 40.328 seconds** (73.4% less time), with the same idle, authority, source-read and local-I/O counts. Two-share setup and Ready completed, and all four prepared module hashes and permissions matched. The exact baseline file reader was preserved; a separate unlocked-stdio experiment failed setup and was rejected. This is one diagnostic workload on the recorded 13.00-family console, not a network-throughput or gameplay claim. That private test later retained an Uninstall Unknown before file deletion. A diagnostic follow-up passed two normal removals, Restore and another Send, with copy phases of 41.006 and 41.600 seconds (approximately 73% less than the private baseline). Both removals completed all 259 entries with one unmount, manual-source removal and platform Uninstall each; no new leaf failure event appeared. These passes neither reproduce nor repair the earlier failure. Full normal-build qualification remains pending, and publication stays on hold. See the [profile and limitations](TESTING.md#unreleased-parser-only-preparation-profile).

The preceding normal exact-removal artifact passed 59 host suites, 67 host commands, 146 normal SDK commands and 61 packaged-input checks, with private diagnostics excluded. Clean Payload Manager tests passed extracted and compressed Alan Wake 2 preparation, safe duplicate-source refusal, three complete normal uninstalls, Restore and a repeat Send. Normal Play/Close produced the expected game process, but the visible screen and gameplay were not verified. The test resident, canonical payload, preparations and Blackwire data root are removed. Preparation took longer than the earlier measured candidate, so the build remains held while that cost is investigated. Recovery from an actual hardware Unknown and gameplay are still unqualified. See the [exact record and limitations](TESTING.md#unreleased-exact-removal-recovery). Published downloads have not changed.

## Previous preview baseline

**v0.2.0-preview.1** adds supported PFS/PFSC compressed sources, two SMB shares, compressed artwork, library refresh and console-control repairs to the standalone web product. It runs as one `Blackwire.elf`; no Blackwire PC service, native Dashboard installer or Media helper is required. See the [changelog](../CHANGELOG.md).

## v0.2.0-preview.1 package

The release preserves the exact tested `blackwire-two-sources-dev.11` runtime. The ELF SHA-256 is `b9189c87cdb88d3ad0fccc947d0d007185bb0cfa1b39e8edb585c96347411fd9`; its embedded reader is `c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025`. Documentation and packaging changes do not change that binary. Full identities and evidence scope are in [testing](TESTING.md#compressed-and-two-share-preview).

- Both configured shares appeared in one live library. Known earlier prepared readers completed normal Finish setup while retaining valid startup files. GTA and RE7 covers came from compressed images.
- The current combined-library reader reached GTA's Continue prompt and RE7's Start Game screen. Earlier compressed development 5 passed user-controlled gameplay for both and a literal GTA Games-icon driving test. Those earlier gameplay checks are not new gameplay qualification of this package.
- Live Refresh rescanned the shares. A removed PC source disappeared while a separate legitimate local-console entry remained visible.
- Running-game Send/Refresh blockers were visible in both PC and PS5 pages and named the actual running game.
- The user confirmed Circle returns from the main library to the PS5 dashboard and closes Details while keeping the library open.
- Ninety UI tests and the complete 144-command SDK build passed. A bounded startup capture after replacement had no new watchdog, reader-failure or fatal-signal markers in its observed window; this is not a gameplay result.

## Known issues and next work

- The published preview's extra Close browser button stayed open on PS5. Use Circle. The unreleased source removes that button; current-candidate hardware qualification is pending.
- Circle in Settings/Help has offline coverage; the physical checks recorded here cover the library and Details.
- `.ffpfsc` is not a universal format guarantee. Unsupported layouts are listed in [format scope](COMPRESSED-DEVELOPMENT.md#formats).
- Both shares must use the same server and account. Moving a source changes its identity; Blackwire does not reconnect a different copy by title alone.
- Controlled loading-time benchmarks, broader title/firmware coverage, hardware compressed-copy Uninstall, missing-resident behavior and crash/restart recovery need more testing.
- A separate idle Game Compressor payload consumed about 96% of a CPU core. Stopping it preceded clean comparison launches. CPU contention is a supported contributor, while the complete system-dialog failure cause remains open. RAM exhaustion was not established.
- Automatic Media setup is implemented in the development candidate but awaits fresh verification; it is not in the unchanged published beta. Wait for its status rather than reloading or invoking the maintenance action while it runs. The public SDK batch fallback can refresh existing applications' metadata. See [setup and refusal limits](SETUP.md#automatic-media-shortcut-setup).

The previous `v0.1.0-preview.1` release remains the extracted-folder baseline. Its retained-file GTA setup recovery, modal checks and Hot Wheels gameplay stay attributed to their original binaries in [testing](TESTING.md#folder-only-preview).

The full native **Blackwire Dashboard** remains a separate product in development. This release contains the web interface and shared resident/reader functions. See the [roadmap](ROADMAP.md#blackwire-dashboard).

Build provenance and dependency records live under [metadata](../metadata/). Preserve them with corresponding source, notices and relinking material. Game content, saved data, credentials, external console system modules and private captures are excluded from the release.
