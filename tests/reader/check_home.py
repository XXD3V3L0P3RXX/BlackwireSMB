"""Actual home-client/runtime host tests; optional focused SDK syntax only."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess


def pin(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--base", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-sdk", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    base = (args.base or root).resolve(strict=True)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    src = root / "src/blackwire"
    tests = root / "tests/reader"
    commands = []
    paths = [root / "tools/build/reader.py", root / "tools/build/reader_patch.py", Path(__file__).resolve()]
    paths += [src / "reader/preload" / n for n in ("runtime.cpp", "runtime.h", "home_client.c", "home_client.h", "source.c", "source.h", "private_smb.c")]
    paths += [src / "library" / n for n in ("home_protocol.c", "home_protocol.h", "source_param.c", "native_document.h", "reader_selection.c", "reader_selection.h")]
    paths += [tests / n for n in ("home_client_test.c", "home_runtime_test.cpp", "prehook_test.cpp", "input_diagnostic_test.cpp")]
    before = {str(p): pin(p) for p in paths}
    data_paths = ["reader/preload/source.c", "reader/preload/source.h", "reader/preload/private_smb.c", "streamreader/cache.c", "streamreader/cache.h"]
    unchanged = {n: pin(src / n) for n in data_paths}
    assert unchanged == {n: pin(base / "src/blackwire" / n) for n in data_paths}
    def run(label, argv):
        print(label, flush=True)
        result = subprocess.run(list(map(str, argv)), cwd=root, capture_output=True, text=True,
                                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1"}, timeout=120)
        commands.append({"label": label, "argv": list(map(str, argv)), "exitCode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(label + " failed; see " + str(out / "commands.json"))
        return result.stdout
    spec = importlib.util.spec_from_file_location("home_reader_patch", root / "tools/build/reader_patch.py")
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    runtime = (src / "reader/preload/runtime.cpp").read_text()
    header = (src / "reader/preload/runtime.h").read_text()
    extracts = {
        "input-diagnostic-under-test.h": runtime.split("// BP_INPUT_DIAGNOSTIC_BEGIN\n", 1)[1].split("// BP_INPUT_DIAGNOSTIC_END", 1)[0],
        "home-admission-under-test.h": runtime.split("// BP_HOME_ADMISSION_BEGIN\n", 1)[1].split("// BP_HOME_ADMISSION_END", 1)[0],
        "metadata-helpers-under-test.h": "\n".join(patch.function(runtime, n) for n in ("bool metadata_guard(", "bool metadata_admit(")),
        "metadata-prepare-under-test.h": patch.function(runtime, "bool bp_metadata_prepare("),
        "home-failure-under-test.h": patch.function(runtime, "int bp_home_startup_failure("),
        "init-report-under-test.h": patch.function(runtime, "void bp_init_report("),
        "init-phases-under-test.h": "enum : unsigned {" + header.split("enum : unsigned {", 1)[1].split("};", 1)[0] + "};\n",
    }
    generated = out / "patch-source"
    upstream = base / "vendor/ampr"
    for name in ("src/sceampr_exports.cpp", "src/ampr_emu_pack.cpp", "src/ampr_emu_index.cpp", "src/ampr_libkernel_hook.cpp", "ps5/crt/crtbeginS.c"):
        target = generated / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(upstream / name, target)
    patch.apply(generated)
    hooks = (generated / "src/ampr_libkernel_hook.cpp").read_text()
    extracts.update({
        "prehook-under-test.h": patch.function(hooks, "int bp_prepare_originals_and_metadata("),
        "failure-release-under-test.h": patch.function(hooks, "int bp_release_unpatched_metadata("),
        "module-start-under-test.h": patch.function((generated / "src/sceampr_exports.cpp").read_text(), "int module_start("),
        "prepatch-under-test.h": hooks.split("// BP_PREPATCH_REVALIDATION_BEGIN\n", 1)[1].split("// BP_PREPATCH_REVALIDATION_END", 1)[0],
    })
    for name, body in extracts.items():
        (out / name).write_text(body)
    cc = ["clang-18", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-Wno-unused-function", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(src)]
    cxx = ["clang++-18", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(src), "-I" + str(out)]
    codec = [src / "library/home_protocol.c", src / "library/source_param.c"]
    exe = out / "client-test"
    wrappers = ["socket", "fcntl", "ioctl", "setsockopt", "connect", "getsockopt", "poll", "send", "recv", "close", "sysctl", "clock_gettime"]
    run("client-host-compile", [*cc, "-DBW_HOME_CLIENT_HOST_TEST", "-DSO_NOSIGPIPE=0x1022", src / "reader/preload/home_client.c", *codec, tests / "home_client_test.c", *["-Wl,--wrap=" + n for n in wrappers], "-o", exe])
    results = {"client": json.loads(run("client-host-cases", [exe]))}
    selection = (src / "library/reader_selection.c").read_text()
    for start in ("static int rsel_identity(", "int bw_reader_selection_load("):
        selection = selection.replace(patch.function(selection, start), "")
    selection_c = out / "selection.c"
    selection_c.write_text(selection)
    selection_obj, manifest_obj = out / "selection.o", out / "manifest.o"
    run("selection-host-compile", [*cc, "-I" + str(src / "library"), "-c", selection_c, "-o", selection_obj])
    run("manifest-host-compile", [*cc, "-c", src / "streamreader/manifest.c", "-o", manifest_obj])
    exe = out / "runtime-test"
    run("runtime-host-compile", [*cxx, "-Wno-misleading-indentation", tests / "home_runtime_test.cpp", selection_obj, manifest_obj, "-o", exe])
    results["runtime"] = json.loads(run("runtime-host-cases", [exe]))
    for name in ("prehook", "input_diagnostic"):
        exe = out / (name + "-test")
        run(name + "-host-compile", [*cxx, tests / (name + "_test.cpp"), "-o", exe])
        results[name] = json.loads(run(name + "-host-cases", [exe]))
    sdk = Path("/opt/ps5-payload-sdk")
    if args.require_sdk and not sdk.is_dir():
        raise RuntimeError("The existing SDK is unavailable")
    native = {}
    if sdk.is_dir():
        syntax = out / "native-layout"
        for folder in ("library", "streamreader"):
            for path in (src / folder).glob("*.h"):
                target = syntax / "bw" / folder / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        (syntax / "src").mkdir()
        for name in ("runtime.cpp", "runtime.h", "source.h", "first_read.h", "home_client.h"):
            shutil.copyfile(src / "reader/preload" / name, syntax / "src" / name)
        for name in ("reader/preload/home_client.c", "library/home_protocol.c", "library/source_param.c"):
            run("native-syntax-" + Path(name).stem, [sdk / "bin/prospero-clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-Wno-unused-function", "-fsyntax-only", src / name])
        runtime_flags = ["-DAMPR_PAYLOAD_SDK_BUILD=1", "-DAMPR_EMU_HAS_LIBKERNEL_HOOK_IMPL=1",
                         "-DAMPR_EMU_PACK_ENABLE=1", "-DAMPR_EMU_PACK_DIRECTORY_OVERLAY_ENABLE=1",
                         "-DAMPR_EMU_PACK_PROCESS_OPEN_ENABLE=1", "-DAMPR_EMU_PACK_INTERCEPT_PROCESS_SYNC_READS=1"]
        run("native-syntax-runtime", [sdk / "bin/prospero-clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *runtime_flags, "-I" + str(upstream / "include"), "-idirafter", upstream / "ps5/include/sce", "-fsyntax-only", syntax / "src/runtime.cpp"])
        client_obj = out / "native-client-audit.o"
        run("native-client-object-audit", [sdk / "bin/prospero-clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-c", src / "reader/preload/home_client.c", "-o", client_obj])
        undefined = {line.split()[-1] for line in run("native-client-imports", [sdk / "bin/llvm-nm", "--undefined-only", client_obj]).splitlines() if line.split()}
        providers = {}
        for name in ("libkernel.so", "libSceLibcInternal.so"):
            listing = run("native-provider-" + name, ["llvm-readelf-18", "--dyn-syms", "--wide", sdk / "target/lib" / name])
            providers[name] = {line.split()[-1] for line in listing.splitlines() if len(line.split()) >= 8 and line.split()[3] == "FUNC" and line.split()[6] != "UND"}
        internal = {"bw_home_claim_format", "bw_home_ack_parse"}
        assert not undefined - providers["libkernel.so"] - providers["libSceLibcInternal.so"] - internal
        native = {"passed": True, "syntaxOnly": True, "fullReaderBuilds": 0,
                  "compiler": pin(sdk / "bin/prospero-clang++"), "clientObjectAudit": pin(client_obj),
                  "clientImports": sorted(undefined), "providerQualified": True, "newSystemServiceDependency": False}
    after = {str(p): pin(p) for p in paths}
    if before != after:
        raise RuntimeError("A qualified source changed during tests")
    proof = {"passed": all(r["passed"] for r in results.values()), "results": results, "native": native,
             "sourceBefore": before, "sourceAfter": after, "sourceBeforeAfterMatched": True,
             "dataPathByteIdenticalToBase": unchanged, "consoleActions": 0, "networkActions": 0,
             "fullReaderBuilds": 0, "residentBuilds": 0, "nativeExitExecuted": False,
             "scope": "Actual local startup control/HTTP/failure paths with modeled transport and SDK; native syntax only"}
    (out / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps({"passed": proof["passed"], "results": results, "native": native}))


if __name__ == "__main__":
    main()
