# Observed results and limits

This preview's test record distinguishes copied files, a running game process, a visible menu, gameplay and user-confirmed controls. Reaching a main/start menu counts as a startup pass; a process or loading screen alone does not.

No title-specific compatibility blocker has been identified in testing so far. The limited coverage below does not establish that every title or every stage of a title will work.

## Current update

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

Earlier web-only builds reached a Hot Wheels race and GTA Story Mode loading at 90%. Earlier development reader/resident versions reached RE7 gameplay, a full Hot Wheels round, MK11's start screen and an NFS race. MX reached a loading/startup screen; a completed menu is not confirmed. A full local GTA copy worked; streamed GTA menu/gameplay remains unconfirmed. These observations belong to their actual versions and do not qualify every title on this preview.

## Performance

First preparation depends on game size and file count and can take several minutes or longer. Preparation time, click-to-game time, menu loading, scene loading and gameplay should be measured separately.

In one historical wired NFS comparison, Play to the first menu took 44.42 seconds and later loading to a drivable car took about 140 seconds. A later reader comparison took 55 seconds and about 142 seconds. It did not establish an improvement. Gameplay was reported as normal while menu and scene loading remained slower.

There is no universal speed claim. This preview does not establish compatibility for all games, firmware, Wi-Fi networks, altered global libraries, compressed/container sources or console-crash recovery.

## Release verification

Keep exact source, build, dependency, binary and reader identities with each release. Offline host checks and compiler checks are separate from hardware observations. Before extending qualification, exercise fresh settings, Send, Ready, direct game-icon launch, normal exit, repeat launch, optional Media setup, and exact streamed-copy Uninstall while confirming originals and saves remain intact. Missing-resident behavior and crash recovery still need broader qualification.

Public reports should contain redacted summaries and synthetic reproductions, not game assets, raw captures, credentials or personal console/PC details. See [contributing](../CONTRIBUTING.md) and [security reporting](../SECURITY.md).
