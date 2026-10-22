# Compressed streaming development

The published `v0.1.0-preview.1` remains the working folder-only preview. This development branch adds a compressed source to the existing preparation and early-reader workflow. GTA V and Resident Evil 7 reached visible gameplay from the supplied compressed images on the test console. A recurring system-dialog crash still prevents release qualification; shorter loading times have not been established by a controlled console comparison.

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
