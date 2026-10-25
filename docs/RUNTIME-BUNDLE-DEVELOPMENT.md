# Self-contained reader installation development

This repair is included in standalone Blackwire v0.3.0-beta.1, with [beta test instructions](BETA-TESTING.md). Existing preview and SendLog downloads are unchanged. Native Dashboard work is separate and paused during this investigation. The earlier development observations below remain attributed to their original binaries.

## Fault and repair

The released installer reads a fixed five-file pack from `/data/shadowmount/fakelib` before it can finish setup. Our preconfigured test console supplied that optional pack, hiding the fresh-install failure. A normal compatible ShadowMountPlus installation can lack the directory; this does not mean Sony system files are missing.

Blackwire already embeds its source-built streaming reader. The repaired installer writes that image to the owned game's `fakelib2/libSceAmpr.sprx`, verifies its size, SHA-256, file identity, owner and mode, and returns Ready only after the existing registration checks also pass. It never opens, creates or modifies the global pack.

An ordinary game without optional companions receives only the embedded reader. ShadowMountPlus treats `fakelib2` as an exclusive overlay, so the installer also preserves qualified optional companions from the prepared game's `fakelib` rather than silently suppressing them. Unknown companion names or bytes refuse before installation mutation and remain untouched. Recognized complete legacy five-file private bundles still verify and upgrade without referring to the global directory. This is not universal support for arbitrary backports.

Cancellation, deadline, immutable prepared-copy identity, exclusive creation, checked descriptor release, recognized predecessor hashes and uncertain-outcome refusal remain enforced. Removal validates the selected profile and exact reader before deleting the owned copy. No console system files or global runtime settings are repaired or replaced.

## Offline verification

Use fresh absent output directories below `build/` on Linux/WSL with LLVM 18 and the open-source payload SDK:

```sh
python3 tests/dashboard/lifecycle/check_self_contained_bundle.py --output build/reader-bundle-01
python3 tests/dashboard/lifecycle/check_preload_bundle_upgrade.py --output build/reader-upgrade-01
python3 tests/dashboard/lifecycle/check_preload_reader_uninstall.py --output build/reader-removal-01
python3 tools/build.py --build --host-tests --allow-source-changes --name reader-build-01 --sdk /opt/ps5-payload-sdk
```

The missing-global test uses actual installer C and synthetic reader bytes under AddressSanitizer and UndefinedBehaviorSanitizer. The released source fails with no destination mutation; the repaired source creates and verifies a reader-only bundle while the global directory remains absent. Its 352 checks also cover repeat installation, tampering, symlink refusal, preserving unknown companions and expired authority. SDK compilation and relocatable linking pass.

The actual-filesystem upgrade suite passes 47 cases and 27,934 checks, including known predecessor migration in a reader-only profile and legacy companion profiles. Read-only reader selection passes 25 cases and 8,863 checks without writes. Nine actual Send/removal scenarios pass, including reader-only and legacy five-file deletion, recognized older readers, absent readers and refusal after tampering or selecting the wrong reader. All removal descriptors balance, and original source/save sentinels remain unchanged. Six default-wrapper scenarios also pass, including uncertain-release retention.

The complete standalone SDK build passes 163 commands and all eleven host suites with unchanged source hashes from beginning to end. It contains the same exFAT reader as the preceding development build; the resident installer changes. No native frontend is included. [The console record](TESTING.md#unreleased-self-contained-reader-setup) covers actual legacy-copy removal, fresh reader-only setup and the limits of launch observation. These results do not establish gameplay on firmware 10.01.
