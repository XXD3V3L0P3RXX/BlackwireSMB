# Observed results and limits

This record distinguishes copied files, a running game process, a visible menu, gameplay and user-confirmed controls. Reaching a main/start menu counts as a startup pass; a process or loading screen alone does not. Results belong to their exact resident/reader pair.

No title-specific compatibility blocker has been identified in testing so far. The limited coverage below does not establish that every title or every stage of a title will work.

## Compressed and two-share preview

The unreleased exFAT experiment is recorded [separately below](#unreleased-exfat-test). It does not replace these release observations.

`v0.2.0-preview.1` packages the tested `blackwire-two-sources-dev.11` runtime without changing its bytes. Its runtime source commit is `f490e1917a1f1393d0948b24b9d93d03eee9e95b`; later release changes are documentation and packaging.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Current standalone Blackwire ELF | 5738856 | b9189c87cdb88d3ad0fccc947d0d007185bb0cfa1b39e8edb585c96347411fd9 |
| Embedded page | 150929 | d1d033c7b35d31fbab2a67c14dce3d215f775104dd4329ef7aa16789b545c28c |
| Embedded per-game reader | 831168 | c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025 |

| Check | Observed result and scope |
| --- | --- |
| Build and source stability | Full source SDK build, 144 successful commands, unchanged inputs across the build. No console is contacted by the builder. |
| UI regression suite | 90 checks passed, including actual-page refresh, refusal without replay, running-title explanations, console/PC hints, legacy Back events and duplicate-channel handling. Synthetic window-close behavior is not proof of Sony browser behavior. |
| Exact deployment | Stored ELF and served page matched the candidate bytes. Saved two-share settings and the reader were unchanged. No console restart was performed for this UI update. |
| Native root Circle | User confirmed a fresh Media launch followed by Circle returned to the PS5 dashboard. |
| Native Details Circle | User confirmed Details closed and the library stayed open. Physical Settings/Help checks were not repeated. |
| Extra Close browser button | User selected it with X; it stayed open. This control is not qualified. Use Circle; button removal is planned for the next build. |
| Library refresh | The authenticated rescan progressed through scanning to a new catalog. A removed PC source was absent; a separate local console copy remained correctly visible. No game preparation or registration was manually reconnected. |
| Running-game blockers | PC and PS5 pages named the running RE4 session in the disabled Send/Refresh reasons. No blocked Send was submitted. |
| Bounded startup observation | A 150-second replacement capture, excluding the first two seconds of backlog, contained no new Watchdog:55, BWREAD_FAIL1, Fatal trap, SIGSYS or SIGBUS markers. This covers resident replacement/idle UI, not gameplay. |

The same reader was introduced in two-source development 7. Both SMB shares appeared together, seven prepared copies became Ready with their valid startup files reused, and GTA/RE7 covers were decoded from their compressed images. GTA reached Continue and RE7 reached Start Game. Each 150-second initial-launch capture had zero watchdog or reader-failure markers. The operator was away from the controller; full gameplay and literal Games-tile launch were not qualified on that reader. The current UI observation had nine Ready entries among thirteen total; Ready counts alone do not qualify gameplay.

Earlier `blackwire-compressed-loading-dev.5` used reader SHA-256 `135148e2de73b5b54d6f18dbfb95c1f2cd2d7d7e3e6dac60b756e844e6cbc684`. With Game Compressor stopped, the user confirmed RE7 controllable gameplay after Continue/save loading and GTA driving after a PC-browser Play request, both without the system error. The user then confirmed driving without the error after a literal GTA Games-tab launch. RE7 dashboard launching was also reported to work; it has a less detailed contemporaneous capture record than GTA's explicit tile test. These are earlier gameplay observations, not new gameplay passes on the current combined-library reader.

The background Game Compressor comparison supports CPU contention as a contributor to the earlier common-dialog failure. It does not establish RAM exhaustion or a sole cause. Format refusals, exact test methods, host measurements and remaining hardware checks are documented in [compressed-source results](COMPRESSED-DEVELOPMENT.md) and [two-share results](TWO-SOURCES-DEVELOPMENT.md).

## Folder-only preview

The following records belong to `v0.1.0-preview.1` and its earlier working binaries. They are retained as historical evidence rather than current-package gameplay qualification.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Current standalone Blackwire ELF | 5706512 | c4897f0078b8854d1c345587198bc33210d6d80c8c61d3fb290e9e8ff1c1f577 |
| Embedded web page | 141067 | 0e3efe1e8bba33feee55a56828af5662536464102a69ba4062fe9838b7bf6a80 |
| Embedded per-game reader, unchanged | 798192 | d02f20f4ee8b1db1617e7b2c7ad4f76821e8b9da2523ac185a2e881ca33462be |

| Check | Result and scope |
| --- | --- |
| Updated interface | The revised layout was reviewed locally. The exact current page was verified as served at `/` and `/ps5`. The current resident includes a closing-tag repair that keeps modal controls interactive. Game-operation progress and notices stay with the selected card. |
| Live Details controls | Back worked in the browser for Ready GTA and unprepared Rise of the Ronin, before any Send for that title. Clicking outside the modal closed it. |
| Live Settings and Help controls | Settings Cancel and Help Close worked in the browser. Clicking outside each modal closed it. |
| Interface regression checks | 87 UI and 19 handoff checks passed. The structural regression check rejects the previous closing-tag defect and accepts the repair. |
| Uninstall confirmation controls | The live browser opened GTA's correctly named Uninstall confirmation after the user-authorized normal close of MX. Cancel dismissed the confirmation and Back returned to the library. No Uninstall was submitted and no game was deleted. |
| Registration recovery | Actual lifecycle/ledger and read-only SQLite host checks passed under ASan/UBSan: 12 cases and 15,873 checks, including uncertain, active, foreign-source and pending-history refusals. |
| Ordinary Uninstall recovery | Actual Send and normal Uninstall adapter/core host checks passed under ASan/UBSan: 12 scenarios, 54 fixture cases and 23,506 checks. Synthetic originals, unrelated local copies and saves were preserved. Hardware qualification of this updated path is pending. |
| Deleted-copy status | Focused host checks cover positive folder absence removing stale cached readiness while uncertain observations remain blocked. Updated hardware qualification is pending. |
| Build provenance | A complete clean source build passed for the backend and approved interface. The current ELF then received the focused interface-only modal repair. Its backend and embedded reader are unchanged from the prior resident below; this is a separate binary identity. See [build notes](BUILD.md#current-preview-build-record). |

Normal ShadowMount replies/effects and process-idle checks are modeled in the host recovery fixtures. Compiler and host-test success do not substitute for console gameplay. Match downloaded files to the release's attached checksum and build manifest.

## GTA recovery on the prior resident

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Prior standalone Blackwire ELF | 5706512 | a59e5cda3d7cac2af23670f984f74d104d9e88f9e451552bd3e69e879ce5b302 |

One ordinary authenticated GTA **Finish setup** completed on this resident: files prepared, registration confirmed, Ready and Play enabled, and zero source-copy bytes. The retained startup files were reused without manual registration repair. This narrowly observed setup result belongs to this prior binary. The current resident retains its backend and embedded reader, but this result is not a GTA gameplay test or a new gameplay observation on the current resident.

## Working resident and reader

The working baseline console observations below belong to this exact pair:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone Blackwire ELF | 5689632 | 6f68cd231609311a796bc1b4d52e759975c541ee488446c30ec7c123ed948ebb |
| Embedded per-game reader | 798192 | d02f20f4ee8b1db1617e7b2c7ad4f76821e8b9da2523ac185a2e881ca33462be |

These are the tested working identities, not an assertion that a later source rebuild has identical bytes or has been run on hardware. Match a downloaded preview to its attached checksums and build manifest. A new packaged build needs its own build record; inherited hardware results must remain attributed to the pair above.

The environment used profile `0x13000040`, stock ShadowMountPlus `1.7-beta5fix1`, the [five external libraries](REQUIREMENTS.md#external-shadowmount-libraries), Kstuff, and a wired SMB share. No custom ShadowMount or Media helper was used.

| Check | Observed result |
| --- | --- |
| Hot Wheels direct PS5 Games-icon launch | Two direct launches were adopted by the resident. The user confirmed gameplay on the second launch. No preceding web Play request was required for these launches. |
| Hot Wheels PC delivery | An independent reader witness observed approximately 609 MB delivered at the second session observation, with no failed reads in that observation. This is a cumulative byte count, not a throughput benchmark or whole-game guarantee. |
| RE7 fresh Send | The user prepared a new startup copy with this resident; preparation and registration completed. |
| RE7 direct Games-icon launch | The resident adopted the session and delivered startup inputs. A visible menu or gameplay result for this run is not yet confirmed. |
| Blackwire blue Media entry | The explicit same-ELF public SDK action returned status 0, its row was observed repeatedly, and its reservation was released. The user confirmed the blue entry opened the Blackwire page. |
| Media SDK scope | The verified mode was `public_batch`. It added Blackwire's title row and refreshed time/JSON metadata on 20 existing content rows. This was not a title-only SDK call. |
| Automatic fresh Media setup | Not integrated. The preview provides an [explicit authenticated action](SETUP.md#optional-media-shortcut-setup). |

Routine resident status does not expose a game-to-controller data witness for every reader path. Its unavailable counters must not be reported as zero; the independent Hot Wheels observation above is separate evidence.

## Earlier results

Earlier web-only builds reached a Hot Wheels race and GTA Story Mode loading at 90%. Earlier development reader/resident versions reached RE7 gameplay, a full Hot Wheels round, MK11's start screen and an NFS race. MX reached a loading/startup screen; a completed menu is not confirmed. A full local GTA copy worked; streamed GTA menu/gameplay was not confirmed for those folder-only artifacts. Later compressed GTA gameplay is recorded separately above. These observations belong to their actual versions and do not qualify every title on this preview.

## Performance

First preparation depends on game size and file count and can take several minutes or longer. Preparation time, click-to-game time, menu loading, scene loading and gameplay should be measured separately.

In one historical wired NFS comparison, Play to the first menu took 44.42 seconds and later loading to a drivable car took about 140 seconds. A later reader comparison took 55 seconds and about 142 seconds. It did not establish an improvement. Gameplay was reported as normal while menu and scene loading remained slower.

There is no universal speed claim. This preview does not establish compatibility for all games, firmware, Wi-Fi networks, altered global libraries, arbitrary container layouts or console-crash recovery. The indexed cache and PFSC batching have host measurements; those are not PS5 load-time benchmarks.

## Release verification

Keep exact source, build, dependency, binary and reader identities with each release. Offline host checks and compiler checks are separate from hardware observations. Before extending qualification, exercise fresh settings, Send, Ready, direct game-icon launch, normal exit, repeat launch, optional Media setup, and exact streamed-copy Uninstall while confirming originals and saves remain intact. Missing-resident behavior and crash recovery still need broader qualification.

Public reports should contain redacted summaries and synthetic reproductions, not game assets, raw captures, credentials or personal console/PC details. See [contributing](../CONTRIBUTING.md) and [security reporting](../SECURITY.md).

## Unreleased exFAT test

On October 25, 2026, standalone development build `blackwire-exfat-test.02` was built and loaded once on the existing test PS5. Native Dashboard work was paused; no native frontend was loaded. These identities are development artifacts, not published downloads:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone resident | 5772296 | fa937f581359c10f7bd9d968d264fe5d998d1a9d358cc94b30c4aa8f672beea0 |
| Embedded reader | 843696 | 39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441 |

- The PC test inputs were separate copies/views of owned game files. MkPFS 0.0.9 packed them using the reporter's ordinary folder command, producing exFAT inside the PFS/PFSC wrapper. No original game or existing prepared copy was deleted or overwritten.
- The console discovered both new Red Dead Redemption and Need for Speed Unbound images as `pfsc_exfat`, alongside the five existing PFS/PFSC entries in the same flat share. Seven candidates were present after the explicit refresh.
- A fresh Need for Speed Unbound preparation completed: 120 startup files totaling about 209.7 MiB, plus 177 streamed-file placeholders. Reader setup and dashboard registration passed; the operation reported `Startup files, reader and PS5 dashboard registration are ready.` Both startup and dashboard readiness were true. The observed whole operation took 109.421 seconds, not a controlled loading-speed benchmark.
- Web Play started the title. The capture visibly reached its EA/Criterion/Frostbite Continue screen with no system-software error observed at that point. A process alone was not used to claim visible startup. Controllable driving, a direct Games-icon launch, normal exit and repeat launch are not yet qualified on this exact reader.
- The five expected global runtime libraries already existed on this console. Passing setup here does **not** reproduce or fix the reporter's absent-global-library step-7 case, nor qualify their firmware 10.01 environment.

The working released resident and existing prepared games were preserved. See [adapter scope and PC tests](EXFAT-DEVELOPMENT.md). This exFAT-only test does not qualify the separate installer repair recorded below, and is not a replacement release.

## Unreleased self-contained reader setup

On October 25, 2026, standalone development build `blackwire-runtime-bundle-test.01` passed the full SDK build and was loaded once on the same 13.00-family test console. This is a private development test, with no changes to published downloads. Native Dashboard remained paused.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone resident | 5772408 | a54746dd8d6f8ef35e47084511b8ed69a2ab9c14cb95ec5dbbe48a6a30863179 |
| Embedded reader | 843696 | 39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441 |

- The actual released installer fails the absent-global-folder host reproduction before destination mutation. The repaired installer passes the same case with the global folder absent throughout. The production installer contains no global-pack path or file access; optional qualified game companions are handled separately. [Installer and upgrade/removal tests](RUNTIME-BUNDLE-DEVELOPMENT.md#offline-verification) use synthetic fixture bytes and actual C under sanitizers.
- The complete SDK build passes 163 commands and eleven host suites. Source hashes match before and after the build. The embedded reader is identical to the preceding exFAT development reader; the resident installer and bundle verification change.
- The existing Need for Speed Unbound preparation remained Ready after resident replacement. Its exact owned legacy five-file copy was then removed using ordinary authenticated Uninstall, and its source was restored to the library. Both actions completed, and the old game/preparation directories were confirmed absent before a new Send.
- A genuinely fresh Send from the same MkPFS 0.0.9 exFAT image completed in 109.500 seconds: 120 startup files, about 209.7 MiB and 177 streamed placeholders. Reader setup and PS5 registration passed, with startup and dashboard readiness both true. This is a whole-operation observation, not a controlled speed benchmark.
- The fresh `fakelib2` contains exactly one file: Blackwire's embedded reader. Its complete downloaded bytes match the reader hash above, and its mode is 0755. No game companion directory was present in this test source.
- Thirty before/after control-file hashes match: the other five preparations' control files and the five global-pack files are unchanged. The global pack existed on this console and was deliberately preserved; absence is proven by the separate host reproduction, not by deleting shared runtime files on the PS5.
- Web Play launched the reader-only copy, and the capture visibly reached the EA/Criterion/Frostbite Continue screen without an error overlay observed. Normal authenticated Close then completed, the game process disappeared, and the resident returned to `stopped` with no recorded last failure. Controllable driving, direct Games-icon launch and repeat launch remain unqualified for this exact bundle profile; process presence alone was not used to claim visible startup or gameplay.

Firmware 10.01, the reporter's exact payload combination, arbitrary companion libraries and every rejected compressed image remain unqualified. The repair removes an installer dependency; it does not install or replace PS5 system files. Source and build backups were verified before testing.

## v0.3.0-beta.1

The public test beta includes the same exFAT reader and installer repair, plus the bounded setup logger. Its exact artifact passed the following checks on October 25, 2026; these observations supersede neither earlier gameplay records nor the reporter's own environment test.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Blackwire-Beta.elf | 5772536 | 98d35b094f4d2585525434e2af9fab03ed5c71b17e39bd6a1e1aed484ce8d6a9 |
| Embedded reader | 843696 | 39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441 |

- A clean exported-source SDK build passed all 164 commands and twelve host suites with source hashes unchanged during the build. The logging tests cover errno preservation, the size cap, symlink/hardlink refusal, nonblocking FIFO handling and actual directory-failure path/error capture.
- The full actual-filesystem upgrade suite passed 47 cases and 27,934 checks on the beta payload source, after correcting its isolated test fixture's production-header search path. Reader selection passed 25 cases and 8,863 checks, nine actual Send/removal cases passed, and six default-wrapper scenarios passed. This fixture-only correction changes no payload source or tested ELF bytes.
- The exact beta was loaded once on the recorded 13.00-family console. Its existing NFS reader-only copy passed normal Uninstall, its source was restored, and a genuinely fresh preparation completed in 112.750 seconds. Startup and dashboard readiness were true; the fresh reader-only directory matched the embedded reader's bytes/hash and mode 0755.
- The dedicated Send log captured all setup steps 2–13 and a successful `setup-end` with result 0, first failure step 0 and no unconfirmed release. Raw logs remain private; only redacted test facts are distributed.
- Web Play visibly reached NFS's EA/Criterion/Frostbite Continue screen without an observed error overlay. Normal authenticated Close completed and left no game process or recorded last failure. Controllable driving, direct Games-icon and repeat launch are not confirmed for this exact beta.
- Thirty control-file hashes for the other five preparations and the existing global pack matched before/after setup. The shared global files were preserved. The independently reproduced absent-global host case and production global-I/O removal establish the dependency repair; this hardware test did not delete the shared pack.
- Independently relinking the supplied clean objects reproduces the beta ELF byte for byte. Corresponding source, notices, practical relinking materials and checksums accompany the release. The normal Payload Manager catalog and older release downloads are unchanged.

Use [the beta instructions](BETA-TESTING.md) to test original share layouts and retained startup files. No universal firmware/title compatibility or controlled loading-speed improvement is claimed.
