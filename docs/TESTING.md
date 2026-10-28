# Observed results and limits

This record distinguishes copied files, a running game process, a visible menu, gameplay and user-confirmed controls. Reaching a main/start menu counts as a startup pass; a process or loading screen alone does not. Results belong to their exact resident/reader pair.

No title-specific compatibility blocker has been identified in testing so far. The limited coverage below does not establish that every title or every stage of a title will work.

## Unreleased automatic Media startup

The newest development candidate schedules one automatic Media setup worker after the ordinary browser-open step returns. It can start without saved SMB settings, waits for confirmed idle status within a 90-second window and reuses the existing asset, application-inventory, public-SDK and registration-readback checks. An existing registered Blackwire shortcut skips installation; an uncertain result is not automatically replayed. This makes **Payload Manager load → automatic Media setup → SMB configuration** the intended fresh-install flow, without a PC command or separate helper.

Focused current-source host checks passed with ASan/UBSan: coordinator 16 cases/416 checks; actual main worker 11/1,697 with backend/SDK effects modeled; actual backend token accessor 8/476; and unchanged public registration core 17/3,156 with real POSIX inventory and JSON parsing but modeled database rows, SDK calls and clock. Existing actual removal authority checks (31/411) and resident startup checks (8/318) also passed. These are host results. A normal packaged build must pass before console testing, and actual automatic registration and Media visibility remain unverified. Required console observations include fresh setup without SMB credentials, an existing shortcut without another SDK installation, busy startup waiting/refusal and reported terminal status. The earlier authenticated manual-action success below qualifies only that older explicit path. Published downloads are unchanged, publication remains on hold, and no earlier shutdown or Uninstall failure is claimed repaired by automatic registration.

## Unreleased startup ordering and owner lifetime

The latest source requests initial library discovery from the startup resident after both service guards are attached and before its background registration observer can reserve work. Generic constructors retain their previous behavior, and a refused startup request is not automatically retried. Failed construction now retains the callback owner and remaining dependencies if any release cannot be confirmed. These changes address the reproduced startup-ordering and lifetime contracts without relaxing preparation or ownership checks.

The actual-C host fixtures pass **31 cases / 411 checks**, with **8 startup cases / 318 checks** covering the initial request and constructor-failure paths. Four unmount-refusal scenarios pass **12 cases / 7,184 checks**. These scenarios cover busy, uncertain, unchanged-mount and unsupported-release refusals while distinguishing the three post-unmount confirmation domains: registration before source checks, source checks, and registration after source checks. No platform Uninstall is submitted when those confirmations fail, and no retry is added. The fixtures model platform and worker effects; these counts are not PS5 results.

New full normal host/SDK builds, packaged-input checks and a clean console run of this source remain pending. A previous normal artifact passed its PC checks, then the console unexpectedly powered off during its launch test; the saved records did not establish the cause. The later startup-trace experiment and prior private diagnostic preparation/removal passes belong to their own artifacts. This ordering/lifetime repair is not claimed to fix that poweroff or explain the earlier intermittent Uninstall Unknown.

The approximately 73% copy-phase improvement below remains a private one-workload measurement; it does not qualify this new runtime, sustained Ethernet throughput or gameplay loading. The baseline file reader and strict destructive guards remain in place. Public downloads are unchanged, and publication remains on hold.

## Unreleased parser-only preparation profile

A controlled private diagnostic comparison on the recorded 13.00-family console isolated the lifecycle tag scan from the file-reader implementation. Both builds used the same profiler, compressed Alan Wake 2 source, embedded game reader and unchanged baseline `copy_fs` reader. Only the bounded lifecycle tag search changed; fresh observations, strict event folding, errno handling, file checks and authority rules were retained.

| Measured copy phase | Baseline | Parser-only candidate |
| --- | --- | --- |
| Total phase wall time | 151.772 s | 40.328 s |
| Time inside idle observations | 126.867 s | 16.558 s |
| Idle / current-operation / source-read / local-I/O counts | 23,866 / 5,926 / 27 / 6,059 | Identical |

The measured copy phase took **73.4% less time** (3.76 times the baseline rate for this phase). These are exclusive diagnostic wall-time measurements, not wire throughput, a whole-Send multiplier or gameplay loading times. PC cache conditions were not independently controlled, and the result is limited to this one workload and console.

The parser-only build passed both-share setup, discovered 17 sources and prepared the compressed source to Ready with 246 entries and 188,211,299 startup bytes. All four actual module hashes and permissions matched; ShadowMountPlus settings were unchanged. No gameplay was tested in this comparison.

An earlier two-change experiment combined the tag search with a public-SDK unlocked stdio flag shortcut. It refused fresh setup with `SETUP_IDLE_ERROR` on the console despite passing host and SDK checks. That experiment was rejected. The parser-only candidate restores the exact baseline reader, including its original stdio and errno behavior; the shortcut is not part of the proposed repair. The underlying runtime mismatch has not been independently established.

A subsequent normal Uninstall retained Unknown before file deletion because `cleanup_runtime_release_fresh_unmounted_binding_unconfirmed` could not be resolved. No platform Uninstall was submitted. That held test was subsequently cleared with independently verified cleanup. Its Uninstall remains a failed observation and an unresolved release blocker.

After that cleanup, a private diagnostic follow-up added bounded failure logging without changing the release checks. It passed two-share setup with 17 sources and prepared compressed Alan Wake 2 to Ready with the same 246 entries and 188,211,299 bytes; all four actual module hashes and permissions matched. Its first measured copy phase took **41.006 seconds**. Normal Uninstall then completed **259/259 entries**, with exactly one runtime unmount, one manual-source removal and one platform Uninstall. Restore followed by another Send reached Ready; that copy phase took **41.600 seconds**. The second normal Uninstall also completed 259/259 with exactly one of each operation. Neither removal emitted the added leaf failure events. Settings remained unchanged.

Both observed copy phases were approximately **73% shorter** than the private 151.772-second baseline. The repeat retained ownership history and uncontrolled cache conditions, so it is a descriptive repeat, not an independent cold-cache benchmark. No gameplay was tested. The earlier Uninstall Unknown was not reproduced, its underlying trigger remains unknown, and the logging does not constitute a functional repair. Only the tag-search change is proposed for the normal runtime; the baseline reader is retained and the extra diagnostic logging remains private.

This remains **private, unreleased diagnostic evidence**. Full normal-build qualification, exclusion of private instrumentation and testing of the resulting normal artifact remain pending. Existing public downloads are unchanged, and publication remains on hold. The historical normal-artifact results below retain their original scope.

## Unreleased exact-removal recovery

The preceding normal artifact `e827e8ca0df180b7889b23fdac9baa9ebb45ac508887c03f139a8060aae54b55` passed all 57 host suites, 65 outer host commands, 146 SDK commands and 60 packaged-input checks. A clean normal Payload Manager installation connected both shares and discovered 17 sources, including five validated compressed candidates. Folder Alan Wake 2 reached Ready with 246 entries and 188,211,299 verified startup bytes; all four actual library/reader hashes matched. A normal Send for the alternative compressed source was refused before creating a job or copying files, with the owning folder copy preserved.

Normal Uninstall then removed all 259 entries and the registration, but its final original-owner completion check returned `removed_copy_registration_owner_completion_unconfirmed`. The exact Removed completion and a checksum-valid ownership ABSENT marker existed, while the service retained Unknown. General pending candidates intentionally omit completed ABSENT events; using that list alone prevented this recovery from finding its own completed event. That artifact is **held**. Its resident, canonical payload, test metadata and game preparation were removed; original PC data, saves, unrelated apps/payloads and ShadowMountPlus settings/binary were preserved. No compressed preparation or gameplay occurred in that run.

Two controlled actual-C host sequences, with and without the alternative-source refusal, completed normal Uninstall. Thus that refusal is not established as the cause. A separate test injects exactly one transient refusal after a successful real current check when the actual observer publishes ABSENT. The actual observer, normal adapter and backend worker naturally return Unknown with the same diagnostic; a later real current check succeeds. This reproduces a failure class, **not the original PS5 trigger**.

The repair uses a separate read-only exact Removed-owner query, leaves general pending-candidate semantics unchanged, and repeats the existing absence observer and fresh removal capability checks before adopting Done. A locked final control/selection check precedes publication; the unchanged service still requires fresh typed status matching the held action before releasing it. No platform Uninstall is repeated. Valid completed historical generations do not veto the selected normal removal; automatic markers on the exact selected lifecycle still refuse.

Focused private host results: the actual recovery function passes nine cases (three matching completed fixtures, including the naturally produced Unknown fixture, and six source/control/release refusals), with immutable ownership trees and zero mutation requests. The maintained ownership/Removed fixture retains all 109 prior cases and adds 16 query cases: 125 cases, 40,754 assertions, 21 read-only queries, zero writes and at most eight full current callbacks despite additional history. The actual service fixture retains all 27 prior cases and adds failed-release, wrong-source and wrong-title recovery checks: 30 cases and 5,758 assertions. These use explicit platform/process/transport models; they do not establish hardware recovery or gameplay.

Genuine Claude Fable source review found historical-marker, repeated-current-check cost and stale-error-reporting issues. The follow-up confirmed all three corrections with stable source and tools/MCP disabled. Review is not hardware approval. The complete 59-suite/67-command host run, 146-command normal SDK build and 61 packaged-input checks passed. The normal ELF is 5,998,944 bytes, SHA-256 `fe516664f0657bcf66ca203a490cc16a4f0a16ab1e2e4b257e6f50699d1ac7d5`, from frozen source `26ee7aaba264cbe24aff57174be891fcd5d54e83` (runtime `1bd775fb9b7468a57991f31ddcbd5411f63597e1`). Private startup, Send and launch-timing instrumentation is excluded.

On the recorded 13.00-family console, a clean installation of that exact ELF through Payload Manager connected both shares and discovered 17 sources. Extracted Alan Wake 2 reached Ready with 246 entries and 188,211,299 startup bytes; all four actual source/reader modules matched the expected names, hashes and permissions. Sending its alternative compressed copy refused before creating a new job or files, while the extracted copy remained Ready. Normal Uninstall then completed Done and independently verified removal of all 259 entries and the game registration.

The compressed copy subsequently completed normal Send in the same resident and ownership history, with the same four module checks passing. Normal Play produced the expected game/reader process and Running status; the capture window was unavailable, so neither the visible game screen nor gameplay was verified. Normal Close and Uninstall passed. Restore, a browser-only second Send and another Uninstall also passed; all three removals reached Done without an uncertain release. Final inventories confirmed the resident, stored canonical payload, preparations and Blackwire data root absent, with the four unrelated baseline apps and original PC module bytes unchanged. No manual ownership or runtime file was added to make those operations pass.

Preparation speed is **not qualified** on this artifact. The observed compressed Send took about 180 seconds, including approximately 122 seconds in copy/verification, versus about 58 and 23 seconds in the earlier matching-workload record. A repeat without the extra PC status observers was still preparing at 175 seconds and Ready when next observed at 238 seconds; that coarse interval is not an exact completion time. Parser CPU and guard/IO costs are being investigated. These runs establish neither the cause of that slowdown, recovery from an actual hardware Unknown, the original late-failure trigger, gameplay compatibility nor the earlier shutdown cause. The build remains **unreleased and held**.

## Unreleased ownership and idle-admission follow-up

The latest source changes are separate from the earlier `c47da742â€¦` normal artifact below. They add a fresh ownership check before a Send job or file copy, align startup-only removal's advisory with its unchanged same-title ownership guard, and retain the first preparation-idle failure from the same call. Incomplete lifecycle-log tails and failed log closes now refuse. The shared idle fold keeps awaiting handoffs busy, follows valid replacement-PID chains and recognizes the four stock title families. The normal browser action route forwards only exact recognized ownership messages; arbitrary or malformed backend replies retain the previous generic or uncertain handling.

| Focused host check | Evidence and scope |
| --- | --- |
| Initial idle admission and handoffs | 43 cases, 4,032 checks and 60 commands pass with ASan/UBSan and all 77 source inputs unchanged. The original 23 cases retain first/second failures, missing/read/partial/close errors, errno handling, first-failure precedence and legacy refusals. Twenty added cases cover awaiting/direct handoffs, pending same-PID reuse, app identity, malformed/orphan chains, stale old-PID finalization and PPSA/CUSA/LAPY/FAKE records. New refusals occur on the first idle check, before reservation or backend dispatch. Valid final-PID controls keep six idle observations and modeled backend acceptance. |
| Shared idle callers | 38 cases and 24,735 checks pass: twelve native Send cases on folder/PFSC sources, twelve derived/batch guard calls and fourteen startup checks. Default native, derived and batch paths retain two independent log reads; startup process-identity brackets remain. Derived/batch workers are not exercised, and startup process snapshots use a modeled query seam. A logged handoff never transfers the independently qualified allowed PID or its birth identity. |
| Ownership and removal advisory | 336 actual-producer/normal-adapter cases and 524,576 checks pass, retaining the earlier 331 cases. Five new cases use real ownership records emitted by the producer: another binding of the same title, the same binding, a corrupted owner, a linked owner and an inventory-close result that cannot be confirmed. The last case really closes the exact inventory descriptor before reporting an injected failure; the actual collector returns release uncertainty. Its advisory keeps that distinct reason, an unarmed next probe has no sticky state, and the malformed-owner control retains its ordinary unverified reason. The old advisory's reason collapse is preserved in a focused expected-red run of two cases and 22,582 checks. Both preparations, app metadata, ownership records and saves are preserved, and the unchanged action guard refuses without mutation transport calls. |
| Backend admission and reservation | The maintained 36 reported cases pass 24,343 checks. Its additional ten-case actual ownership matrix covers empty/inert/same-binding/completed controls and conflicting, pending, retiring, malformed, linked or release-uncertain inventories. Two corrected-inventory retries succeed in the same resident. A real reserved Send refusal releases its matching outer reservation while retaining the backend's release-uncertainty latch; no preparation job or copy starts on refusal. |
| Joined browser action route | The old service reproduced loss of the ownership advice in an expected-red run with 19,249 parent-process checks. The final combined actual service â†’ reserved backend â†’ ownership-producer route passes 26,866 parent-process checks, including later-request reset, exact-message and malformed/duplicate/wrong-type/missing/unrecognized reply controls. Two early-cache controls change fixture state after catalog creation to reach the actual pending-cleanup and unknown-ownership Send branches; a newly blocked catalog can ordinarily disable Send before that request. Two isolated unknown-result child controls separately check transport failure and malformed acceptance: they retain authority and exit without claiming child cleanup. Those child controls are not included in the parent check count. |

These tests use synthetic, non-executable source bytes, fixed host settings/idle observations and modeled platform registration or transport effects. Initial idle tests model catalog/authentication and backend acceptance; the joined ownership test executes the real native backend and ownership collector. They do not qualify live ShadowMount behavior, a controller, gameplay or another firmware profile. Log parsing is read-only; a matched finalizing record is only the inherited logical endpoint, not proof of completed ShadowMount cleanup, runtime release or a live process census. This work does not establish the cause of the reporter's exact idle refusal or the earlier console shutdown/access loss.

Before the shared-parser repair, the 56-suite/64-command host run and 146-command normal SDK build passed. That earlier `8c38â€¦` artifact remains held: its tests missed the handoff/family cases, so those passes do not qualify the new parser or authorize deployment.

That follow-up subsequently passed its 57-suite/65-command full host run, 146-command normal SDK build and packaged-byte checks, then reached folder Ready and correctly refused a conflicting source on console. Its later Uninstall confirmation failed; the artifact remains held as recorded in the exact-removal section above. These changes are **unreleased**. The earlier c47 preparation measurements and console results do not qualify them. A successful live **Remove startup files only** operation on an eligible step-8 conflict remains unqualified; ordinary Uninstall and safe refusal are different results. No source-owned module, stock ShadowMountPlus setting or destructive ownership guard was relaxed to obtain these host results.

## October 27 final-publication candidate

Normal resident: **5,963,456 bytes**, SHA-256 `c47da742114b135e649ffd91179555eb7055b14f602b1487c044ee46c84c59a0`. Runtime source commit `fa620d45c615b869b9a1a319910c757e9b462a0b`; frozen build/pin commit `1c795da2421e7ee8181aae78c53e7e65f2a9cf71`. The 875,296-byte embedded reader remains `0873276751a10ad5eed00a59104597798d9256703fd8e0ab20b742b3c02d87fc`, and the embedded page is `91e4a0bfdcbe46e88326bee74571f0883e45525666b7e70010ce2a1935aa7fd0`.

The full 54-suite/62-command host run and 146-command normal SDK build passed with identical frozen source snapshots. Independent packaged-byte checks passed all 59 gates, including normal diagnostic exclusion, source/runtime/dependency integrity and the actual 210-case/414,739-check publication regression. Genuine Claude Fable reviewed the actual production diff and found no introduced defect in that scope. Review does not establish hardware safety or an unexplained crash's cause.

The candidate was installed from the single stored `Blackwire.elf` through normal Payload Manager controls after verified complete removal of the preceding test. Downloaded stored bytes and served page matched before testing. Both existing shares connected. Fresh compressed Alan Wake 2 reached Ready with **246 entries and 188,211,299 bytes** of verified startup data, including setup step 7 and dashboard registration. Normal Refresh kept Play available; explicit normal UI Play visibly reached **Game Saving / X Continue** without an error. This is an interactive intro, not a verified main menu or controllable gameplay. Normal Close and confirmed Uninstall succeeded, followed by verified removal of its resident, canonical payload and Blackwire test root.

A later clean installation of this same candidate also passed the failed-secondary-share correction: the error identified the missing share, the saved configuration stayed byte-identical, and correcting only the share name connected without reentering credentials. A disposable, readable image with an invalid header produced six candidates, five validated images and one `unsupported_format` rejection at `container_open`. Removing that fixture and using normal Refresh restored five validated images and zero rejections. This checks a nonfatal format rejection, not a connected-file permission denial or a failed whole-library scan.

Fresh compressed Alan Wake 2 preparation, normal Uninstall, Restore and a second Send all passed on this exact candidate. Stopping the idle resident and loading the same stored ELF through Payload Manager preserved Ready and Play after Refresh. Two subsequent normal launches reached Game Saving / X Continue, with successful normal Close after each and no reported launch failure. The final normal Uninstall removed all 259 prepared entries; independent inventories then confirmed removal of the game registration, resident, stored payload and Blackwire test root while preserving the original applications and unrelated payload slots. These observations do not qualify the main menu, controllable gameplay or every later asset read. The distinct **Remove startup files only** action has host coverage but no real eligible step-8 conflict on this clean console; ordinary Uninstall is a separate check.

The PC diagnostic collector now retains three authoritative container counts and five per-item rejection/namespace fields. All seven maintained collector tests pass; the same tests reproduce failures against the prior collector. Genuine `claude-fable-5-1` review found no introduced defect in that two-file change. The real invalid-image capture also preserved all eight fields without including credentials, tokens or source bindings. This PC-only collector change does not alter the resident, reader or embedded page above.

A subsequent clean installation tested extracted-folder Alan Wake 2 through normal Send and Play. It reached Ready with the same 246 entries and 188,211,299 startup bytes. Actual console downloads confirmed four module hashes: the source's 254,326-byte Ampr and supported 11,590-byte PlayGo 0.4 nolog module remained byte-identical in `fakelib`; the owned `fakelib2` contained the matching PlayGo alias and the 875,296-byte embedded Blackwire reader. The source modules and its excluded original index retained their hashes, sizes and exact timestamps. Normal Play reached Game Saving / X Continue without a reported or visible error; later asset delivery and gameplay remain unverified. Normal Close and Uninstall completed, and independent inventories confirmed removal of the preparation, registration, resident, canonical payload and test root while preserving the original apps and unrelated payload slots.

A normal old/new/old comparison used the same compressed source binding and image generation, with each preceding console preparation, resident, stored payload and test root removed before installing the next. The old normal resident is `2325ad834458c2fa25ccff7a8384ffc877537af13ccc7385ff6374e888852d7d`. One-second, GET-only PC observations yielded:

| Sampled interval | Old A1 | Candidate B | Old A2 |
| --- | ---: | ---: | ---: |
| First running job observation â†’ Ready | 65.031 s | 58.015 s | 66.000 s |
| Copy and verification | 31.031 s | 23.000 s | 32.000 s |
| Reader/registration setup | 28.000 s | 28.015 s | 28.000 s |

The observed centers imply **26â€“28% less copy-phase time and 11â€“12% less total preparation time** in this case. The nominal one-second Send brackets are 64.031â€“66.031, 57.015â€“59.015 and 65â€“67 seconds respectively; conservative request-latency bounds are wider. These are sampled elapsed intervals, not statistical confidence intervals. All three reached the same verified file/byte totals. Source caches were not flushed, general PC load was not continuously measured, and only one candidate run was measured. Saved invocation/commit/build timestamps show the first baseline finished before the full builds began; compiler overlap is not a supported explanation. The approximately 28-second registration/setup portion did not improve. No raw SMB throughput, physical SSD traffic, game-loading multiplier or universal performance improvement is claimed.

All three test copies were uninstalled normally. Final independent inventories showed no Blackwire resident, game process, stored payload, test root, game app directory or ShadowMount manual/library entry; unrelated apps, payloads, original PC images and saved data were preserved. Stock ShadowMountPlus's binary and settings were not changed. Evidence and rollback files remain on the PC.

Publication remains on hold. The earlier loss of console access remains unexplained; successful later loads do not establish its cause. A reused desktop automation tab sometimes stopped accepting input after settings closed, despite no inert attribute or covering page layer; Details and Help activation, including Enter, had no effect in one fresh-settings page. A fresh page worked, and a later saved-settings check on that reopened page also preserved working Help controls. In a separate preserved-page comparison, the user reported that Help opened with the actual mouse; subsequent automated Details activation also worked in that tab without a product-code change. This narrows the observed symptom to the automated interaction context rather than establishing broken natural Help controls; the exact control-path cause is not identified. The actual-page headless regression also passed. The earlier pre-acceptance idle-authority refusal remains without a reproduced reporter-specific root cause or targeted repair; successful local Sends do not prove that report fixed. Literal Games-icon launching, controller gameplay and the reporter's firmware/library combination are not qualified for this exact candidate.

## Unreleased image access and metadata reads

An earlier normal browser-workflow candidate uses runtime commit `7208d30ba9e571a8f1ed018e4bd57a4aca75514f` and source pins at `58a171caecde3a2825f38f37eb407351a5216170`. Its 5,963,456-byte resident is SHA-256 `2325ad834458c2fa25ccff7a8384ffc877537af13ccc7385ff6374e888852d7d`; the reader remains `0873276751a10ad5eed00a59104597798d9256703fd8e0ab20b742b3c02d87fc`. The complete 54-suite/62-command host run and 146-command normal SDK build passed with identical frozen source snapshots. Independent packaged-byte checks confirmed that private tracing, launch timing and verbose Send logging are excluded. These are local development artifacts, not a published release.

A clean normal Payload Manager install passed startup and the two browser repairs. After a deliberately failed secondary-share check, correcting only that share reconnected without reentering the unchanged account or password. A fresh compressed Alan Wake 2 preparation reached Ready with 246 entries and 179.5 MiB. Its observed Send interval was approximately 62â€“64 seconds; cache conditions were not controlled. Refresh retained the exact current entry's Play capability despite its historical Send byte-verification flag being cleared. Explicit normal UI Play reached the visible Game Saving/Continue screen without an error; controllable gameplay was not checked and no delivered-network-byte counter was available. Normal Close, confirmed Uninstall and Restore passed. The preparation, app registration, resident, canonical payload and test data were then removed; unrelated tools, saves and original PC files were preserved.

A separate private build with the existing opt-in Send logger attributed one complete same-source preparation at 64.492 seconds: 3.889 seconds hashing startup source data, 31.046 seconds copying/publishing local files and 27.463 seconds provisioning. Registration occupied 19.052 seconds. A matching bounded ShadowMount log tail reported that the new source was not yet old enough to scan, followed by installation and successful registration. This identifies a registration delay in that attempt; it is not a normal-build speed comparison, a raw SMB-throughput result or authority to bypass source stability checks. The private build and its game preparation were also fully removed, with evidence retained on the PC.

An actual-page regression in a disposable desktop Chromium profile passed Save/Cancel/reopen, selection, Details/Back, failed-share correction and keyboard submission. An input stall observed in reused automation tabs was not reproduced there; its cause remains unconfirmed. No speculative modal or inert-state change was added.

A subsequent focused publication-copy repair passes 210 actual-producer cases and 414,739 ASan/UBSan checks with stable sources. Stage-selective faults first reproduced 28 cases where the old final-file writer attempted a write after callback-time parent or file-identity substitution; the repair blocks those attempts. In a 1,049,307-byte positive fixture, final-file write calls decrease from 17 to 5 while temporary-file writes, the 64 KiB source-consumer contract and exact two-write/three-read byte totals remain unchanged. Every final write and partial/interrupted retry reacquires current authority, then checks its held parent and both file identities. The existing 256 KiB buffer is reused, so no new body memory is allocated; an individual final write may now be 256 KiB between checks. These host counts are not physical SSD traffic or an elapsed-speed result. The normal candidate build and console comparison are recorded above; those results remain limited to that exact artifact and test case.

Earlier development evidence follows; its pending checks refer to those earlier artifacts.

The normal candidate tested on October 27 is built from runtime commit `f54370e3fcff2a7e576a44ad776b2a38381b89ac`, with source pins at `edc6bb8ddbd7cb95abd3a781988b8dd159ca0a24`. Its complete 54-suite/62-command host run and 146-command normal SDK build passed with matching frozen source snapshots. Independent packaged-byte checks passed, including disabled private tracing, launch timing and verbose setup logging. The resident is 5,963,456 bytes, SHA-256 `bfdefc152f1755dc3837dba0526324b473a4eb96140550be8bd386ced11ec375`; its 875,296-byte reader remains `0873276751a10ad5eed00a59104597798d9256703fd8e0ab20b742b3c02d87fc`.

Its first Payload Manager load was followed by loss of console-tool access. Video capture was already unavailable before loading; the console's power state and the cause remain unconfirmed. No game preparation was submitted in that attempt. After recovery, the stored candidate's exact bytes matched the qualified PC ELF. The surviving crash dump identified a separate `no_limits.elf` fault during recovery setup; it did not establish the earlier failure's cause.

A captured startup comparison with the earlier `5ebea4c...` resident passed. After its verified removal, a private diagnostic variant reached all fourteen persisted initialization checkpoints through the request loop and returned health. That diagnostic variant was also removed. A subsequent clean install of the exact normal `bfdefc...` candidate through Payload Manager passed captured startup and connected both shares. These successful runs do not explain or repair the earlier loss of access.

The private startup diagnostic uses bounded file checkpoints. Its file writer disables that transport after an incomplete row instead of appending another checkpoint to a fragment. The original helper reproduces the refusal; the repair passes 17 cases and 397 sanitizer checks, including short writes, interruptions, write/close failures and continued independent stdout transport. This diagnostic-only change does not repair the unexplained startup failure, and its code is excluded when ordinary startup tracing is disabled.

The normal `bfdefc...` candidate accepted all five compressed images. Fresh compressed Alan Wake 2 preparation reached Ready with 246 entries and 179.5 MiB, including reader and dashboard registration. One-second PC observations bound its job at approximately 62â€“64 seconds. This is descriptively shorter than the earlier 68.725â€“70.142-second run below, but cache conditions were uncontrolled and no isolated speed improvement is claimed. Refresh preserved the exact compressed entry's startup readiness and Play capability. It also exposed a separate UI defect: inactive completed Send history still blocked Play, so no launch or gameplay was accepted in this test. Normal typed-API Uninstall removed the preparation and registration; normal UI Restore succeeded. The resident, canonical slot and Blackwire data tree were then removed, with unrelated tools and saves preserved.

That run also reproduced username loss after a failed secondary-share check: saved settings remained intact, but correcting only the share submitted blank credentials. Focused actual-script regressions cover the username-draft repair and the completed-Send UI repair, including changed-PC/account refusal, active/unknown operations, incomplete progress and mismatched entries. Both repairs subsequently passed the clean normal build and live retry/Refresh/Play checks recorded above. Neither UI repair changes byte-verification or backend launch authority.

One normal candidate using the metadata window was installed through Payload Manager on the recorded 13.00-family console. Both configured shares connected and the combined library returned 17 entries. A fresh compressed Alan Wake 2 preparation completed 246 rows and 179.5 MiB of startup data, passed the reader/source-library setup check at step 7, then refused step 8 with uncertain registration ownership. Its preexisting console app directory retained metadata but had no `mount.lnk`; an empty ShadowMount library did not establish platform absence. The database contents were not independently obtained, so the earlier underlying database observation is unknown. This attempt did not reach Ready or gameplay and does not qualify the later registration-error and preparation-only cleanup changes.

The failed candidate and its preparation were removed before another build: the resident and canonical Payload Manager slot were removed, the existing Alan Wake title was uninstalled through the normal platform service, and the app directory and Blackwire test root were independently absent afterward. No raw database edit, replacement library/mount file, source deletion or saved-data deletion was used. Independent database absence and fresh preparation still need verification in the next normal install. No successful matched PS5 preparation-speed comparison has been established.

A subsequent normal candidate (`5ebea4c33794d9102a24185997894b78ca7a3177ac5db198590c78c60098e938`, 5,963,416 bytes) passed 52 host suites, 60 host commands and 146 SDK commands with identical frozen source snapshots. Its actual packaged bytes excluded private trace, timing and verbose setup instrumentation. It was installed through Payload Manager and connected both real shares, returning 17 library entries. Compressed Alan Wake 2 reached Ready with 246 preparation entries and 179.5 MiB of startup data. Normal Play visibly reached its Game Saving/Continue screen; gameplay was not checked. Normal Close, confirmed Uninstall, independent app/game/preparation-directory removal, Restore and a second fresh Send succeeded. The second Ready transition was observed between 68.725 and 70.142 seconds after Send. This is a successful preparation baseline interval with uncontrolled cache conditions, not a speed-improvement claim.

The next Refresh of that earlier resident completed discovery with all five compressed images accepted and none rejected, but the prepared compressed entry became Unknown. The actual reload wrapper reproduced that failure: it applied a legacy-only hint to a valid sealed policy3 projection. Another actual legacy case showed the wrapper clearing an existing upgrade requirement. The source repair passes 85 actual-producer reload cases and 97,115 checks, including corrupted-material refusals and legacy controls. Reload leaves `bytes_verified` false; later content, source, reader and registration checks remain required. Targeted genuine Claude/Fable review covered these two reproduced repairs. The normal `bfdefc...` test above confirms retained catalog readiness; the separate UI history repair and retained launch still need verification.

A separate 99-case actual native-copy fault suite exercises syscall failures, short writes, interruptions, collisions, substitutions, readback corruption, uncertain cleanup and abrupt host-producer termination. The existing staged path measured two local writes and three local reads per accepted body byte. These are syscall-byte measurements, not physical SSD traffic, power-loss qualification or a demonstrated PS5 speed increase.

The subsequent native-write change removes repeated authority observations while retaining a fresh full current-operation check followed by the named/held parent check before every actual write attempt, including short-write and interruption retries. Its expanded actual-code suite passes 126 cases and 204,862 checks. Eighteen matched cases against the prior production source preserve DATA checks, write attempts and the two-write/three-read verification work; a 17-piece body reduces current callbacks from 34 to 17 and idle callbacks from 51 to 17. These callbacks model the supplied authority boundary, not production settings-query or process-enumeration time. Targeted genuine Claude/Fable review found no confirmed defect under this contract. The full builds above passed; a normal PS5 timing comparison remains pending, and callback reductions do not establish a preparation-speed gain.

The later preparation-only removal repair passes actual producer/receipt/core tests (111 cases), normal-adapter tests (331 cases), asynchronous backend tests (31 cases), service tests (27 cases) and the updated page tests (92 cases plus 19 browser-handoff cases). The normal-adapter suite exercises the real source-exclusion parsers and filesystem code with fixed transport replies and a modeled outer idle callback; it is not a live ShadowMount or controller test. It confirms that no title-wide uninstall, unmount or manual-list mutation is submitted by this separate action. Its regressions cover an actual interrupted ordinary Uninstall followed by a new resident operation, altered or linked history, same-title copies from another share, completed Uninstall/Restore/fresh Send, and different-title app links into either protected root. Linked, malformed and changing app inventories refuse removal; valid separate apps are preserved while the selected startup copy is removed. These tests use synthetic non-executable game and reader bytes.

Twenty-two actual filesystem/SQLite observation cases also distinguish missing app links, database conflicts, query failures and changing observations without granting ownership or changing legacy registration states. They distinguish a statement-finalization result repeating an interrupted query from a separate release failure. These repairs are included in the full-build candidate above; their console qualification remains pending.

The current revision distinguishes a failed SMB connection from an image-access failure after the share connects. Its error provenance retains the selected share, relative image path, failing stage/operation and available transport result. Directory listing success does not prove the same account can read that image; check the [share and file permission layers](SETUP.md#a-listed-image-still-needs-file-read-access).

An actual five-image PC SMB A/B comparison reduced metadata read requests from **1,790 to 56** with the bounded 128 KiB metadata window. One same-PC elapsed-time observation changed from **0.386 to 0.206 seconds**. This is a PC metadata/discovery measurement, not a PS5 loading or preparation benchmark; it does not qualify sustained transfer speed or later gameplay. The [window's limits and retained checks](COMPRESSED-DEVELOPMENT.md#unreleased-metadata-read-window) apply.

PS5 preparation-speed and gameplay qualification for this revision remain pending. Earlier console results below belong to their named binaries and do not qualify this changed reader. Ordinary release builds keep private startup traces, launch timing and verbose setup diagnostics disabled; private diagnostic runs do not change that release policy. The recorded lifecycle profile remains the 13.00 family; 10.01 lifecycle behavior is not qualified, and firmware alone has not been established as the cause of the reported step-8/9 failures.

## v0.3.0-beta.2

Exact standalone resident: 5,772,600 bytes, SHA-256 `ad3505cca3c6b409152e8c12024d81582a351aee473d7fecee4d82bc4e1e252c`; build ID `blackwire-v0.3.0-beta.2`. The reader remains the beta.1 reader `39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441`. This release repairs settings reporting and client recovery; the native Dashboard is excluded.

| Check | Observed result and scope |
| --- | --- |
| Clean SDK build | 167 successful commands, fifteen host suites, unchanged source throughout. |
| Actual-page behavior | 91 UI tests and 19 browser-handoff cases passed. An accepted settings check followed by a resident-token change reproduced the old stuck form; the repair releases its stale watch, keeps the share draft and requires explicit reload/save without request replay. |
| Startup settings | Actual startup functions with a held scan worker pass seven cases covering valid, absent, invalid and unsafe settings, an already active worker, legacy loading and worker-start failure. The old code fails the valid-saved-settings case. |
| Private settings files | Actual persistence code saves and reads a fresh disposable configuration; linked, invalid and oversized files remain refused. Seven filesystem cases pass under ASan/UBSan, with credential-free failure diagnostics. Two-share regression: forty cases and 7,770 checks. |
| Console startup | On the recorded 13.00-family console, valid saved settings were already reported while the initial scan was running. Seven library entries returned; settings became editable after discovery. |
| Failed connection | A deliberately nonexistent test share failed at SMB connection. The settings file's bytes and metadata stayed unchanged, the fields unlocked, and the new diagnostic identified `smb-connect`. |
| Valid connection | An explicit check of the existing share passed and returned all seven entries in about 14.3 seconds. Unchanged settings were kept. This is a functional observation, not a comparative speed benchmark. |
| Preserved state | Thirty-five preparation/global control-file hashes match before and after; saved settings and the matching served page were checked. No preparations were removed for these tests. |
| Collector | Read-only console summary includes the new setup status and excludes the saved password, server address and browser token. |

The reporter's remaining no-games/settings failure has not yet been confirmed on their environment. A separate Reddit report says preparation was refused before acceptance because frontend/idle authority could not be retained; beta.2 does not claim a verified repair for that specific refusal. Collect its exact build ID, browser context, payload versions and lifecycle evidence. Gameplay, physical controller/browser-exit behavior and controlled loading-speed comparisons were not repeated for this resident.

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
| Automatic fresh Media setup | Not integrated in this tested preview. Its explicit-action result does not qualify the newer [automatic startup candidate](#unreleased-automatic-media-startup). |

Routine resident status does not expose a game-to-controller data witness for every reader path. Its unavailable counters must not be reported as zero; the independent Hot Wheels observation above is separate evidence.

## Earlier results

Earlier web-only builds reached a Hot Wheels race and GTA Story Mode loading at 90%. Earlier development reader/resident versions reached RE7 gameplay, a full Hot Wheels round, MK11's start screen and an NFS race. MX reached a loading/startup screen; a completed menu is not confirmed. A full local GTA copy worked; streamed GTA menu/gameplay was not confirmed for those folder-only artifacts. Later compressed GTA gameplay is recorded separately above. These observations belong to their actual versions and do not qualify every title on this preview.

## Performance

First preparation depends on game size and file count and can take several minutes or longer. Preparation time, click-to-game time, menu loading, scene loading and gameplay should be measured separately.

In one historical wired NFS comparison, Play to the first menu took 44.42 seconds and later loading to a drivable car took about 140 seconds. A later reader comparison took 55 seconds and about 142 seconds. It did not establish an improvement. Gameplay was reported as normal while menu and scene loading remained slower.

There is no universal speed claim. This preview does not establish compatibility for all games, firmware, Wi-Fi networks, altered global libraries, arbitrary container layouts or console-crash recovery. The indexed cache and PFSC batching have host measurements; those are not PS5 load-time benchmarks.

## Release verification

Keep exact source, build, dependency, binary and reader identities with each release. Offline host checks and compiler checks are separate from hardware observations. Before extending qualification, exercise automatic Media setup after a normal Payload Manager load, fresh settings, Send, Ready, direct game-icon launch, normal exit, repeat launch and exact streamed-copy Uninstall while confirming originals and saves remain intact. Missing-resident behavior and crash recovery still need broader qualification.

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
- The dedicated Send log captured all setup steps 2â€“13 and a successful `setup-end` with result 0, first failure step 0 and no unconfirmed release. Raw logs remain private; only redacted test facts are distributed.
- Web Play visibly reached NFS's EA/Criterion/Frostbite Continue screen without an observed error overlay. Normal authenticated Close completed and left no game process or recorded last failure. Controllable driving, direct Games-icon and repeat launch are not confirmed for this exact beta.
- Thirty control-file hashes for the other five preparations and the existing global pack matched before/after setup. The shared global files were preserved. The independently reproduced absent-global host case and production global-I/O removal establish the dependency repair; this hardware test did not delete the shared pack.
- Independently relinking the supplied clean objects reproduces the beta ELF byte for byte. Corresponding source, notices, practical relinking materials and checksums accompany the release. The normal Payload Manager catalog and older release downloads are unchanged.

Use [the beta instructions](BETA-TESTING.md) to test original share layouts and retained startup files. No universal firmware/title compatibility or controlled loading-speed improvement is claimed.
