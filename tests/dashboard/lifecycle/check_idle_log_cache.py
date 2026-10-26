#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual operation-owned idle cache and backend caller checks, offline only.

Run with Linux clang (WSL is suitable). Only synthetic host fixtures execute.
Optional --sdk compiles/links a cache-only PS5 artifact and a backend object;
neither artifact is uploaded or executed. No prior build or reader is needed.
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

HERE = Path(__file__).resolve().parent


def pin(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def repository(explicit):
    candidates = [explicit.resolve()] if explicit else HERE.parents
    for candidate in candidates:
        if (candidate / "src/blackwire/library/native_backend.c").is_file():
            return candidate
    raise ValueError("A Blackwire repository root is required")


def first_directory(*paths):
    for path in paths:
        if path.is_dir():
            return path
    raise ValueError("Required repository directory absent: " + str(paths))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def retained_main(text, start):
    require(text.count(start) == 1, "Retained routing entry changed")
    body = text.split(start, 1)[1]
    if start.startswith("static"):
        marker = "\nstatic void *execute_joined("
        require(body.count(marker) == 1, "Caller fixture boundary changed")
        body = body.split(marker, 1)[0]
    return body.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang-18"))
    parser.add_argument("--sdk", type=Path, help="Optional PS5 payload SDK root")
    args = parser.parse_args()
    root = repository(args.root)
    lib = root / "src/blackwire/library"
    stream = root / "src/blackwire/streamreader"
    tests = HERE
    fixture_names = ("test_idle_log_cache.c", "test_idle_cache_begin.c", "test_idle_cache_callers.c")
    require(all((tests / name).is_file() for name in fixture_names),
            "Install the three maintained C fixtures beside this runner")
    baseline = next((p for p in (root / "tools/maintenance/test_registration_backend.c",
                                root / "tests/dashboard/lifecycle/test_registration_backend.c")
                     if p.is_file()), None)
    require(baseline is not None, "Actual retained routing fixture absent")
    # The additional cache cases retain the actual ordinary routing scenarios,
    # rather than quietly replacing the maintained baseline with copied claims.
    require(retained_main(baseline.read_text(), "int main(void){") ==
            retained_main((tests / "test_idle_cache_callers.c").read_text(),
                          "static int retained_routing_main(void){"),
            "Retained routing scenarios differ; reconcile the actual fixtures")
    smb = first_directory(root / "vendor/libsmb2/include", root / "src/vendor/libsmb2/include")
    sqlite = first_directory(root / "vendor/sqlite", root / "src/vendor/sqlite")
    lz4 = next((p for p in (root / "vendor/ampr/third_party/lz4/lib",
                           root / "src/vendor/ampr/third_party/lz4/lib") if p.is_dir()), None)
    output = args.output.resolve()
    require(output.is_relative_to(root / "build") and not output.exists(),
            "Use a fresh absent output below the repository build directory")
    output.mkdir(parents=True)
    commands, dependencies = [], []
    # Capture dependencies before a compiler can read them, then compare every
    # actual dependency afterwards. No historical private proof is consulted.
    candidates = {Path(__file__).resolve(), baseline,
                  *(tests / name for name in fixture_names)}
    for directory in (lib, stream, smb, sqlite,
                      root / "src/blackwire/dashboard",
                      root / "src/blackwire/tools/compressed_stream_candidate"):
        candidates.update(p.resolve() for p in directory.rglob("*")
                          if p.is_file() and p.suffix in (".c", ".h", ".py"))
    before = {path: pin(path) for path in candidates}
    used = {Path(__file__).resolve(), baseline}

    def run(label, argv):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True,
                                timeout=180, env={**os.environ,
                                    "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1",
                                    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        commands.append(dict(label=label, argv=argv, exitCode=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        require(not result.returncode and not re.search(
            r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:", result.stderr),
            label + " failed; see commands.json")
        return result.stdout

    includes = [lib, stream, stream / "diagnostics", smb, sqlite,
                root / "src/blackwire/tools/compressed_stream_candidate"]
    if lz4:
        includes.append(lz4)
        before.update({p.resolve(): pin(p) for p in lz4.rglob("*.h")})
    common = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-Wno-misleading-indentation", "-Wno-unused-function",
              "-Wno-unused-variable", "-ffunction-sections", "-fdata-sections"]
    common += [str(value) for directory in includes for value in ("-I", directory)]
    sanitizer = ["-O1", "-g", "-pthread", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    source = lib / "startup_runtime_idle.c"
    identity = stream / "diagnostics/game_stop_identity.c"
    raw = source.read_text()
    used.update((source, identity, lib / "idle_log_cache.h"))

    def host(label, fixture, production, wrapped=(), extra=(), runtime=()):
        binary = output / label
        used.add(fixture)
        objects = []
        for index, item in enumerate([fixture, *production]):
            obj = output / (label + "-" + str(index) + ".o")
            dep = obj.with_suffix(".d")
            dependencies.append(dep)
            run(label + "-compile-" + str(index), [args.cc, *common, *sanitizer,
                *extra, "-MD", "-MF", dep, "-c", item, "-o", obj])
            objects.append(obj)
        run(label + "-link", [args.cc, *sanitizer, *objects,
            *["-Wl,--wrap=" + name for name in wrapped],
            "-Wl,--gc-sections", "-lm", "-o", binary])
        return run(label + "-run", [binary, *runtime]).splitlines()

    core_lines = host("cache-core", tests / "test_idle_log_cache.c", [source, identity],
        ["open", "close", "dup", "lseek", "fdopen", "fclose", "fstat", "lstat",
         "ferror", "fgets", "clock_gettime", "calloc"])
    core, cost = json.loads(core_lines[-1]), json.loads(core_lines[-2])
    require(core["passed"] and core["cases"] == 55 and core["allOwnedDescriptorsClosed"],
            "Core scenarios or descriptor ownership checks changed")
    begin = json.loads(host("cache-begin", tests / "test_idle_cache_begin.c",
                            [source, identity])[-1])
    require(begin["passed"] and begin["nestedOwnerPreserved"] and begin["allOwnersClosed"],
            "Nested begin failed to preserve its published owner")
    # A generated output-only negative control proves the regression is effective.
    replacements = {
        "if(!out||*out||!cache_nonce(operation)||current(r)||*out)return -1;":
        "if(!out||*out||!cache_nonce(operation)||current(r))return -1;",
        "memcpy(p->operation,operation,33);memcpy(p->path,path,n+1);if(*out){free(p);return -1;}*out=p;return 0;":
        "memcpy(p->operation,operation,33);memcpy(p->path,path,n+1);*out=p;return 0;"}
    negative = raw
    for old, new in replacements.items():
        require(negative.count(old) == 1, "Begin negative-control seam changed")
        negative = negative.replace(old, new)
    control = output / "without-begin-rechecks.c"
    control.write_text(negative)
    begin_control = json.loads(host("cache-begin-negative", tests / "test_idle_cache_begin.c",
        [control, identity], runtime=["--expect-overwrite"])[-1])
    require(begin_control["passed"] and begin_control["sameSlotOverwritten"] and
            begin_control["allOwnersClosed"], "Begin negative control was ineffective")
    caller_lines = host("cache-callers", tests / "test_idle_cache_callers.c",
        [source, identity, lib / "native_job.c", stream / "share_config.c"],
        ["open", "dup", "close", "fclose", "fgets", "clock_gettime"],
        ["-DBW_COPY_HOST_TEST=1", "-DBW_NATIVE_SEND_HOST_TEST=1", "-DBW_NATIVE_FS_HOST_TEST=1"])
    routing, callers = json.loads(caller_lines[-2]), json.loads(caller_lines[-1])
    require(callers["passed"] and callers["retainedRoutingCases"] == 64 and
            callers["focusedCallerCases"] == 17 and callers["allCachesClosedBeforeHandoff"] and
            callers["releaseUnknownRetained"], "Actual caller/lifetime scenarios changed")

    native, linked = [], None
    if args.sdk:
        sdk = args.sdk.resolve()
        entry = output / "cache-link-entry.c"
        entry.write_text('#include "idle_log_cache.h"\n'
            'static int absent(void*p){(void)p;return -1;}\n'
            'int main(void){struct bw_idle_log_cache*c=0;struct bw_idle_log_cache_release r;'
            'struct bw_startup_idle_request q={.deadline=1,.current=absent};'
            'int n=bw_idle_log_cache_begin(&c,"0123456789abcdef0123456789abcdef",1,&q);'
            'if(!n)n=bw_startup_idle_ps5_cached(&q,c,"0123456789abcdef0123456789abcdef");'
            'if(c&&bw_idle_log_cache_close(&c,&r))n=-1;return n;}\n')
        for item in (source, lib / "startup_runtime_idle_ps5.c", identity, entry,
                     lib / "native_backend.c"):
            obj = output / (item.stem + ".sdk.o")
            dep = obj.with_suffix(".d")
            dependencies.append(dep)
            flags = ["-DBW_NATIVE_BACKEND=1", "-DBW_NATIVE_PLAYBACK=1", "-DBW_NATIVE_PRELOAD=1",
                     "-DBW_GTA_AUDIO_BOOTSTRAP=1", "-DBW_LAUNCH_TIMING=1"] if item.name == "native_backend.c" else []
            run("sdk-" + item.stem, [sdk / "bin/prospero-clang", *common, "-O0", "-g",
                "-fstack-usage", *flags, "-I", sdk / "target/user/homebrew/include",
                "-MD", "-MF", dep, "-c", item, "-o", obj])
            native.append(obj)
        linked = output / "cache-link-only.elf"
        run("sdk-cache-complete-link", [sdk / "bin/prospero-clang", *native[:4],
            "-Wl,--gc-sections", "-Wl,-Map=" + str(output / "cache.map"), "-o", linked])
        run("sdk-backend-cache-relocatable", [sdk / "bin/ld.lld", "-r", native[-1],
            *native[:3], "-o", output / "backend-cache.o"])
    external = {}
    for dep in dependencies:
        for name in shlex.split(dep.read_text().replace("\\\n", " ").split(":", 1)[1]):
            path = (Path(name) if Path(name).is_absolute() else root / name).resolve()
            if path.is_relative_to(output):
                continue
            if path.is_relative_to(root):
                used.add(path)
            else:
                external[str(path)] = pin(path)
    for path in used:
        require(path in before and pin(path) == before[path], "Input changed/unpinned: " + str(path))
    proof = dict(passed=True, atUtc=datetime.now(timezone.utc).isoformat(), consoleActions=0,
        nativeRuntimeExecuted=False, fullBuilds=0, sourceBeforeAfterMatch=True,
        core=core, costCounters=cost, begin=begin, beginNegativeControl=begin_control,
        retainedRouting=routing, callers=callers,
        source={str(p.relative_to(root)): before[p] for p in sorted(used)},
        externalCompilerInputs=external, nativeObjects={p.name: pin(p) for p in native},
        nativeCacheLink=pin(linked) if linked else None, commands=commands,
        generatedNegativeControl=pin(control),
        limits=["Only host fixtures execute; game/transport/registration effects are modeled.",
            "Actual cache parsing, real filesystem/FD faults and joined backend workers are exercised.",
            "Cost counters measure parser work, not whole Play or Uninstall time.",
            "Metadata reuse is scoped to the supported append/rotate log writer; filesystem timestamp resolution is not proved.",
            "A same-node same-size rewrite inside a coarse timestamp tick can be invisible without privileged forgery."])
    (output / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps(dict(passed=True, core=core, begin=begin, callers=callers,
                          proof=pin(output / "proof.json"))))


if __name__ == "__main__":
    main()
