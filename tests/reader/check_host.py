#!/usr/bin/env python3
"""Focused reader host fixtures only: no SDK, production build, network or console."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess


def function(text, start):
    begin = text.index(start)
    end = text.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[begin:end]


def pin(path):
    body = path.read_bytes()
    return {"bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    out = args.out.resolve()
    out.relative_to(root)
    out.mkdir(parents=True, exist_ok=False)
    source = out / "source"
    owned = root / "src/blackwire"
    tests = root / "tools/preload-reader/tests"
    if not tests.is_dir():
        tests = root / "tests/reader"
    vendor = root / "src/vendor/libsmb2/include"
    if not vendor.is_dir():
        vendor = root / "vendor/libsmb2/include"
    commands = []
    checked = []

    def relative(path):
        return Path(path).relative_to(root).as_posix()

    def copy(path, destination):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        checked.append(path)

    for folder in ("library", "streamreader", "reader/preload"):
        for path in (owned / folder).glob("*.h"):
            copy(path, source / folder / path.name)
    units = ["streamreader/manifest.c", "streamreader/cache.c", "streamreader/share_config.c",
             "library/reader_selection.c", "library/source_binding.c", "library/source_profile.c",
             "reader/preload/source.c"]
    for name in units:
        path = owned / name
        text = path.read_text()
        if name.endswith("reader_selection.c"):
            for start in ("static int rsel_identity(", "int bw_reader_selection_load("):
                text = text.replace(function(text, start), "")
        elif name.endswith("source_profile.c"):
            text = text[:text.index("static int sp_view(")]
        elif name.endswith("share_config.c"):
            text = text[:text.index("static int same(")]
        (source / name).write_text(text)
        checked.append(path)
    checked += [tests / "source_test.c", tests / "cache_storage_test.c", tests / "connect_diagnostic_test.cpp",
                owned / "reader/preload/runtime.cpp", Path(__file__).resolve()]
    for path in vendor.rglob("*.h"):
        checked.append(path)
    checked = list(dict.fromkeys(checked))
    before = {relative(path): pin(path) for path in checked}

    def run(label, argv):
        argv = [str(value) for value in argv]
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=180,
                                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1",
                                     "UBSAN_OPTIONS": "halt_on_error=1"})
        commands.append({"label": label, "argv": argv, "exitCode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(label + " failed; retained commands.json")
        return result.stdout

    cc = ["clang-18", "-D_DEFAULT_SOURCE", "-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
          "-Wno-misleading-indentation", "-Wno-unused-function", "-fsanitize=address,undefined",
          "-fno-omit-frame-pointer", "-ffile-prefix-map=" + str(root) + "=/blackwire"]
    exe = out / "host-source"
    run("source-compile", [*cc, "-DBW_GTA_AUDIO_BOOTSTRAP=1", "-I" + relative(vendor),
                          "-I" + relative(source), *[relative(source / name) for name in units],
                          relative(tests / "source_test.c"), "-Wl,--wrap=clock_gettime", "-o", relative(exe)])
    results = {"source": json.loads(run("source-cases", [relative(exe)]))}
    copy(owned / "streamreader/cache.c", out / "cache-under-test.c")
    exe = out / "host-cache"
    run("cache-compile", [*cc, "-I" + relative(out), "-I" + relative(owned / "streamreader"),
                         relative(tests / "cache_storage_test.c"), "-o", relative(exe)])
    results["cache"] = json.loads(run("cache-cases", [relative(exe)]))
    runtime = (owned / "reader/preload/runtime.cpp").read_text()
    source_text = (owned / "reader/preload/source.c").read_text()
    (out / "connect-source-under-test.h").write_text("\n".join(function(source_text, name) for name in (
        "static void connect_failure(", "static int connect_source(", "int bp_source_connect(",
        "int bp_source_connect_with_storage(", "void bp_source_connect_diagnostic(")) + "\n")
    (out / "connect-worker-under-test.h").write_text(function(runtime, "bool bp_worker_initialize(") + "\n")
    (out / "performance-report-under-test.h").write_text(function(runtime, "static void bp_runtime_report_performance(") + "\n")
    (out / "cache-backing-under-test.h").write_text(runtime.split("// BP_CACHE_BACKING_BEGIN\n", 1)[1].split("// BP_CACHE_BACKING_END", 1)[0])
    (out / "cache-release-under-test.h").write_text("\n".join(function(runtime, name) for name in (
        "void bp_worker_finish(", "void bp_runtime_cancel(", "int bp_runtime_release(")) + "\n")
    exe = out / "host-connect"
    run("connect-compile", ["clang++-18", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-ffile-prefix-map=" + str(root) + "=/blackwire", "-I" + relative(out),
                            "-I" + relative(owned), relative(tests / "connect_diagnostic_test.cpp"), "-o", relative(exe)])
    results["connect"] = json.loads(run("connect-cases", [relative(exe)]))
    after = {relative(path): pin(path) for path in checked}
    if before != after:
        raise RuntimeError("Checked inputs changed during the host fixtures")
    proof = {"passed": all(value.get("passed") is True for value in results.values()),
             "results": results, "source": after, "sourceBeforeAfterMatched": True,
             "transport": "explicitly modeled in the actual-source host fixture",
             "productionBuilds": 0, "sdkBuilds": 0, "consoleActions": 0, "networkActions": 0,
             "nativeRuntimeExecuted": False}
    (out / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps(proof["results"]))


if __name__ == "__main__":
    main()
