# Preview status

## Current published test beta — v0.3.0-beta.2

The current published beta fixes saved settings appearing blank during startup and a browser settings check left locked by a previous resident. It retains beta.1's supported MkPFS exFAT reader and self-contained step-7 installer, plus bounded diagnostics. The direct download and Payload Manager custom source offer the same beta.2 runtime. Use the [beta download and instructions](BETA-TESTING.md); exact identities and observations are recorded in [testing](TESTING.md#v030-beta2). Native Dashboard remains a separate product.

## Unreleased verification

The latest local source adds image-access errors, source-companion preservation, strict preparation-only removal, retained-library reload repairs and reduced duplicate preparation checks. Its normal build passed 54 host suites, 62 host commands and 146 SDK commands, with private diagnostics excluded. Its first normal Payload Manager launch did not yield an available Blackwire page; the console state and cause are unconfirmed. No game was prepared with that candidate. It remains held for console verification and a measured speed comparison; see the [exact record](TESTING.md#unreleased-image-access-and-metadata-reads). Published downloads have not changed.

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
- Automatic Media registration on fresh ELF startup is not integrated. Use the optional [authenticated setup action](SETUP.md#optional-media-shortcut-setup) while idle. The qualified fallback is a public SDK batch scan that can refresh existing applications' metadata.

The previous `v0.1.0-preview.1` release remains the extracted-folder baseline. Its retained-file GTA setup recovery, modal checks and Hot Wheels gameplay stay attributed to their original binaries in [testing](TESTING.md#folder-only-preview).

The full native **Blackwire Dashboard** remains a separate product in development. This release contains the web interface and shared resident/reader functions. See the [roadmap](ROADMAP.md#blackwire-dashboard).

Build provenance and dependency records live under [metadata](../metadata/). Preserve them with corresponding source, notices and relinking material. Game content, saved data, credentials, external console system modules and private captures are excluded from the release.
