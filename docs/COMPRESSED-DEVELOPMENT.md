# Compressed streaming development

The published `v0.1.0-preview.1` remains the working folder-only preview. This development branch adds a compressed source to the existing preparation and early-reader workflow. GTA V and Resident Evil 7 reached visible gameplay from the supplied compressed images on the test console. After identifying an unrelated background Game Compressor process consuming a CPU core and stopping it, the same reader completed four clean initial launches. Full gameplay, dashboard launches and shorter loading times still require controlled console qualification.

## Formats

The candidate accepts unsigned contiguous D32 PFS, PFSC containing that PFS layout, and supported one-file PFS wrappers. The supplied GTA V and Resident Evil 7 `.ffpfsc` samples use a PFS wrapper around PFSC-compressed PFS. Both were inspected read-only and compared with their independently unpacked folders.

An extension is not proof of format support. Compressor versions can also produce exFAT images. exFAT, signed/encrypted PFS, scattered extents, wide inode layouts and individually compressed inner files remain unsupported. Unsupported or corrupt sources must refuse preparation instead of appearing Ready.

The original image remains on the SMB share. Preparation enumerates logical files, copies startup data onto the PS5 and creates sparse placeholders there. Streamed reads use exact map IDs bound to the image's logical files; those IDs are not assumed to equal PFS inode numbers. No PC decompression server or write access to the share is required.

The folder containing a compressed image must be reachable inside the selected SMB share. A sibling outside the share is not automatically exposed. Keep existing share permissions and source files intact while testing a separate source configuration.

## Loading changes

- A fixed hash index replaces repeated scans of the 1,024 RAM-cache slots. The 64 MiB payload budget, eviction order, exact EOF lengths and failed-load behavior are retained.
- PFSC groups adjacent compressed blocks into bounded physical reads of at most 256 KiB. Batching stops at the end of the current requested logical range; it does not fetch unrelated future blocks.
- The existing folder reader already combines cold blocks within a request up to 1 MiB. That behavior is retained.
- The strict source policy remains enabled. Cached folder and container reads still check the exact current source generation. Signing remains enabled.
- Existing prepared games can use the ordinary guarded upgrade from the published preview's exact reader size and SHA-256. Unknown reader binaries remain refused; accepting a filename or an approximate match would be insufficient.
- A prepared compressed image uses its sealed image generation and logical map for launch validation. It does not hash every remote startup body again just because Play was pressed. Local required startup files still pass the retained receipt checks.

Measured host results:

| Check | Reference | Candidate | Scope |
| --- | ---: | ---: | --- |
| GTA, 64 MiB sequential decoded ranges | 1,038 source reads | 275 source reads | Local-file range adapter, decoded bytes compared |
| RE7, 64 MiB sequential decoded ranges | 1,030 source reads | 263 source reads | Local-file range adapter, identical physical byte count |
| One million cache lookups, median of three runs | 250.8 ms | 22.6 ms | Optimized PC CPU microbenchmark |

These are request-count and host CPU measurements. They do not establish an equivalent reduction in PS5 menu or game loading time. A real SMB connection adds signing, transport latency and server I/O; console decompression also consumes CPU.

## Verification

- The container decoder compared 172 GTA files and 50 RE7 files, with start, middle and EOF ranges read from each file.
- The actual early reader compared 171 streamed GTA files and 49 streamed RE7 files through a modeled SMB transport. The boot executable remains LOCAL. Tests exercise positive short replies, a warm-cache read with strict generation checks, cancellation, changed-source refusal and draining a simulated pending destination before freeing its transport.
- Compressed-image preparation and launch validation now retain the caller's transport owner. Read/stat failures drain and clear that connection before decoder workspace is freed; failed closes also clear ownership. Dedicated fault cases verify both pointer cleanup and callback destination lifetime under sanitizers.
- Synthetic fixtures exercise plain PFS, bare PFSC, compressed wrappers, plain wrappers, reordered IDs, equal-size files and EOF tails. Corrupt checksums, invalid tables, signed PFS, exFAT and truncated headers are refused.
- The indexed cache passed 30,000 differential operations against the unchanged released implementation, including eviction, partial blocks and failed loads: 155,549 checks under address/undefined-behavior sanitizers.
- Home-launch source selection and Send-completion provisioning have focused host cases for compressed identities. Sony registration and process operations in those tests are modeled.
- Container-identity Uninstall/Restore passed 216 host cases and 32,806 checks using actual temporary prepared files and read-only SQLite queries. The normal console registration API is modeled; hardware removal remains pending.

The existing `metadata/build-manifest.json` records the published preview's build. Development builds write their own command, input, reader and package records beneath their new `build/<name>` output directory.

Read-only host comparisons can be reproduced through Linux or WSL:

```sh
python3 -B tools/verify_container.py IMAGE.ffpfsc UNPACKED_FOLDER --sequential-mib 64
python3 -B tools/verify_container.py IMAGE.ffpfsc UNPACKED_FOLDER --sequential-mib 64 --no-read-ahead
python3 -B tests/compressed/check_reader.py --image IMAGE.ffpfsc --folder UNPACKED_FOLDER --output build/new-reader-check
python3 -B tests/compressed/check_synthetic.py --output build/new-format-check
```

Each evidence output must be new. Test fixtures contain no game material. The optional real images and unpacked originals are never modified or copied into the repository.

## Required hardware checks

1. Use a separate development payload and test configuration while retaining the released ELF and settings backup.
2. Prepare one supported compressed image through ordinary Send. Verify the game icon is registered before Ready.
3. Launch through Blackwire, then repeat directly from the regular PS5 Games tab. Verify startup, a menu transition and actual gameplay.
4. Exit normally and relaunch. Check missing-share behavior and cancellation without repeated registration or reader work.
5. Uninstall the owned PS5 prepared copy through its normal confirmation. Verify the PC image and saved data remain intact and the old PS5 icon is removed.
6. Compare the same menu and scene selections against the folder release, using both reader counters and visible stopwatch times. Record failures as well as successes.

The observed gameplay does not complete this checklist. Clean launch/relaunch, removal and controlled loading-time comparisons still require qualification. The working released build should not be replaced automatically.

## Folder-to-container registration regression

Live preparation exposed an old Removed record being matched to a new prepared copy by title alone. The development resident now also matches the exact source binding before blocking or observing that Removed owner. The focused regression first failed against the old implementation and passes with the repair; real pending events for the title remain blocked. This does not adopt or delete foreign registrations. Both compressed test copies became Ready after deploying the repair without copying their startup files again.

## System-dialog scheduling trial

Both compressed games continued playing after dismissing CE-108262-9. Kernel records identify `SceCdlgApp` (`NPXS40093`), its `sceCdlgWatchDog` thread and `Watchdog:55`; the reader counters report no failed reads in those captures. A GTA folder comparison with the identical reader SHA-256 completed dialog initialization and remained at its Continue prompt without that failure during a 130-second capture. This compares the initial launch stage, not equivalent full gameplay or menu timing.

The upstream I/O worker uses priority 384. Compressed runs show sustained CPU use on that worker while the dialog main thread uses priority 610. Development build 3 tests priority 700 for container sources only, retaining upstream folder priority and affinity. It logs the requested priority and the actual setter result. The trial does not suppress the dialog or its watchdog.

Hardware testing confirmed that priority 700 is not a complete fix. One RE7 launch reached its title screen with no watchdog failure during a 151-second capture, but GTA reproduced the common-dialog watchdog failure after the priority setter returned success. Gameplay and later menu transitions were not tested with this trial reader. The cause remains unresolved.

GTA's 24 local startup files (174,907,527 bytes), including its executable and trophy data, were downloaded read-only from the prepared compressed copy and fully SHA-256 compared with the independent unpacked folder. Every file matched. The failing dialog's crash record lists 122 loaded modules and contains no Blackwire or Ampr reader module. These checks narrow the investigation; they do not establish that every streamed read or system interaction is correct.

The diagnostic resident leaves the PS5 browser closed when loaded. Tests send the ordinary Play request from the PC; the Media shortcut can still open the PS5 page explicitly. Existing prepared copies can receive the trial reader through the normal upgrade from the exact read-back development-2 reader size and hash. Unknown predecessors remain refused.

## Background compressor comparison

The priority-only trial reproduced GTA's common-dialog watchdog failure four times. A read-only process/CPU capture then identified the separately running `game-compressor.elf` using about 96% of one CPU core while idle and about 62% during a failing launch. Its thread name resembled a system discovery service, so the process executable and PID were checked before attributing that load. Its status endpoint was unavailable; a running compression job could not be ruled out from that endpoint.

With the console owner's authorization, only that payload was stopped through Payload Manager. No Blackwire binary, prepared reader, watchdog, Sony process, autoload entry or console setting changed during the comparison. Both prepared readers were read back and matched development 3's SHA-256, `3b91a0b41203ce15afe40937dfe4e2b13c289f6fdfeedf11076e4c86eb7b6033`.

| Unchanged reader, compressor stopped | Observation | Dialog initialization | Watchdog failures | Reader failure markers |
| --- | ---: | ---: | ---: | ---: |
| GTA launch 1 | 120 seconds | 3.96 seconds | 0 | 0 |
| GTA launch 2 | 120 seconds | 0.70 seconds | 0 | 0 |
| RE7 launch 1 | 150 seconds | 5.20 seconds | 0 | 0 |
| RE7 launch 2 | 150 seconds | 5.26 seconds | 0 | 0 |

Video showed GTA's Continue prompt and RE7's Start Game screen. These are dialog initialization measurements, not time to drive or enter gameplay. The result supports background CPU contention as a contributor; it does not prove a sole cause, qualify later loading transitions or test the cancellation repair below. The unwanted compressor remains stopped.

## Canceled compressed reads

An independent Claude CLI review found that request cancellation was being interpreted as a permanent container-generation failure. A modeled-SMB regression reproduced this: an uncached read canceled after a positive transport reply returned `-ECANCELED`, but the next request on the same immutable source returned `-EIO`.

The development source now distinguishes explicit request cancellation from a changed source or a real transport/decoder failure. An interrupted lazy open releases its own partial handle and decoder workspace on the same worker; an admitted session can serve the next request without reconnecting. Canceled reads never publish a valid completion witness. Genuine transport failures still destroy the connection before retained destinations are freed, and failed cleanup remains permanent.

A follow-up review caught an overlapping error value and a late-cancellation precedence error in the first draft. Both were reproduced before correction. Cancellation now has a distinct result value; manifest mismatches and genuine decode/unsupported-format failures retain their original failure behavior even if cancellation arrives at the final check. Refusal tests require the next request to make no further SMB calls.

The corrected source passed these host checks:

- Ten synthetic format cases, including four supported layouts, six corrupt/mismatched/unsupported refusals, cancellation during lazy admission and payload reads, final-fence cancellation, and simultaneous real errors. Address and undefined-behavior sanitizers remained clean.
- GTA: 171 streamed files, 513 compared ranges and seven cancellation/recovery cases against the independent unpacked original.
- RE7: 49 streamed files, 144 compared ranges and the same seven cancellation/recovery cases.
- The ordinary folder-reader and home-launch handshake suite, retaining its strict source fences and 64 MiB payload cache.
- Strict native SDK compilation of the four affected reader/container units.

PFS metadata caching invalidates a page before filling it and marks it valid only after a complete callback. Namespace validation is committed only after a complete walk; traversal scratch is discarded on failure. The repair retains those rules, signing, immutable-generation checks, cache sizes, worker affinity and priority.

These checks do not establish a linked or installed repair. The console comparison above used development 3 without this source change. The repaired reader still needs a separate development build and hardware qualification; the public folder-only release remains unchanged.
