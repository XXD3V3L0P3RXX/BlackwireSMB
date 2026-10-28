# Blackwire roadmap

The published `v0.3.0-beta.2` adds settings recovery to beta.1's supported exFAT reader and self-contained installer. The earlier `v0.2.0-preview.1` introduced supported PFS/PFSC sources, two shares, compressed artwork, refresh and PS5 controls. Earlier gameplay and Circle checks remain attributed to their exact binaries. See [testing](TESTING.md) and the [changelog](../CHANGELOG.md) for version-specific scope. The current local candidate has passing full builds but no completed console preparation test; work below remains planned unless those records mark it verified.

## Standalone setup and reliability

- Qualify the development candidate's automatic fresh-install Media setup after a normal Payload Manager load, including missing SMB settings, an existing shortcut and busy/refused startup. Its guarded public-SDK path has earlier explicit-action evidence; the new automatic ordering has focused host and normal-build checks and still needs console qualification. No PC command is required by the intended installation flow.
- Broaden fresh-install, missing-resident, normal-exit, Uninstall and crash/restart qualification.
- Extend hardware checks for deleted-copy status and the updated ordinary Uninstall recovery.
- Keep incomplete operations actionable without blind retries.
- Qualify the unreleased Close browser button removal while retaining Circle and dialog Back controls. Complete physical Settings/Help controller checks and investigate initial history-event interactions against actual failures.
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
- Broaden the already supported contiguous exFAT layout's format and console coverage; preserve explicit refusals for unsupported or changing images.

## Blackwire Dashboard

We are developing a full native **Blackwire Dashboard** as a separate product. The planned interface combines a cover browser and controller navigation with streamed PC games and existing local console games in one library. Planned features include native connection settings, cover/grid views, readiness counts, preparation progress and launch controls.

The native interface and its installer will have their own artifact and hardware qualification. They are not bundled with this standalone web preview. There is no announced release date; development and test results will determine availability.
