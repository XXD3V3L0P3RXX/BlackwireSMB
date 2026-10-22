"""Test the actual maintained cached-cover handlers without network/console I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def pin(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--sdk", default="/opt/ps5-payload-sdk")
    args = parser.parse_args()
    if sys.platform != "linux" or not args.name.replace("-", "").isalnum():
        raise ValueError("Use Linux/WSL and a new neutral output name")
    out = ROOT / "build/native-dashboard" / args.name
    out.mkdir(parents=True, exist_ok=False)
    library = ROOT / "src/blackwire/library"
    stream = ROOT / "src/blackwire/streamreader"
    vendor = ROOT / "vendor" if (ROOT / "vendor/libsmb2/include").is_dir() else ROOT / "src/vendor"
    sdk = Path(args.sdk).resolve(strict=True)
    units = [library / name for name in ("native_job.c", "native_setup.c", "source_identity.c",
             "source_binding.c", "source_profile.c", "source_catalog.c", "source_param.c")]
    units += [stream / "share_config.c", stream / "manifest.c"]
    inputs = set(units + [HERE / "host_cover_cache.c", Path(__file__), library / "native_backend.c"])
    inputs.update(library.glob("*.h"))
    inputs.update(stream.glob("*.h"))
    before = {p.relative_to(ROOT).as_posix(): pin(p) for p in sorted(inputs)}
    commands = []
    proof = {"passed": False, "source": before, "networkContacted": False, "consoleContacted": False,
             "sourcePreparationChanged": False, "compressedReaderEnabled": False, "deployed": False}

    def run(label, argv):
        argv = [str(x) for x in argv]
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=180,
                                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1",
                                     "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        (out / (label + ".log")).write_text(result.stdout + result.stderr)
        commands.append({"label": label, "argv": argv, "exitCode": result.returncode})
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode or (label.endswith("-run") and result.stderr):
            raise RuntimeError(label + " refused; inspect retained log")
        return result.stdout

    try:
        common = ["-std=gnu11", "-D_DEFAULT_SOURCE=1", "-Wall", "-Wextra", "-Werror",
                  "-Wno-unused-function", "-Wno-unused-variable", "-Wno-misleading-indentation",
                  "-g", "-O1", "-ffunction-sections", "-fdata-sections", "-I", library, "-I", stream,
                  "-I", vendor / "libsmb2/include"]
        reports = {}
        for label, early in (("early", True), ("legacy", False)):
            executable = out / label
            flags = ["clang-18", *common, "-DBW_NATIVE_SETUP_HOST_TEST=1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if early:
                flags += ["-DBW_NATIVE_PRELOAD=1"]
            run(label + "-compile", flags + [HERE / "host_cover_cache.c", *units,
                                              "-Wl,--gc-sections", "-pthread", "-o", executable])
            reports[label] = json.loads(run(label + "-run", [executable]))
        checks = 0

        def require(value, message):
            nonlocal checks
            checks += 1
            if not value:
                raise AssertionError(message)

        for label, report in reports.items():
            require(report["cases"] == 17, label + " cover scenarios")
            require(report["actionCalls"] == 0, label + " action callbacks untouched")
            state = report["state"]
            require(state["library"]["coverBytes"] == 27, label + " actual cache allocation unchanged")
            require(state["library"]["requests"] == 31 and state["library"]["contentBytes"] == 711,
                    label + " source counters unchanged")
            games = state["games"]
            require(len(games) == 8, label + " catalog count")
            for i, game in enumerate(games):
                require(game["cover"] == (i in (0, 1, 2, 3, 4)), label + " cover availability " + str(i))
                require(not game["canPlay"] and not game["startupReady"], label + " no invented preparation " + str(i))
            for i in (0, 1, 2, 3):
                require(games[i]["canPrepare"] and games[i]["canStart"], label + " folder capability retained " + str(i))
            for i in (4, 6):
                require(games[i]["sourceKind"] == "container", label + " source kind retained " + str(i))
                require(games[i]["canPrepare"] == (label == "legacy") and
                        games[i]["canStart"] == (label == "legacy"), label + " container capability unchanged " + str(i))
            for i in (5, 7):
                require(not games[i]["canPrepare"] and not games[i]["canStart"], label + " invalid source refused " + str(i))
        sdk_flags = [sdk / "bin/prospero-clang", *common, "-DBW_NATIVE_PRELOAD=1", "-DBW_NATIVE_PLAYBACK=1",
                     "-DBW_GTA_AUDIO_BOOTSTRAP=1", "-c", library / "native_backend.c", "-o", out / "backend.sdk.o"]
        run("backend-sdk", sdk_flags)
        after = {p.relative_to(ROOT).as_posix(): pin(p) for p in sorted(inputs)}
        require(before == after, "compiled source changed during check")
        proof.update(passed=True, reports=reports, assertionChecks=checks,
                     nativeChecks=sum(report["checks"] for report in reports.values()),
                     cases=sum(report["cases"] for report in reports.values()), sourceBeforeAfterMatch=True,
                     sdkObject=pin(out / "backend.sdk.o"), sdkCompiler=pin(sdk / "bin/prospero-clang"),
                     commands=commands)
    finally:
        (out / "proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps({"passed": proof["passed"], "cases": proof["cases"],
                      "nativeChecks": proof["nativeChecks"], "assertionChecks": proof["assertionChecks"],
                      "proof": pin(out / "proof.json"), "consoleContacted": False}))


if __name__ == "__main__":
    main()
