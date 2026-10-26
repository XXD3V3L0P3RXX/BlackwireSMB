"""Portable build of the source-derived folder and PFS/PFSC early reader.

The retained source uses the STRICT identity policy and contiguous-read
coalescer. No previous output, private proof, console file or SDK installation
is a build dependency or modified by this recipe.
"""
from pathlib import Path
import hashlib
import json
from common import copy, pin, save, tree, source_flags
from reader_patch import apply, function
import sdk_runtime

CFILES = ["streamreader/manifest.c", "streamreader/cache.c", "streamreader/share_config.c",
          "library/reader_selection.c", "library/source_binding.c", "library/source_profile.c",
          "reader/preload/source.c", "reader/preload/first_read.c", "reader/preload/private_smb.c"]
HOME_CFILES = ["library/home_protocol.c", "library/source_param.c", "reader/preload/home_client.c"]
CFILES += HOME_CFILES
CONTAINER_CFILES = ["library/source_identity.c", "streamreader/container_source.c", "streamreader/container_manifest.c",
                    "streamreader/pfs_source.c", "streamreader/pfsc_range.c", "streamreader/pfsc_inflate.c"]
CFILES += CONTAINER_CFILES
PROJECTION_CFILES = ["library/source_companions.c", "library/reader_projection.c"]
CFILES += PROJECTION_CFILES
FLAGS = ["AMPR_PACK_ENABLE=1", "AMPR_DIRECTORY_OVERLAY=1", "AMPR_PROCESS_READ_INTERCEPT=1",
         "AMPR_PROCESS_OPEN_INTERCEPT=1", "AMPR_PROCESS_AIO_INTERCEPT=1",
         "AMPR_PROCESS_SYNC_READ_INTERCEPT=1", "AMPR_DEBUG_LOG=0", "AMPR_COMMAND_LOG=0", "BP_METADATA_PROFILE=0",
         "AMPR_PACK_IO_LOG=0", "AMPR_INTERNAL_POOL_SIZE=0x18000000ull"]
# A reproducibility epoch, not the wall-clock compilation date. Force it for
# this reader subtree so inherited environment does not change its identity.
READER_SOURCE_DATE_EPOCH = 0
RENAMES = {"getaddrinfo": "bp_smb_getaddrinfo", "freeaddrinfo": "bp_smb_freeaddrinfo",
           "getprotobyname": "bp_smb_getprotobyname", "random": "bp_smb_random",
           "srandom": "bp_smb_srandom", "getenv": "bp_smb_getenv", "__isthreaded": "bp_smb_isthreaded"}


def reader_source_flags(root, out, sdk):
    """Normalize output names as well as checkout/SDK paths in file/debug data."""
    return [*source_flags(root, sdk), "-gno-record-command-line",
            "-ffile-prefix-map=" + str(out) + "=/blackwire/reader-build",
            "-fdebug-prefix-map=" + str(out) + "=/blackwire/reader-build"]


def prepare(root, out):
    source = out / "reader-source"
    source.mkdir()
    origins = json.loads((root / "metadata/dependencies.json").read_text())["ampr"]["files"]
    for name in origins:
        copy(root / "vendor/ampr" / name, source / name)
    apply(source)
    for folder in ("library", "streamreader"):
        for path in (root / "src/blackwire" / folder).glob("*.h"):
            copy(path, source / "bw" / folder / path.name)
    slices = {}
    for name in [*CFILES[:6], "library/home_protocol.c", "library/source_param.c", *CONTAINER_CFILES, *PROJECTION_CFILES]:
        path = root / "src/blackwire" / name
        text = path.read_text()
        if name.endswith("reader_selection.c"):
            for start in ("static int rsel_identity(", "int bw_reader_selection_load("):
                text = text.replace(function(text, start), "")
        elif name.endswith("source_profile.c"):
            text = text[:text.index("static int sp_view(")]
        elif name.endswith("share_config.c"):
            text = text[:text.index("static int same(")]
        target = source / "bw" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        slices[name] = {"original": pin(path), "compiled": pin(target)}
    for path in (root / "src/blackwire/streamreader/vendor/miniz").iterdir():
        if path.is_file():
            copy(path, source / "bw/streamreader/vendor/miniz" / path.name)
    owned = root / "src/blackwire/reader/preload"
    for path in owned.iterdir():
        if path.suffix in (".c", ".h"):
            copy(path, source / "bw/reader/preload" / path.name)
    for name in ("runtime.cpp", "runtime.h", "source.h", "first_read.h", "home_client.h"):
        copy(owned / name, source / "src" / name)
    # Functions which must retain their exact upstream implementations.
    original = (root / "vendor/ampr/src/ampr_libkernel_hook.cpp").read_text()
    patched = (source / "src/ampr_libkernel_hook.cpp").read_text()
    for name in ("bool install_inline_detour(", "bool uninstall_inline_detour(",
                 "void* resolve_libkernel_symbol(", "void refresh_hook_runtime_state_from_detours("):
        if function(original, name) != function(patched, name):
            raise ValueError("Unexpected hook implementation change: " + name)
    if pin(source / "src/ampr_emu_apr_reactor.cpp") != origins["src/ampr_emu_apr_reactor.cpp"]:
        raise ValueError("APR reactor implementation changed")
    extract_tests(root, source, out)
    return source, slices


def extract_tests(root, source, out):
    pack = (source / "src/ampr_emu_pack.cpp").read_text()
    hooks = (source / "src/ampr_libkernel_hook.cpp").read_text()
    runtime = (root / "src/blackwire/reader/preload/runtime.cpp").read_text()
    source_text = (root / "src/blackwire/reader/preload/source.c").read_text()
    def extract(name, text):
        (out / name).write_text(text)
    extract("completion-witness-under-test.h", pack.split("// BWREAD_COMPLETION_WITNESS_BEGIN\n", 1)[1].split("// BWREAD_COMPLETION_WITNESS_END", 1)[0])
    extract("prehook-under-test.h", function(hooks, "int bp_prepare_originals_and_metadata("))
    extract("failure-release-under-test.h", function(hooks, "int bp_release_unpatched_metadata("))
    extract("module-start-under-test.h", function((source / "src/sceampr_exports.cpp").read_text(), "int module_start("))
    extract("prepatch-under-test.h", hooks.split("// BP_PREPATCH_REVALIDATION_BEGIN\n", 1)[1].split("// BP_PREPATCH_REVALIDATION_END", 1)[0])
    extract("input-diagnostic-under-test.h", runtime.split("// BP_INPUT_DIAGNOSTIC_BEGIN\n", 1)[1].split("// BP_INPUT_DIAGNOSTIC_END", 1)[0])
    extract("home-admission-under-test.h", runtime.split("// BP_HOME_ADMISSION_BEGIN\n", 1)[1].split("// BP_HOME_ADMISSION_END", 1)[0])
    extract("home-failure-under-test.h", function(runtime, "int bp_home_startup_failure("))
    extract("init-report-under-test.h", function(runtime, "void bp_init_report("))
    extract("connect-source-under-test.h", "\n".join(function(source_text, n) for n in
            ("static void connect_failure(", "static int connect_source(", "int bp_source_connect(",
             "int bp_source_connect_with_storage(", "void bp_source_connect_diagnostic(")) + "\n")
    extract("connect-worker-under-test.h", function(runtime, "bool bp_worker_initialize(") + "\n")
    extract("performance-report-under-test.h", function(runtime, "static void bp_runtime_report_performance(") + "\n")
    extract("cache-backing-under-test.h", runtime.split("// BP_CACHE_BACKING_BEGIN\n", 1)[1].split("// BP_CACHE_BACKING_END", 1)[0])
    extract("cache-release-under-test.h", "\n".join(function(runtime, n) for n in
            ("void bp_worker_finish(", "void bp_runtime_cancel(", "int bp_runtime_release(")) + "\n")
    copy(root / "src/blackwire/streamreader/cache.c", out / "cache-under-test.c")
    header = (root / "src/blackwire/reader/preload/runtime.h").read_text()
    extract("init-phases-under-test.h", "enum : unsigned {" + header.split("enum : unsigned {", 1)[1].split("};", 1)[0] + "};\n")


def host_tests(root, out, source, runner):
    tests = root / "tests/reader"
    cc = ["clang-18", "-D_DEFAULT_SOURCE", "-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
          "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    cxx = ["clang++-18", "-std=c++17", "-g", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    results = {}
    exe = out / "test-source"
    runner.run([*cc, "-DBW_GTA_AUDIO_BOOTSTRAP=1", "-Wno-misleading-indentation", "-Wno-unused-function",
                "-I" + str(root / "vendor/libsmb2/include"), "-I" + str(root / "src/blackwire"),
                *[source / "bw" / name for name in CFILES if not name.endswith("private_smb.c") and name not in HOME_CFILES],
                tests / "source_test.c", "-Wl,--wrap=clock_gettime", "-o", exe], "host-source-compile")
    results["source"] = json.loads(runner.run([exe], "host-source-test"))
    exe = out / "test-adapter"
    runner.run([*cc, root / "src/blackwire/reader/preload/private_smb.c", tests / "adapter_test.c", "-o", exe], "host-adapter-compile")
    results["adapter"] = json.loads(runner.run([exe], "host-adapter-test"))
    for name in ("prehook", "input_diagnostic"):
        exe = out / ("test-" + name)
        runner.run([*cxx, "-I" + str(out), tests / (name + "_test.cpp"), "-o", exe], "host-" + name + "-compile")
        results[name] = json.loads(runner.run([exe], "host-" + name + "-test"))
    obj = out / "first-read.o"
    runner.run([*cc, "-c", root / "src/blackwire/reader/preload/first_read.c", "-o", obj], "host-first-read-c")
    exe = out / "test-first-read"
    runner.run([*cxx, "-I" + str(root / "src/blackwire"), "-I" + str(out),
                tests / "first_read_test.cpp", obj, "-o", exe], "host-first-read-compile")
    results["first_read"] = json.loads(runner.run([exe], "host-first-read-test"))
    exe = out / "test-connect"
    runner.run([*cxx, "-I" + str(root / "src/blackwire"), "-I" + str(out),
                tests / "connect_diagnostic_test.cpp", "-o", exe], "host-connect-compile")
    results["connect"] = json.loads(runner.run([exe], "host-connect-test"))
    exe = out / "test-cache"
    runner.run([*cc, "-I" + str(out), "-I" + str(root / "src/blackwire/streamreader"),
                tests / "cache_storage_test.c", "-o", exe], "host-cache-compile")
    results["cache"] = json.loads(runner.run([exe], "host-cache-test"))
    # The fixed local startup handshake does not enter source/data tests or
    # alter their transport model. Qualify it with its own actual-source cases.
    home_out = out / "home-startup-host"
    runner.run(["python3", tests / "check_home.py", "--root", root, "--out", home_out], "host-home-startup")
    results["home_startup"] = json.loads((home_out / "proof.json").read_text())
    for key, script in (("exfat_metadata", "check_exfat.py"), ("compressed_stream", "check_synthetic.py"),
                        ("pfsc_batching", "check_pfsc_batching.py"), ("preparation_batching", "check_prepare_batching.py"),
                        ("folder_preparation_batching", "check_source_smb_batching.py"),
                        ("metadata_read_window", "check_metadata_read_window.py"),
                        ("catalog_admission", "check_catalog_admission.py")):
        checked = out / key
        runner.run(["python3", root / "tests/compressed" / script, "--output", checked], "host-" + key)
        results[key] = json.loads((checked / "proof.json").read_text())
    checked = out / "standalone-startup-trace"
    runner.run(["python3", root / "tests/build/check_standalone_startup_trace.py",
                "--host-only", "--output", checked], "host-standalone-startup-trace")
    results["standalone_startup_trace"] = json.loads((checked / "proof.json").read_text())
    checked = out / "startup-trace-build"
    runner.run(["python3", root / "tests/dashboard/lifecycle/check_startup_trace_build.py",
                "--host-only", "--output", checked], "host-startup-trace-build")
    results["startup_trace_build"] = json.loads((checked / "proof.json").read_text())
    checked = out / "self-contained-bundle"
    runner.run(["python3", root / "tests/dashboard/lifecycle/check_self_contained_bundle.py",
                "--host-only", "--output", checked], "host-self-contained-bundle")
    results["self_contained_bundle"] = json.loads((checked / "proof.json").read_text())
    checked = out / "source-ampr-witness"
    runner.run(["python3", root / "tests/dashboard/lifecycle/check_source_ampr_witness.py",
                "--output", checked], "host-source-ampr-witness")
    results["source_ampr_witness"] = json.loads((checked / "proof.json").read_text())
    checked = out / "source-connection-wrapper"
    runner.run(["python3", root / "tests/dashboard/lifecycle/check_source_connect_wrapper.py",
                "--output", checked], "host-source-connection-wrapper")
    results["source_connection_wrapper"] = json.loads((checked / "proof.json").read_text())
    checked = out / "projection-contract"
    runner.run(["python3", root / "tests/reader/check_projection_contract.py",
                "--output", checked], "host-projection-contract")
    results["projection_contract"] = json.loads((checked / "proof.json").read_text())
    for key, script, extra in (
            ("source_namespace", "compressed/check_namespace.py", []),
            ("source_companions", "dashboard/lifecycle/check_source_companions.py", ["--host-only"]),
            ("reader_projection", "reader/check_reader_projection.py", []),
            ("projected_send", "dashboard/lifecycle/check_projected_send.py", []),
            ("projected_data_chain", "dashboard/lifecycle/check_projected_data_chain.py", []),
            ("projected_bundle_data_chain", "dashboard/lifecycle/check_projected_bundle_data_chain.py", []),
            ("projected_bundle", "dashboard/lifecycle/check_projected_bundle.py", ["--host-only"]),
            ("projected_uninstall", "dashboard/lifecycle/check_projected_game_uninstall.py", ["--host-only"]),
            ("projected_uninstall_e2e", "dashboard/lifecycle/check_projected_game_uninstall_e2e.py", []),
            ("projected_registration", "dashboard/lifecycle/check_projected_game_uninstall_e2e.py", ["--registration"]),
            ("legacy_source_removal", "dashboard/lifecycle/check_legacy_source_removal.py", [])):
        checked = out / key
        runner.run(["python3", root / "tests" / script, "--output", checked, *extra], "host-" + key)
        results[key] = json.loads((checked / "proof.json").read_text())
    checked = out / "projected_handoff"
    runner.run(["python3", root / "tests/home/check_projected_handoff.py", "--root", root,
                "--output", checked], "host-projected_handoff")
    results["projected_handoff"] = json.loads((checked / "projected-handoff/proof.json").read_text())
    checked = out / "prepare-source-policy"
    runner.run(["python3", root / "tests/dashboard/lifecycle/check_prepare_source_policy.py",
                "--host-only", "--output", checked], "host-prepare-source-policy")
    results["prepare_source_policy"] = json.loads((checked / "proof.json").read_text())
    for key, script in (("manual_image_sources", "check_manual_image_sources.py"),
                        ("uninstall_identity", "check_uninstall_identity_diagnostic.py"),
                        ("uninstall_release", "check_uninstall_release_unknown.py"),
                        ("uninstall_finish_diagnostic", "check_uninstall_finish_diagnostic.py"),
                        ("play_preparation_failure", "check_play_preparation_failure.py")):
        checked = out / key
        runner.run(["python3", root / "tests/dashboard/lifecycle" / script,
                    "--host-only", "--output", checked], "host-" + key)
        results[key] = json.loads((checked / "proof.json").read_text())
    checked = out / "smb-cmac"
    runner.run(["python3", root / "tools/smb-transport-tests/check_cmac.py", "--output", checked], "host-smb-cmac")
    results["smb_cmac"] = json.loads((checked / "proof.json").read_text())
    for mode, extra in (("automatic", []), ("software", ["--force-software"])):
        report = runner.run(["python3", root / "tools/smb-transport-tests/run.py", "--sanitize", *extra],
                            "host-smb-transport-" + mode)
        expected = "SMB transport signing: 83 cases passed (real receive/parser/crypto; no network)"
        if expected not in report:
            raise ValueError("SMB transport regression result missing: " + mode)
        results["smb_transport_" + mode] = {"passed": True, "cases": 83, "sanitized": True,
                                              "mode": mode, "consoleContacted": False}
    checked = out / "send-logging"
    runner.run(["python3", root / "tests/diagnostics/check_send_logging.py", "--output", checked], "host-send-logging")
    results["send_logging"] = json.loads((checked / "proof.json").read_text())
    for key, script in (("settings_startup", "diagnostics/check_settings_startup.py"),
                        ("settings_storage", "diagnostics/check_settings_storage.py"),
                        ("settings_check_failure", "diagnostics/check_settings_check_failure.py"),
                        ("catalog_image_failure", "diagnostics/check_catalog_image_failure.py"),
                        ("registration_wait", "diagnostics/check_registration_wait.py"),
                        ("two_shares", "sources/check_two_shares.py")):
        checked = out / key
        runner.run(["python3", root / "tests" / script, "--output", checked], "host-" + key)
        results[key] = json.loads((checked / "proof.json").read_text())
    if not all(value.get("passed") is True for value in results.values()):
        raise ValueError("A host reader test did not pass")
    save(out / "host-tests.json", results)
    return results


def private_arc4(root, out, runtime, runner):
    sdk_runtime.products(root, out, runtime)
    arc4 = out / "arc4random.o"
    copy(runtime["readerArc4"], arc4)
    symbols = runner.run(["llvm-nm-18", arc4], "reader-private-arc4-symbols")
    if not any(line.strip() == "U bp_smb_isthreaded" for line in symbols.splitlines()) or any(line.endswith(" __isthreaded") for line in symbols.splitlines()):
        raise ValueError("Source-built reader arc4 namespace differs")
    return arc4


def build(root, out, sdk, source, dependency, runner, jobs, runtime):
    private = out / "libsmb2-private.a"
    copy(dependency["smb"], private)
    runner.run(["llvm-objcopy-18", *["--redefine-sym=" + a + "=" + b for a, b in RENAMES.items()], private], "reader-private-smb")
    arc4 = private_arc4(root, out, runtime, runner)
    providers, provider_pins = {}, {}
    for name in ("libkernel.so", "libSceLibcInternal.so"):
        path = sdk / "target/lib" / name
        provider_pins[name] = pin(path)
        symbols = runner.run(["llvm-readelf-18", "--dyn-syms", "--wide", path], "reader-provider-" + name)
        providers[name] = sorted({line.split()[-1] for line in symbols.splitlines()
                                  if len(line.split()) >= 8 and line.split()[3] == "FUNC" and line.split()[6] != "UND"})
    generator = source / "tools/generate_payload_exports.py"
    text = generator.read_text()
    old = text[text.index("def is_libc_import("):text.index("def source_exports(")]
    new = '''def is_libc_import(name: str) -> bool:
    if name in PUBLIC_KERNEL_FUNCTIONS: return False
    if name in PUBLIC_LIBC_FUNCTIONS: return True
    raise ValueError(f"unsupported or non-FUNCTION public SDK import: {name}")


'''
    text = text.replace(old, new)
    text = text.replace("NID_SALT =", "PUBLIC_KERNEL_FUNCTIONS = frozenset(" + repr(providers["libkernel.so"]) + ")\nPUBLIC_LIBC_FUNCTIONS = frozenset(" + repr(providers["libSceLibcInternal.so"]) + ")\n\nNID_SALT =", 1)
    generator.write_text(text)
    make = source / "Makefile"
    text = make.read_text().replace("CPP_SOURCES := \\", "CPP_SOURCES := src/runtime.cpp \\", 1)
    objects = " ".join("$(OBJ)/bw/" + name[:-2] + ".o" for name in CFILES)
    text = text.replace("RENAMED_OBJECTS :=", "OBJECTS += " + objects + "\nRENAMED_OBJECTS :=", 1)
    text = text.replace("$(COMBINED_OBJ): $(OBJECTS)", "$(COMBINED_OBJ): $(OBJECTS) " + str(private) + " " + str(arc4))
    text = text.replace("-r -o $@ $(OBJECTS)", "-r -o $@ $(OBJECTS) " + str(private) + " " + str(arc4))
    text = text.replace("$(RENAMED_OBJECTS) $(METADATA_OBJ) $(LIBC_SO)", "$(OBJ)/nids/combined.o $(METADATA_OBJ) $(LIBC_SO)", 1)
    text = text.replace("$(RENAMED_OBJECTS) $(METADATA_OBJ) \\", "$(OBJ)/nids/combined.o $(METADATA_OBJ) \\", 1)
    mapping_flags = " ".join(reader_source_flags(root, out, sdk))
    text += "\nCOMMON_FLAGS += " + mapping_flags + "\nCXXFLAGS += " + mapping_flags + "\nCRT_CFLAGS += " + mapping_flags + "\n"
    text += '''
# Source-owned FOLDER reader: explicit worker/cache ownership and no index rebuild.
CXXFLAGS += -DAMPR_EMU_APP0_INDEX_AUTOBUILD=0 -DAMPR_EMU_PACK_WORKERS=1 -DAMPR_EMU_PACK_WORKER_CAPACITY=1 -DAMPR_EMU_PACK_DECODED_CACHE_BYTES=0 -DAMPR_EMU_PACK_PHYSICAL_CACHE_BYTES=0
$(OBJ)/src/runtime.o: CXXFLAGS += -Wall -Wextra -Werror
$(OBJ)/bw/%.o: bw/%.c $(BUILD_MAKEFILE) $(BUILD_CONFIG)
\t@mkdir -p $(dir $@)
\t$(CC) -std=c11 -DBW_GTA_AUDIO_BOOTSTRAP=1 $(COMMON_FLAGS) -I'''+str(root / "vendor/libsmb2/include")+''' -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -MD -MP -c $< -o $@
$(OBJ)/nids/combined.o: $(COMBINED_OBJ) $(RENAME_MAP)
\t@mkdir -p $(dir $@)
\tcp $< $@
\t$(OBJCOPY) @$(RENAME_MAP) $@
'''
    make.write_text(text)
    native = out / "reader"
    runner.run(["make", "-j" + str(jobs), "verify", "OUT=" + str(native), "PS5_PAYLOAD_SDK=" + str(sdk), "SOURCE_DATE_EPOCH=" + str(READER_SOURCE_DATE_EPOCH), *FLAGS], "reader-sdk-verify", source)
    imported = {name: (native / "obj/generated" / name).read_text().splitlines()
                for name in ("libkernel.imports.txt", "libSceLibcInternal.imports.txt")}
    forbidden = set(RENAMES)
    if forbidden.intersection(imported["libkernel.imports.txt"] + imported["libSceLibcInternal.imports.txt"]):
        raise ValueError("Private SMB symbol remains a public import")
    symbols = runner.run(["llvm-nm-18", native / "obj/link/libSceAmpr.combined.o"], "reader-symbol-check")
    if not any(line.endswith(" D bp_smb_isthreaded") for line in symbols.splitlines()):
        raise ValueError("Missing real private DATA object")
    if any(line.endswith(" U " + name) for line in symbols.splitlines() for name in RENAMES):
        raise ValueError("Unresolved private symbol")
    memory = runner.run(["llvm-nm-18", "--print-size", native / "libSceAmpr.prx"], "reader-memory-check")
    backing = {}
    for line in memory.splitlines():
        words = line.split()
        if len(words) == 4 and any(n in words[3] for n in ("cache_storage", "g_internal_amm_static_pool")):
            backing[words[3]] = int(words[1], 16)
    if sorted(backing.values()) != [67108864, 402653184]:
        raise ValueError("Reader memory contract differs")
    image = native / "libSceAmpr.sprx"
    result = {"folderOnly": False, "containerSelection": True, "image": pin(image),
              "publicProviders": provider_pins, "publicImports": imported, "privateRenames": RENAMES,
              "sourceRuntime": {"receipt": runtime["receiptPin"],
                                "manifest": sdk_runtime.products(root, out, runtime)["runtimeManifest"]},
              "privateArc4": pin(arc4),
              "privateSmb": pin(private), "flags": FLAGS, "workerCount": 1,
              "cacheBytes": 67108864, "poolBytes": 402653184, "backingSymbols": backing,
              "totalModuleMemoryIsNotCacheSize": True, "indexAutobuild": False,
              "generatedSource": tree(source), "sourceDateEpoch": READER_SOURCE_DATE_EPOCH,
              "buildClockIsSourceEpoch": True, "outputPrefix": "/blackwire/reader-build",
              "nativeRuntimeProved": False}
    save(out / "reader-build.json", result)
    return image, result
