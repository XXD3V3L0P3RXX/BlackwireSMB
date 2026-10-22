# Blackwire roadmap

The standalone preview has an observed path for **Send → Ready → Games icon**, repeated Hot Wheels direct launch, and an explicitly installed blue Media entry. The latest update adds the revised game-card interface and has completed retained-file GTA setup recovery on console. See [testing](TESTING.md) for the exact scope. Work below remains planned unless that record marks it verified.

## Standalone setup and reliability

- Add automatic fresh-install Media setup using the qualified public SDK path, without requiring a PC connection or opening a browser. It is not integrated in this preview.
- Broaden fresh-install, missing-resident, normal-exit, Uninstall and crash/restart qualification.
- Extend hardware checks for deleted-copy status and the updated ordinary Uninstall recovery.
- Keep incomplete operations actionable without blind retries.
- Publish reproducible source/build records, checksums, notices and relinking material alongside each preview.

## Loading and performance

- Compare the same game's menu and scene-loading intervals with the same selections; the historical reader comparison did not show shorter loading.
- Expose useful delivered-byte, request-time, request-size and cache-hit counters where the resident can observe them.
- Evaluate bounded read-ahead, startup-range caching and concurrency against correctness and memory limits.

## Source formats and game coverage

- Record startup, menu and gameplay results for more owned extracted backups.
- Investigate failures using actual source/read evidence and preserve unknown outcomes.
- Qualify compressed/container streaming separately; folder sources remain the preview path.

## Blackwire Dashboard

We are developing a full native **Blackwire Dashboard** as a separate product. The planned interface combines a cover browser and controller navigation with streamed PC games and existing local console games in one library. Planned features include native connection settings, cover/grid views, readiness counts, preparation progress and launch controls.

The native interface and its installer will have their own artifact and hardware qualification. They are not bundled with this standalone web preview. There is no announced release date; development and test results will determine availability.
