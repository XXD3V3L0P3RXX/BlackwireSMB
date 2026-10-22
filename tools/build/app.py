"""Build the standalone web resident; no native application publication."""
import importlib.util
import json
from pathlib import Path
import sys
import launch_timing
import sdk_runtime
from common import pin, save, source_flags


def build(root, out, sdk, dependency, image, runner, jobs=2, host_tests=False, allow_source_changes=False, runtime=None):
    # A previous build's objects or an unpinned runtime are never a fallback.
    source_identity = sdk_runtime.source_lineage(root, out, runtime)
    digest = pin(image)["sha256"]
    embedded = out / "embedded"
    embedded.mkdir()
    data = image.read_bytes()
    lines = ['#include "preload_bundle.h"', 'static const uint8_t preload_bytes[] = {']
    for start in range(0, len(data), 16):
        lines.append("    " + ",".join("0x%02x" % b for b in data[start:start+16]) + ",")
    lines += ["};", "const struct bw_preload_image bw_preload_embedded_image = {",
              "    preload_bytes,sizeof preload_bytes,",
              "    {" + ",".join("0x" + digest[i:i+2] for i in range(0,64,2)) + "},",
              '    "' + source_identity + '",10000u', "};", ""]
    (embedded / "preload-image.c").write_text("\n".join(lines), encoding="ascii")
    recipe = json.loads((root / "tools/build/app-recipe.json").read_text())
    commands = launch_timing.commands(recipe["commands"])
    for row in commands:
        argv = row["argv"]
        # Preserve the accepted standalone Media path in the resident entry.
        if row["label"] == "compile-00":
            argv.insert(1, "-DBW_MEDIA_PUBLIC_TRIAL=1")
            # Diagnostic development runs launch games from the PC browser.
            # The PS5 Media shortcut remains available when explicitly opened.
            argv.insert(1, "-DBW_TEST_PC_BROWSER_ONLY=1")
        # Only these existing translation units consume the startup broker.
        # Other translation units retain their original compile flags.
        if row["label"] in ("compile-00", "compile-01"):
            argv.insert(1, "-DBW_HOME_STARTUP=1")
        if row["label"] == "compile-reader" or any("/library/" + n in s for n in
            ("native_backend.c", "native_prepared_target.c", "native_startup_hint.c") for s in argv):
            argv.insert(1, "-DBW_NATIVE_PRELOAD=1")
        if "-c" in argv:
            # This TU hashes every required local startup file. Keep those
            # exact checks while compiling their portable SHA loops at O2.
            if argv[argv.index("-c") + 1].endswith("/library/native_send.c"):
                if any(flag.startswith("-O") for flag in argv):
                    raise ValueError("Startup verifier optimization already selected")
                argv.insert(1, "-O2")
            argv[1:1] = ["-I", "{SMB_INCLUDE}", *source_flags(root, sdk)]
            if row["label"] == "compile-00":
                argv[1:1] = ["-DBW_WEB_ONLY=1", "-I", "{DASHBOARD}/service", "-I", "{DASHBOARD}/local-removal"]
            argv += ["-MD", "-MP", "-MF", "{OUT}/deps/" + row["label"] + ".d"]
    additional, objects = [], []
    # The accepted Media unit uses the same production flags as main.c.
    # Keep it separate from the native cover-app product and link it once.
    media_object = "{OUT}/objects/media-public-trial.o"
    media_argv = next(row["argv"] for row in commands if row["label"] == "compile-00").copy()
    media_argv[media_argv.index("-c") + 1] = "{SRC}/library/media_public_trial.c"
    media_argv[media_argv.index("-o") + 1] = media_object
    media_argv[media_argv.index("-MF") + 1] = "{OUT}/deps/media-public-trial.d"
    additional.append({"label":"compile-media-public-trial", "argv":media_argv})
    for name, source in (("bundle", "{SRC}/library/preload_bundle.c"),
                         ("handoff", "{SRC}/library/preload_handoff.c"),
                         ("launch", "{SRC}/library/preload_launch.c"),
                         ("image", "{OUT}/embedded/preload-image.c")):
        target = "{OUT}/objects/preload-" + name + ".o"
        additional.append({"label": "compile-preload-" + name, "argv": [
            "{SDK}/bin/prospero-clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
            "-DBW_GTA_AUDIO_BOOTSTRAP=1", *(["-DBW_LAUNCH_TIMING=1"] if name == "launch" else []),
            *source_flags(root, sdk), "-I", "{SRC}/streamreader", "-I", "{SRC}/library",
            "-I", "{SDK}/target/user/homebrew/include", "-c", source, "-o", target,
            "-MD", "-MP", "-MF", "{OUT}/deps/preload-" + name + ".d"]})
        objects.append(target)
    # The ordinary home-startup broker is part of the web resident. Its
    # three new objects are always compiled; no native cover app is included.
    for name in ("home_protocol", "home_session", "home_identity_ps5"):
        target = "{OUT}/objects/" + name + ".o"
        additional.append({"label": "compile-" + name.replace("_", "-"), "argv": [
            "{SDK}/bin/prospero-clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-misleading-indentation", "-g",
            "-DBW_HOME_STARTUP=1", "-DBW_WEB_ONLY=1", "-DBW_NATIVE_BACKEND=1",
            "-DBW_NATIVE_PRELOAD=1", "-DBW_NATIVE_PLAYBACK=1", "-DBW_GTA_AUDIO_BOOTSTRAP=1",
            *source_flags(root, sdk), "-I", "{SRC}/library", "-I", "{SRC}/streamreader",
            "-I", "{SRC}/streamreader/diagnostics", "-I", "{SMB_INCLUDE}", "-I", "{SQLITE}",
            "-I", "{SDK}/target/user/homebrew/include", "-c", "{SRC}/library/" + name + ".c", "-o", target,
            "-MD", "-MP", "-MF", "{OUT}/deps/" + name + ".d"]})
        objects.append(target)
    sources = []
    for part in ("model", "service", "local-removal"):
        sources.extend(sorted((root / "src/blackwire/dashboard" / part).glob("*.c")))
    for index, source in enumerate(sources):
        target = "{OUT}/objects/dashboard-" + str(index) + ".o"
        flags = [] if source.suffix == ".S" else [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-g",
            "-DBW_WEB_ONLY=1", "-DBW_NATIVE_PRELOAD=1", "-DBW_NATIVE_PLAYBACK=1", "-DBW_GTA_AUDIO_BOOTSTRAP=1", *source_flags(root, sdk)]
        includes = []
        for path in ("{SRC}/library", "{SRC}/streamreader", "{DASHBOARD}/model", "{DASHBOARD}/service", "{DASHBOARD}/local-removal", "{SMB_INCLUDE}", "{SQLITE}"):
            includes += ["-I", path]
        additional.append({"label": "compile-dashboard-" + str(index), "argv": [
            "{SDK}/bin/prospero-clang", *flags, *includes, "-c", str(source), "-o", target,
            "-MD", "-MP", "-MF", "{OUT}/deps/dashboard-" + str(index) + ".d"],
            "cwd": None})
        objects.append(target)
    link = next(row for row in commands if "{OUT}/blackwire-native.elf" in row["argv"] and "-o" in row["argv"])
    link["argv"].insert(1, "-Wl,-t")
    # Match the accepted Media object's placement before resident objects.
    media_index = next(i for i, value in enumerate(link["argv"]) if value.startswith("{OUT}/objects/"))
    link["argv"].insert(media_index, media_object)
    link["argv"][link["argv"].index("-o"):link["argv"].index("-o")] = objects
    commands[commands.index(link):commands.index(link)] = additional
    tokens = {"{ROOT}": str(root), "{SRC}": str(root / "src/blackwire"), "{SDK}": str(sdk),
              "{OUT}": str(out), "{SQLITE}": str(root / "vendor/sqlite"),
              "{SQLITE_OBJECT}": str(dependency["sqlite"]), "{SMB_ARCHIVE}": str(dependency["smb"]),
              "{SMB_INCLUDE}": str(root / "vendor/libsmb2/include"),
              "{DASHBOARD}": str(root / "src/blackwire/dashboard"),
              "{READER}": str(root / "src/blackwire/reader/native-reader.c")}
    def expand(value):
        for key, replacement in tokens.items():
            value = value.replace(key, replacement)
        if "{" in value or "}" in value:
            raise ValueError("Unexpanded recipe token")
        return value
    (out / "deps").mkdir()
    runtime_link = None
    for row in commands:
        argv = [expand(value) for value in row["argv"]]
        if row["label"] == "compile-00":
            argv.insert(1, '-DBW_RESIDENT_BUILD_ID="blackwire-compressed-loading-dev.5"')
        if row is link:
            argv = sdk_runtime.link_arguments(root, out, argv, runtime)
        if "-o" in argv:
            Path(argv[argv.index("-o") + 1]).parent.mkdir(parents=True, exist_ok=True)
        trace = runner.run(argv, "app-" + row["label"], row.get("cwd") or root / "src/blackwire/library")
        if row is link:
            runtime_link = sdk_runtime.verify_link_trace(root, out, trace, runtime)
    raw=out/"blackwire-native.elf"
    destination=out/"dist"
    destination.mkdir()
    package=destination/"Blackwire.elf"
    runner.run(["llvm-objcopy-18","--strip-debug",raw,package],"package-strip-debug")
    # Debug stripping must preserve every allocated load range and its bytes.
    import struct
    def loads(data):
        if data[:5]!=b"\x7fELF\x02":raise ValueError("Expected ELF64")
        offset=struct.unpack_from("<Q",data,32)[0]
        size,count=struct.unpack_from("<HH",data,54)
        return [entry for i in range(count) if (entry:=struct.unpack_from("<IIQQQQQQ",data,offset+i*size))[0]==1]
    original,packed=raw.read_bytes(),package.read_bytes()
    if loads(original)!=loads(packed) or any(original[p[2]:p[2]+p[5]]!=packed[p[2]:p[2]+p[5]] for p in loads(original)):
        raise ValueError("Packaging changed allocated ELF content")
    image_pin=pin(package)
    save(out/"embedding.json",{"reader":pin(image),"sourceManifestSha256":source_identity,
        "generatedC":pin(embedded/"preload-image.c"),"folderOnly":False,"standaloneWeb":True,
        "sourceRuntime":{"receipt":runtime["receiptPin"],"link":runtime_link},"nativeFrontendIncluded":False,
        "nativeAssetBundleIncluded":False,"rawApplication":pin(raw),"package":image_pin,"consoleActions":0})
    save(destination/"SHA256.json",{"Blackwire.elf":image_pin})
    return image_pin
