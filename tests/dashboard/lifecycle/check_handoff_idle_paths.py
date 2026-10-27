#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused actual default Send, duplicate-log guards and startup handoff checks.

Host-only synthetic source/process seams; no console or network access. Run with
Linux clang (WSL is suitable). Existing broad copy/cache suites are not rerun.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def pin(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang-18"))
    args = parser.parse_args()
    root = ROOT
    out = args.output.resolve()
    if not out.is_relative_to(root / "build") or out.exists():
        raise ValueError("Use a fresh absent output under source build/")
    out.mkdir(parents=True)
    lib = root / "src/blackwire"
    fixture = HERE / "test_handoff_idle_paths.c"
    retained_copy = HERE / "test_native_local_copy.c"
    retained_cache = HERE / "test_idle_log_cache.c"
    # Byte-exact helper prefixes retain real producer/provider and syscall
    # wrappers. Only each retained fixture's unrelated main/scenarios are omitted.
    for source, marker, name in (
            (retained_copy, b"int main(int argc,char **argv){", "native_copy_helpers.inc"),
            (retained_cache, b"int main(void){", "idle_log_cache_helpers.inc")):
        data = source.read_bytes()
        if data.count(marker) != 1:
            raise ValueError("Retained fixture main boundary changed")
        (out / name).write_bytes(data.split(marker)[0])
    names = ["library/source_namespace.c", "library/source_companions.c", "library/source_provider.c", "library/source_prepare.c", "library/source_smb.c", "library/source_directory.c", "library/source_snapshot.c", "library/compressed_data.c", "library/source_plan.c", "library/source_profile.c", "library/source_binding.c", "library/source_identity.c", "library/source_param.c", "library/reader_selection.c", "library/native_launch.c", "streamreader/filemap.c", "streamreader/container_source.c", "streamreader/container_manifest.c", "streamreader/pfs_source.c", "streamreader/pfsc_range.c", "streamreader/pfsc_inflate.c", "streamreader/manifest.c", "streamreader/share_config.c"]
    flags = ["-D_DEFAULT_SOURCE=1", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter", "-ffunction-sections", "-fdata-sections", "-I" + str(out), "-I" + str(HERE), "-I" + str(lib / "library"), "-I" + str(lib / "streamreader"), "-I" + str(lib / "streamreader/diagnostics"), "-I" + str(lib), "-I" + str(root / "vendor/libsmb2/include"), "-I" + str(root / "tests/compressed")]
    variants = {
        "send": ("HANDOFF_SEND", [fixture, *[lib / n for n in names]], ["read", "pread", "write", "fsync", "openat", "close", "unlinkat", "bw_source_provider_read", "fopen"]),
        "derived": ("HANDOFF_DERIVED", [fixture], ["fopen"]),
        "batch": ("HANDOFF_BATCH", [fixture], ["fopen"]),
        "startup": ("HANDOFF_STARTUP", [fixture, lib / "library/startup_runtime_idle.c", lib / "streamreader/diagnostics/game_stop_identity.c"], ["open", "close", "dup", "lseek", "fstat", "lstat", "fdopen", "fclose", "ferror", "fgets", "clock_gettime", "calloc", "fopen"]),
    }
    commands = []

    def run(label, argv):
        row = {"label": label, "argv": list(map(str, argv))}
        if any(c["label"] == label for c in commands):
            raise ValueError("Duplicate evidence label")
        p = subprocess.run(row["argv"], cwd=root, capture_output=True, text=True, timeout=240, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        row.update(exitCode=p.returncode, log=label + ".log")
        commands.append(row)
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        (out / row["log"]).write_text(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError(label + " failed; inspect retained log")
        return p.stdout

    tracked = {Path(__file__).resolve(), fixture, retained_copy, retained_cache, root / "tests/compressed/fixtures.py"}
    for kind, (macro, sources, wraps) in variants.items():
        for i, source in enumerate(sources):
            dep = run("deps-" + kind + "-" + str(i), [args.cc, *flags, "-D" + macro + "=1", "-MM", source])
            for token in shlex.split(dep.replace("\\\n", " ").split(":", 1)[1]):
                path = Path(token)
                tracked.add((path if path.is_absolute() else root / path).resolve())
    before = {str(p.relative_to(root)): pin(p) for p in sorted(tracked)}
    (out / "source-before.json").write_text(json.dumps(before, indent=2) + "\n")
    binaries = {}
    for kind, (macro, sources, wraps) in variants.items():
        binary = out / ("actual-handoff-" + kind)
        run("compile-" + kind, [args.cc, *flags, "-D" + macro + "=1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", *sources, "-Wl,--gc-sections", *["-Wl,--wrap=" + w for w in wraps], "-pthread", "-o", binary])
        binaries[kind] = binary
    sys.path.insert(0, str(root / "tests/compressed"))
    from fixtures import make_pfs, pfsc
    results = []
    modes = ("quiet", "complete", "pending", "handoff-live", "stale-old", "second-pending")
    with tempfile.TemporaryDirectory(prefix="blackwire-handoff-paths-") as temporary:
        temp = Path(temporary)
        folder = temp / "source"
        game = folder / "fixture"
        game.mkdir(parents=True)
        length = 65537
        body = bytes((i * 13 + 17) % 251 for i in range(length))
        files = {"eboot.bin": body, "startup/boot.sprx": body, "startup/empty.sprx": b"", "sce_sys/param.json": b'{"titleId":"PPSA99001"}', "fakelib2/libSceAmpr.sprx": b"ORIGINAL UNEXECUTED AMPR", "fakelib2/README": b"LOCAL COMPANION METADATA", "assets/stream.pak": b"asset" * 200}
        for name, data in files.items():
            path = game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        image = temp / "fixture.ffpfsc"
        packed = pfsc(make_pfs(files)[0])
        image.write_bytes(packed)
        for kind in ("folder", "pfsc"):
            for mode in modes:
                destination = temp / (kind + "-" + mode)
                destination.mkdir()
                result = json.loads(run("send-" + kind + "-" + mode, [binaries["send"], kind, folder, image, destination, length, hashlib.sha256(body).hexdigest(), mode]).splitlines()[-1])
                assert result["passed"] and result["suppliedIdleCallback"] is False
                assert result["accepted"] == (mode in ("quiet", "complete"))
                results.append(result)
                assert all((game / name).read_bytes() == data for name, data in files.items())
                assert image.read_bytes() == packed
        for kind in ("derived", "batch"):
            for mode in modes:
                result = json.loads(run(kind + "-" + mode, [binaries[kind], temp / (kind + "-" + mode + ".log"), mode]).splitlines()[-1])
                assert result["passed"] and result["accepted"] == (mode in ("quiet", "complete"))
                assert result["logOpens"] == (2 if mode in ("quiet", "complete", "second-pending") else 1)
                results.append(result)
    startup = json.loads(run("startup-cold-cache-owned", [binaries["startup"]]).splitlines()[-1])
    assert startup["passed"] and startup["cases"] == 14 and startup["allOwnedDescriptorsClosed"] and startup["allowedIdentityUnchanged"]
    results.append(startup)
    after = {str(p.relative_to(root)): pin(p) for p in sorted(tracked)}
    proof = {"passed": before == after, "cases": len(results) - 1 + startup["cases"], "checks": sum(r["checks"] for r in results), "sourceBeforeAfterMatch": before == after, "sourceBefore": before, "sourceAfter": after, "commands": commands, "results": results, "actualDefaultSendCases": 12, "actualDerivedAndBatchGuardCases": 12, "actualStartupCases": 14, "secondFreshObservationPreserved": True, "allowedProcessIdentityNotTransferred": True, "sourceFilesUnchanged": True, "sanitizers": "ASan+UBSan", "consoleActions": 0, "networkActions": 0, "limits": ["Source transport/image bytes and process snapshots are declared host adapters; actual provider, native Send, filesystem publication, preparation receipt and retained verification execute", "Derived/batch cases execute their actual static admission guards, not full derived/batch preparation workers", "Startup tests execute the actual cold/cached/adopt evaluators with real host log files and process-profile parser; no console process liveness is proved", "Generated helper prefixes are byte-exact retained fixture code before main; original test files are pinned and unchanged", "This focused suite supplements, and does not rerun or replace, the existing broad copy/cache/session suites", "Host success is not a console startup, gameplay, firmware portability or performance claim"]}
    (out / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    assert before == after, "Sources changed during test"
    print(json.dumps({k: proof[k] for k in ("passed", "cases", "checks", "sourceBeforeAfterMatch", "consoleActions", "networkActions")}))


if __name__ == "__main__":
    main()
