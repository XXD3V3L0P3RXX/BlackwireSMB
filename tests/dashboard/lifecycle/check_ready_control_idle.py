#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline actual default-idle CONTROL deduplication regression.

Run with Linux clang (WSL is suitable). The actual backend CONTROL functions,
default setup log parser, settings reader and structural cache execute against
synthetic files. Sony process-query transport and held startup proof are spies.
Four declaration-only production SDK objects are checked; no app is run.
No prior build, embedded reader, private evidence or console is required.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LIB = ROOT / "src/blackwire/library"
STREAM = ROOT / "src/blackwire/streamreader"


def pin(path):
    raw = path.read_bytes()
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang-18"))
    parser.add_argument("--sdk", type=Path, default=Path("/opt/ps5-payload-sdk"))
    args = parser.parse_args()
    out = args.output.resolve()
    require(out.is_relative_to(ROOT / "build") and out != ROOT / "build"
            and not out.exists(), "Use a fresh output below this repository build directory")
    out.mkdir(parents=True)
    from fixture_sdk import production_rows

    fixture = HERE / "test_ready_control_idle.c"
    production = [LIB / "native_setup.c", LIB / "startup_runtime_idle.c",
                  STREAM / "diagnostics/game_stop_identity.c", STREAM / "share_config.c"]
    used = {Path(__file__).resolve(), HERE / "fixture_sdk.py",
            ROOT / "tools/build/app-recipe.json", fixture, *production}
    possible = set(used)
    for base in (ROOT / "src/blackwire", ROOT / "vendor/libsmb2/include",
                 ROOT / "vendor/sqlite", ROOT / "vendor/ampr/third_party/lz4/lib"):
        possible.update(p.resolve() for p in base.rglob("*")
                        if p.is_file() and p.suffix in (".c", ".h", ".S", ".inc", ".def"))
    before = {path: pin(path) for path in possible}
    commands, dependencies = [], []

    def run(label, argv):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
            timeout=180, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1",
                "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        commands.append(dict(label=label, argv=argv, exitCode=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        require(not result.returncode and not re.search(
            r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:", result.stderr),
            label + " failed; evidence retained in commands.json")
        return result.stdout

    includes = [LIB, STREAM, STREAM / "diagnostics", ROOT / "vendor/libsmb2/include",
                ROOT / "vendor/sqlite", ROOT / "src/blackwire/tools/compressed_stream_candidate"]
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-Wno-misleading-indentation", "-Wno-unused-function", "-Wno-unused-variable",
             "-ffunction-sections", "-fdata-sections", "-DBW_COPY_HOST_TEST=1",
             "-DBW_NATIVE_SEND_HOST_TEST=1", "-DBW_NATIVE_FS_HOST_TEST=1"]
    flags += [str(value) for directory in includes for value in ("-I", directory)]
    sanitizer = ["-O1", "-g", "-pthread", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    objects = []
    for index, source in enumerate(production):
        obj = out / (str(index) + "-" + source.stem + ".host.o")
        dep = obj.with_suffix(".d")
        dependencies.append(dep)
        extra = ["-Dbw_native_setup_read=rc_actual_setup_read"] if index == 0 else []
        run("host-" + source.stem, [args.cc, *flags, *sanitizer, *extra,
            "-MD", "-MF", dep, "-c", source, "-o", obj])
        objects.append(obj)
    binary, dep = out / "ready-control-test", out / "ready-control-test.d"
    dependencies.append(dep)
    wraps = ["open", "dup", "close", "fclose", "fopen", "fgets", "clock_gettime"]
    run("host-control-link", [args.cc, *flags, *sanitizer, fixture, *objects,
        *["-Wl,--wrap=" + name for name in wraps], "-MD", "-MF", dep,
        "-Wl,--gc-sections", "-lm", "-o", binary])
    lines = run("actual-default-custom-and-uncached-controls", [binary]).splitlines()
    cost, result = json.loads(lines[0]), json.loads(lines[-1])
    require(result["passed"] and result["cases"] == 17
            and all(result[name] for name in ("actualBackendControl", "actualDefaultSetupParser",
                "actualSettingsFiles", "actualCacheAndProcessRecordParser"))
            and result["consoleActions"] == 0 and not result["nativeTimingClaim"],
            "The actual CONTROL fixture scope changed")
    expected = dict(legacyLogOpens=0, legacyParsedLines=0, cacheParsedLines=500,
                    processQueryCalls=160, settingsReads=160, heldProofChecks=40)
    require(all(cost.get(name) == value for name, value in expected.items()),
            "Default CONTROL parser cost or retained authority checks changed")

    native = []
    rows = production_rows(ROOT, out / "sdk-template", args.sdk.resolve())
    for name in ("native_backend.c", "native_setup.c", "startup_runtime_idle.c",
                 "startup_runtime_idle_ps5.c"):
        row = next(row for row in rows if "-c" in row["argv"] and
                   Path(row["argv"][row["argv"].index("-c") + 1]).name == name)
        argv = row["argv"].copy()
        obj = out / (Path(name).stem + ".sdk.o")
        dep = obj.with_suffix(".d")
        dependencies.append(dep)
        argv[argv.index("-o") + 1] = str(obj)
        argv[argv.index("-MF") + 1] = str(dep)
        # The idle collector directly includes the shared process-record header.
        argv[1:1] = ["-I", str(STREAM / "diagnostics")]
        run("production-sdk-" + name, argv)
        native.append(obj)
    combined = out / "ready-control-native.o"
    run("sdk-relocatable-link", [args.sdk.resolve() / "bin/ld.lld", "-r", *native,
                                 "-o", combined])
    external = {}
    for dep in dependencies:
        for name in shlex.split(dep.read_text().replace("\\\n", " ").split(":", 1)[1]):
            path = (Path(name) if Path(name).is_absolute() else ROOT / name).resolve()
            if path.is_relative_to(out):
                continue
            if path.is_relative_to(ROOT):
                used.add(path)
            else:
                external[str(path)] = pin(path)
    for path in used:
        require(path in before and pin(path) == before[path],
                "Actual repository source changed or was unpinned")
    proof = dict(passed=True, atUtc=datetime.now(timezone.utc).isoformat(),
        controls=result, costCounters=cost, sourceBeforeAfterMatch=True,
        source={str(path.relative_to(ROOT)): before[path] for path in sorted(used)},
        externalCompilerInputsObservedAfter=external,
        sdkUnits=len(native), sdkRelocatable=pin(combined), hostUnits=len(production) + 1,
        nativeObjects={path.name: pin(path) for path in native},
        consoleActions=0, fullBuilds=0, nativeRuntimeQualified=False,
        limits=["Actual backend CONTROL, default setup parser, settings files and cache execute.",
            "Sony query transport and retained startup proof are explicit spies.",
            "Cost counters prove parser work, not native elapsed or whole-Play speed.",
            "Only exact default idle plus an existing operation cache deduplicates the legacy parser.",
            "Custom/uncached, process/current/settings, source, cancellation/deadline and release-Unknown cases remain.",
            "External compiler/header inputs are observed after compilation, not attested before it."])
    (out / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps(dict(passed=True, controls=result, costCounters=cost,
                          sdkUnits=len(native), proof=pin(out / "proof.json"))))


if __name__ == "__main__":
    main()
