"""Source-owned payload CRT/libc subset. No SDK installation or network."""
from pathlib import Path
import json
import shlex
import shutil
from common import copy, pin, save

COMMIT = "f5d40f02770e9b0001dd9e18056eca70b3da8b40"
CRT = ["crt.c", "syscall.c", "klog.c", "nid.c", "kernel.c", "rtld.c", "rtld_so.c",
       "rtld_sprx.c", "rtld_payload.c", "rtld_dlfcn.c", "mdbg.c", "patch.c",
       "kernel_procio.c", "kernel_iommu.c", "_start.S"]
LIBC = ["arc4random.c", "syscalls.c", "strcasecmp.c", "assert.c", "mman.c", "netdb.c"]


def ordinary(path):
    path = Path(path)
    if not path.is_file() or path.is_symlink() or any(p.is_symlink() for p in path.parents):
        raise ValueError("Missing or linked runtime input: " + str(path))
    return path


def relative(name):
    if not isinstance(name, str) or not name or name == "." or "\\" in name:
        raise ValueError("Invalid runtime manifest path")
    path = Path(name)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ValueError("Invalid runtime manifest path")
    return path


def verify(root):
    """Dependency pins stay strict even when native source changes are allowed."""
    root = Path(root)
    metadata = root / "metadata/source-manifest.json"
    dependencies = root / "metadata/dependencies.json"
    source_map = json.loads(ordinary(metadata).read_text())
    deps = json.loads(ordinary(dependencies).read_text())
    record = source_map.get("sdkRuntime")
    if not isinstance(record, dict) or record.get("manifest") != "vendor/sdk-runtime/runtime-source.json" or record.get("commit") != COMMIT:
        raise ValueError("Pinned source-owned SDK runtime record missing")
    base = root / "vendor/sdk-runtime"
    path = ordinary(root / relative(record["manifest"]))
    if pin(path) != record.get("pin"):
        raise ValueError("Runtime manifest lineage differs")
    manifest = json.loads(path.read_text())
    expected = [{"group": group, "name": name} for group, names in (("crt", CRT), ("libc", LIBC)) for name in names]
    if manifest.get("commit") != COMMIT or manifest.get("units") != expected or len(manifest.get("files", {})) != 112:
        raise ValueError("Runtime source or unit contract differs")
    target = ["-target", "x86_64-sie-ps5", "-fPIC", "-fno-plt", "-fno-stack-protector",
              "-fvisibility-nodllstorageclass=default", "-O1"]
    flags = {"crt": [*target, "-ffreestanding", "-fno-builtin", "-nostdlib", "-Wall", "-Werror",
                     "-fms-extensions", "-Wno-microsoft-anon-tag"],
             "libc": [*target, "-nobuiltininc"], "crtAssemblyExtra": ["-masm=intel"]}
    if manifest.get("flags") != flags:
        raise ValueError("Runtime compiler flags differ")
    rows = deps.get("sdk", {}).get("linkedRuntimeSource", {})
    if rows.get("runtime-source.json") != pin(path):
        raise ValueError("Runtime manifest dependency pin differs")
    sources = {}
    for name, expected_pin in manifest["files"].items():
        leaf = relative(name)
        if leaf.suffix not in (".h", ".c", ".S"):
            raise ValueError("Unexpected runtime source type")
        selected = ordinary(base / "source" / leaf)
        if pin(selected) != expected_pin or rows.get("source/" + name) != expected_pin:
            raise ValueError("Runtime source dependency differs: " + name)
        sources[selected.resolve()] = expected_pin
    actual = set()
    for selected in (base / "source").rglob("*"):
        if selected.is_symlink():
            raise ValueError("Linked runtime source refused")
        if selected.is_file():
            actual.add(selected.resolve())
    if actual != set(sources):
        raise ValueError("Unlisted or missing runtime source/header")
    for name, expected_pin in rows.items():
        if pin(ordinary(base / relative(name))) != expected_pin:
            raise ValueError("Strict runtime dependency differs: " + name)
    notice_record = record.get("llvmReference", {})
    if notice_record.get("path") != "metadata/llvm-runtime-reference.json" or set(record.get("notices", {})) != {"LICENSES/LLVM-Apache-2.0.txt", "LICENSES/LLVM-RUNTIME-COPYRIGHT.txt", "LICENSES/LLVM-BSD.txt"}:
        raise ValueError("Complete runtime notice/reference record missing")
    reference = root / relative(notice_record.get("path"))
    if pin(ordinary(reference)) != notice_record.get("pin"):
        raise ValueError("LLVM runtime reference pin differs")
    for name, expected_pin in record.get("notices", {}).items():
        if pin(ordinary(root / relative(name))) != expected_pin:
            raise ValueError("Runtime notice differs")
    return dict(manifest=manifest, sources=sources, manifestPin=pin(path),
                sourceManifestPin=pin(metadata), dependenciesPin=pin(dependencies))


def dependency_inputs(dep, cwd, sources):
    body = dep.read_text().replace("\\\n", " ").split(":", 1)[1]
    used = set()
    for name in shlex.split(body):
        path = (Path(name) if Path(name).is_absolute() else Path(cwd) / name).resolve()
        if path not in sources or pin(ordinary(path)) != sources[path]:
            raise ValueError("Unpinned runtime compile dependency: " + str(path))
        used.add(path)
    return used


def build(root, out, sdk, runner):
    root, out = Path(root), Path(out)
    captured = verify(root)
    destination = out / "sdk-runtime"
    destination.mkdir()
    objects, used = {"crt": [], "libc": []}, set()
    source = root / "vendor/sdk-runtime/source"
    tools = {name: Path(shutil.which(name) or name) for name in
             ("clang-18", "ld.lld-18", "llvm-ar-18", "llvm-objcopy-18", "llvm-nm-18")}
    # LLVM multicall tools use the invocation name to select their behavior.
    # Hash the resolved regular file, but do not execute its resolved basename.
    tool_pins = {name: pin(ordinary(path.resolve(strict=True))) for name, path in tools.items()}
    for row in captured["manifest"]["units"]:
        group, name = row["group"], row["name"]
        obj = destination / (group + "-" + Path(name).stem + ".o")
        dep = obj.with_suffix(".d")
        extra = ["-masm=intel"] if name.endswith(".S") else []
        runner.run([tools["clang-18"], *captured["manifest"]["flags"][group],
                    "-nostdinc", "-isystem", source / "include/freebsd", *extra,
                    "-MD", "-MF", dep, "-c", name, "-o", obj],
                   "sdk-runtime-" + group + "-" + Path(name).stem, source / group)
        used.update(dependency_inputs(dep, source / group, captured["sources"]))
        objects[group].append(obj)
    if used != set(captured["sources"]):
        raise ValueError("Runtime compile dependency closure differs")
    crt, libc, arc4 = (destination / name for name in ("crt1.o", "libc-selected.a", "reader-arc4random.o"))
    runner.run([tools["ld.lld-18"], "-m", "elf_x86_64", "-r", "-o", crt, *objects["crt"]], "sdk-runtime-crt-combine")
    runner.run([tools["llvm-ar-18"], "rcs", libc, *objects["libc"]], "sdk-runtime-libc-archive")
    copy(objects["libc"][0], arc4)
    runner.run([tools["llvm-objcopy-18"], "--redefine-sym=__isthreaded=bp_smb_isthreaded", arc4], "sdk-runtime-reader-arc4-rename")
    symbols = runner.run([tools["llvm-nm-18"], arc4], "sdk-runtime-reader-arc4-symbols")
    if not any(line.strip() == "U bp_smb_isthreaded" for line in symbols.splitlines()) or any(line.endswith(" __isthreaded") for line in symbols.splitlines()):
        raise ValueError("Source-built reader arc4 namespace differs")
    after = verify(root)
    if captured != after or any(pin(ordinary(path.resolve(strict=True))) != tool_pins[name] for name, path in tools.items()):
        raise ValueError("Runtime source/tool inputs changed")
    products = {"crt": pin(crt), "libc": pin(libc), "readerArc4": pin(arc4)}
    result = dict(passed=True, sourceCommit=COMMIT, unitCount=21, usedSourceInputs=len(used),
        externalHeaders=0, sourceBeforeAfterMatch=True, sourceManifest=captured["sourceManifestPin"],
        dependencies=captured["dependenciesPin"], runtimeManifest=captured["manifestPin"],
        source={p.relative_to(source).as_posix(): captured["sources"][p] for p in sorted(used)},
        products=products, tools=tool_pins, installedStaticRuntimeUsed=False, nativeRuntimeProved=False)
    receipt = destination / "result.json"
    save(receipt, result)
    return dict(crt=crt, libc=libc, readerArc4=arc4, receipt=receipt, receiptPin=pin(receipt))


def products(root, out, runtime):
    """Reject previous output, substituted artifacts or silently rebased lineage."""
    captured = verify(root)
    destination = (Path(out) / "sdk-runtime").resolve()
    expected = {"crt": "crt1.o", "libc": "libc-selected.a", "readerArc4": "reader-arc4random.o", "receipt": "result.json"}
    if not isinstance(runtime, dict):
        raise ValueError("Current source runtime required")
    for key, name in expected.items():
        path = ordinary(runtime.get(key, ""))
        if path.resolve() != destination / name:
            raise ValueError("Runtime artifact is not owned by this build")
    if pin(runtime["receipt"]) != runtime.get("receiptPin"):
        raise ValueError("Runtime receipt changed")
    result = json.loads(Path(runtime["receipt"]).read_text())
    if result.get("passed") is not True or result.get("unitCount") != 21 or result.get("usedSourceInputs") != 112 or result.get("sourceCommit") != COMMIT or result.get("externalHeaders") != 0 or result.get("sourceBeforeAfterMatch") is not True or result.get("installedStaticRuntimeUsed") is not False:
        raise ValueError("Runtime producer result refused")
    for key in ("crt", "libc", "readerArc4"):
        if pin(runtime[key]) != result.get("products", {}).get(key):
            raise ValueError("Runtime artifact changed")
    if result.get("sourceManifest") != captured["sourceManifestPin"] or result.get("dependencies") != captured["dependenciesPin"] or result.get("runtimeManifest") != captured["manifestPin"]:
        raise ValueError("Runtime source lineage changed")
    return result


def source_lineage(root, out, runtime):
    return products(root, out, runtime)["sourceManifest"]["sha256"]


def link_arguments(root, out, argv, runtime):
    products(root, out, runtime)
    argv = list(map(str, argv))
    if "-nostartfiles" in argv or "-nodefaultlibs" in argv:
        raise ValueError("Unexpected preconfigured runtime link")
    return [argv[0], "-nostartfiles", "-nodefaultlibs", str(runtime["libc"]), *argv[1:],
            str(runtime["crt"]), "-lkernel_web", "-lSceLibcInternal", "-lSceNet"]


def verify_link_trace(root, out, trace, runtime):
    products(root, out, runtime)
    selected = [line.split("(", 1)[0].strip() for line in trace.splitlines()]
    crt, libc = str(runtime["crt"]), str(runtime["libc"])
    if crt not in selected or libc not in selected:
        raise ValueError("Source-built static runtime absent from actual link")
    if any(value.endswith("/libc.a") or value.endswith("/crt1.o") and value != crt for value in selected):
        raise ValueError("Unowned static SDK runtime in actual link")
    return dict(sourceOwnedStaticRuntimeSelected=True, installedStaticRuntimeExcluded=True)
