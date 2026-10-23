# Blackwire roadmap

`v0.2.0-preview.1` adds supported PFS/PFSC compressed sources, two shares, compressed artwork, library refresh and PS5 control repairs. Earlier compressed testing reached GTA and RE7 gameplay; current combined-reader tests reached both initial menus. Circle is confirmed for the library and Details. See [testing](TESTING.md) and the [changelog](../CHANGELOG.md) for version-specific scope. Work below remains planned unless those records mark it verified.

## Standalone setup and reliability

- Add automatic fresh-install Media setup using the qualified public SDK path, without requiring a PC connection or opening a browser. It is not integrated in this preview.
- Broaden fresh-install, missing-resident, normal-exit, Uninstall and crash/restart qualification.
- Extend hardware checks for deleted-copy status and the updated ordinary Uninstall recovery.
- Keep incomplete operations actionable without blind retries.
- Remove the redundant Close browser button; retain the confirmed native Circle exit. Complete physical Settings/Help controller checks and add an interaction guard to initial history-event handling.
- Publish reproducible source/build records, checksums, notices and relinking material alongside each preview.

## Loading and performance

The indexed 64 MiB payload cache, bounded PFSC batching and reduced unchanged-source launch verification are included in this preview. Host comparisons are recorded in [compressed-source results](COMPRESSED-DEVELOPMENT.md). Controlled PS5 loading-time comparisons remain pending; the cache budget was not increased.

- Compare the same game's menu and scene-loading intervals with the same selections; the historical reader comparison did not show shorter loading.
- Expose useful delivered-byte, request-time, request-size and cache-hit counters where the resident can observe them.
- Evaluate bounded read-ahead, startup-range caching and concurrency against correctness and memory limits.

## Source formats and game coverage

- Record startup, menu and gameplay results for more owned extracted backups.
- Investigate failures using actual source/read evidence and preserve unknown outcomes.
- Extend the current combined-library reader's qualification through full gameplay, repeat Blackwire Play and Games-tab launches, missing-share handling and hardware Uninstall. Earlier-reader gameplay is recorded separately.
- Add and independently test exFAT-based compressor output after the PFS/PFSC console path is qualified.

## Blackwire Dashboard

We are developing a full native **Blackwire Dashboard** as a separate product. The planned interface combines a cover browser and controller navigation with streamed PC games and existing local console games in one library. Planned features include native connection settings, cover/grid views, readiness counts, preparation progress and launch controls.

The native interface and its installer will have their own artifact and hardware qualification. They are not bundled with this standalone web preview. There is no announced release date; development and test results will determine availability.
