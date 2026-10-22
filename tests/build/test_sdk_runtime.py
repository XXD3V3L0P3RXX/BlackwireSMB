#!/usr/bin/env python3
"""Focused source-runtime closure and recipe checks; no full app or native run."""
from pathlib import Path
import argparse
import ast
import json
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/build"))
from common import Runner, copy, pin, save, tree, verify_inputs
import app
import reader
import provenance
import sdk_runtime as runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="New exclusive check directory")
    parser.add_argument("--sdk", default="/opt/ps5-payload-sdk")
    args = parser.parse_args()
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    sdk = Path(args.sdk).resolve(strict=True)
    before = {name: tree(ROOT / name) for name in ("vendor/sdk-runtime", "tools/build", "metadata", "LICENSES")}
    cases = []

    def check(name, predicate):
        if not predicate:
            raise AssertionError(name)
        cases.append({"name": name, "passed": True})

    def refused(name, call):
        try:
            call()
        except (ValueError, FileNotFoundError):
            cases.append({"name": name, "passed": True, "refused": True})
            return
        raise AssertionError("Expected refusal: " + name)

    # Keep mutation cases on the native host filesystem. The compiled products
    # and logs still belong to the requested current output. Retain this new
    # private fixture directory for diagnosis, rather than delete any input.
    work = Path(tempfile.mkdtemp(prefix="blackwire-runtime-check-"))
    fixture = work / "source"
    fixture.mkdir()
    for part in ("vendor/sdk-runtime", "metadata", "LICENSES"):
        shutil.copytree(ROOT / part, fixture / part)
    (fixture / "tools/build").mkdir(parents=True)
    copy(ROOT / "tools/build/app-recipe.json", fixture / "tools/build/app-recipe.json")
    check("strict 112 source/header inputs", len(runtime.verify(fixture)["sources"]) == 112)

    def variant(name):
        path = work / name
        shutil.copytree(fixture, path)
        return path

    for name, edit in (
        ("missing-source", lambda p: (p / "vendor/sdk-runtime/source/libc/arc4random.c").unlink()),
        ("changed-source", lambda p: (p / "vendor/sdk-runtime/source/libc/arc4random.c").write_bytes(b"changed")),
        ("unlisted-header", lambda p: (p / "vendor/sdk-runtime/source/include/unlisted.h").write_text("/* unlisted */\n")),
        ("missing-notice", lambda p: (p / "LICENSES/LLVM-Apache-2.0.txt").unlink()),
        ("changed-llvm-reference", lambda p: (p / "metadata/llvm-runtime-reference.json").write_text("{}\n")),
    ):
        path = variant(name)
        edit(path)
        refused(name, lambda: runtime.verify(path))
    path = variant("linked-source")
    selected = path / "vendor/sdk-runtime/source/libc/arc4random.c"
    saved = selected.read_bytes()
    selected.unlink()
    target = path / "original.c"
    target.write_bytes(saved)
    selected.symlink_to(target)
    refused("linked source", lambda: runtime.verify(path))
    path = variant("linked-source-parent")
    original = path / "vendor/sdk-runtime/source/libc"
    moved = path / "libc-source"
    original.rename(moved)
    original.symlink_to(moved, target_is_directory=True)
    refused("linked source ancestor", lambda: runtime.verify(path))

    # Execute the real common.verify_inputs with a small native/dependency map.
    # Runtime pins/verification are unchanged; allow-source-changes only affects
    # the deliberately changed native stub, never the runtime group.
    path = variant("common-check")
    native = path / "src/blackwire/reader/native-reader.c"
    native.parent.mkdir(parents=True)
    native.write_text("int original;\n")
    manifest_path = path / "metadata/source-manifest.json"
    data = json.loads(manifest_path.read_text())
    data["nativeSource"] = {"src/blackwire/reader/native-reader.c": pin(native)}
    save(manifest_path, data)
    native.write_text("int development_change;\n")
    dep_path = path / "metadata/dependencies.json"
    data = json.loads(dep_path.read_text())
    for part in ("ampr", "libsmb2", "sqlite"):
        data[part]["files"] = {}
    save(dep_path, data)
    save(path / "tools/build/app-recipe.json", {"commands": []})
    check("common native override keeps runtime strict", len(verify_inputs(path, True)["changedNativeSource"]) == 1)
    (path / "vendor/sdk-runtime/source/libc/arc4random.c").write_text("int changed_runtime;\n")
    refused("native override refuses changed runtime", lambda: verify_inputs(path, True))

    current = out / "current-output"
    current.mkdir()
    runner = Runner(fixture, current, sdk)
    owned = runtime.build(fixture, current, sdk, runner)
    result = runtime.products(fixture, current, owned)
    check("real 21-unit source build", result["unitCount"] == 21 and result["usedSourceInputs"] == 112 and result["externalHeaders"] == 0)
    check("source stability", result["sourceBeforeAfterMatch"] is True)
    refused("previous-output ownership", lambda: runtime.products(fixture, out / "different-output", owned))
    bad = dict(owned)
    bad["crt"] = owned["libc"]
    refused("substituted artifact path", lambda: runtime.products(fixture, current, bad))
    bad = dict(owned, receiptPin={"bytes": 0, "sha256": "0" * 64})
    refused("substituted producer receipt", lambda: runtime.products(fixture, current, bad))
    old = owned["crt"].read_bytes()
    owned["crt"].write_bytes(old + b"changed")
    refused("changed artifact", lambda: runtime.products(fixture, current, owned))
    owned["crt"].write_bytes(old)
    old = (fixture / "metadata/source-manifest.json").read_bytes()
    (fixture / "metadata/source-manifest.json").write_bytes(old + b"\n")
    refused("source lineage changed after compile", lambda: runtime.products(fixture, current, owned))
    (fixture / "metadata/source-manifest.json").write_bytes(old)
    old = (fixture / "metadata/dependencies.json").read_bytes()
    (fixture / "metadata/dependencies.json").write_bytes(old + b"\n")
    refused("dependency lineage changed after compile", lambda: runtime.products(fixture, current, owned))
    (fixture / "metadata/dependencies.json").write_bytes(old)

    # An actual compiler-injected header must be detected from the real .d,
    # even though the ordinary source tree is still byte-for-byte pinned.
    outside = out / "not-a-runtime-dependency.h"
    outside.write_text("/* Deliberately outside the authenticated closure. */\n")
    external_out = out / "external-header-output"
    external_out.mkdir()

    class ExternalHeaderRunner(Runner):
        def run(self, argv, label, cwd=None, **kw):
            if label == "sdk-runtime-crt-crt":
                argv = [argv[0], "-include", outside, *argv[1:]]
            return super().run(argv, label, cwd, **kw)

    external_runner = ExternalHeaderRunner(fixture, external_out, sdk)
    refused("actual transitive external header", lambda: runtime.build(fixture, external_out, sdk, external_runner))
    check("external header refuses before link", len(external_runner.records) == 1)
    check("real renamed reader DATA dependency", pin(reader.private_arc4(fixture, current, owned, runner)) == pin(owned["readerArc4"]))

    # Real driver/link, not execution: prove -nodefaultlibs/-nostartfiles select
    # the current objects and retain ordinary public import declarations.
    unit = current / "link-check.c"
    unit.write_text('extern int strcasecmp(const char *, const char *);\nint main(void) { return strcasecmp("runtime", "RUNTIME"); }\n')
    obj = current / "link-check.o"
    runner.run([sdk / "bin/prospero-clang", "-c", unit, "-o", obj], "source-runtime-link-check-compile")
    linked = current / "link-check.elf"
    argv = runtime.link_arguments(fixture, current, [sdk / "bin/prospero-clang", "-Wl,-t", obj, "-o", linked], owned)
    trace = runner.run(argv, "source-runtime-link-check")
    check("actual SDK link trace", runtime.verify_link_trace(fixture, current, trace, owned)["installedStaticRuntimeExcluded"])
    refused("installed CRT trace", lambda: runtime.verify_link_trace(fixture, current, trace + "\n/opt/ps5-payload-sdk/target/lib/crt1.o\n", owned))
    refused("installed libc trace", lambda: runtime.verify_link_trace(fixture, current, trace + "\n/opt/ps5-payload-sdk/target/lib/libc.a(arc4random.o)\n", owned))
    refused("missing static runtime trace", lambda: runtime.verify_link_trace(fixture, current, str(obj), owned))
    refused("preconfigured alternate runtime flags", lambda: runtime.link_arguments(fixture, current, ["cc", "-nodefaultlibs"], owned))

    # Execute the actual application recipe adapter with modeled compilation;
    # this tests wiring and embedded lineage, not a complete application link.
    for part in ("model", "service", "package", "local-removal"):
        (fixture / "src/blackwire/dashboard" / part).mkdir(parents=True, exist_ok=True)
    bundle = current / "fixture-bundle"
    bundle.mkdir()
    image = current / "fixture-reader.sprx"
    image.write_bytes(b"synthetic recipe reader input")

    class RecipeRunner:
        def __init__(self): self.records = []
        def run(self, argv, label, cwd=None):
            self.records.append((label, list(map(str, argv))))
            if "-o" in argv:
                target = Path(argv[argv.index("-o") + 1])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"modeled recipe output")
            return str(owned["crt"]) + "\n" + str(owned["libc"]) + "(libc-arc4random.o)\n"

    captured = RecipeRunner()
    original_dashboard = app.dashboard
    app.dashboard = lambda *a, **kw: (bundle, {"sourceBuilt": False, "testFixtureOnly": True})
    try:
        app.build(fixture, current, sdk, {"smb": current / "fixture-smb.a", "sqlite": current / "fixture-sqlite.o"}, image, captured, runtime=owned)
    finally:
        app.dashboard = original_dashboard
    link_rows = [argv for label, argv in captured.records if label == "app-full-sdk-link"]
    check("actual app adapter one explicit link", len(link_rows) == 1 and link_rows[0][1:3] == ["-nostartfiles", "-nodefaultlibs"])
    check("actual app adapter current runtime ownership", str(owned["crt"]) in link_rows[0] and str(owned["libc"]) in link_rows[0])
    embedding = json.loads((current / "embedding.json").read_text())
    code = (current / "embedded/preload-image.c").read_text()
    identity = pin(fixture / "metadata/source-manifest.json")["sha256"]
    check("embedded exact current source lineage", embedding["sourceManifestSha256"] == identity and ('"' + identity + '"') in code)
    check("embedded exact current reader digest", embedding["reader"] == pin(image) and all("0x" + pin(image)["sha256"][i:i+2] in code for i in range(0, 64, 2)))
    check("app result binds runtime producer", embedding["sourceRuntime"]["receipt"] == owned["receiptPin"])
    actual_sdk = provenance.sdk_inputs(fixture, current, sdk)
    check("provenance excludes implicit installed libc", "target/lib/libc.a" not in actual_sdk["files"] and actual_sdk["sourceRuntimeReceipt"] == owned["receiptPin"])
    check("reader no installed archive extraction", '"reader-sdk-arc4"' not in (ROOT / "tools/build/reader.py").read_text())
    for name in ("sdk_runtime.py", "app.py", "reader.py", "common.py", "provenance.py"):
        ast.parse((ROOT / "tools/build" / name).read_text())
    after = {name: tree(ROOT / name) for name in before}
    check("all input trees unchanged", before == after)
    save(out / "proof.json", {"passed": True, "cases": cases, "caseCount": len(cases),
        "actualRuntimeCommands": len(runner.records), "actualSourceRuntime": result,
        "realMinimalSdkLink": pin(linked), "modeledAppRecipeCommands": len(captured.records),
        "retainedFixtureDirectory": str(work),
        "fullApplicationBuilt": False, "nativeRuntimeProved": False, "consoleContacted": False,
        "sourceBeforeAfterMatch": True, "source": before})
    print(json.dumps({"passed": True, "cases": len(cases), "output": str(out), "nativeRuntimeProved": False}))


if __name__ == "__main__":
    main()
