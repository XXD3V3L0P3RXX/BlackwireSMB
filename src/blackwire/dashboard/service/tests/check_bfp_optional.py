#!/usr/bin/env python3
"""Actual scoped service/catalog/installer checks, with modeled Sony/backend APIs.

No console, network request, game executable, process query or signal is used.
Run through Linux/WSL; all files touched by cases are private temporary fixtures.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]


def pin(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def fixture(base):
    source, apps = base / "full-local", base / "apps"
    registered = apps / "PPSA99911"
    for path in (source / "sce_sys", registered / "sce_sys"):
        path.mkdir(parents=True)
    param = json.dumps({"titleId": "PPSA99911", "contentId": "EP0000-PPSA99911_00-BLACKWIRETEST0001",
                        "titleName": "Local fixture", "applicationDrmType": "standard"}).encode()
    (source / "sce_sys/param.json").write_bytes(param)
    (registered / "sce_sys/param.json").write_bytes(param)
    (source / "eboot.bin").write_bytes(b"not-a-game-executable-fixture" * 3)
    (source / "eboot.bin").chmod(0o755)
    (registered / "mount.lnk").write_text(str(source))
    database = base / "app.db"
    with sqlite3.connect(database) as connection:
        connection.execute("CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)")
        connection.execute("INSERT INTO tbl_contentinfo VALUES(?,?)",
                           ("PPSA99911", "EP0000-PPSA99911_00-BLACKWIRETEST0001"))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    icon = registered / "sce_sys/icon0.png"
    icon.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(b"\0\0\0\0\xff")) + chunk(b"IEND", b""))
    smp = base / "smp.json"
    smp.write_text(json.dumps({"status": 0, "count": 1, "games": [{
        "path": str(source), "runtime_path": str(source), "source_type": "folder",
        "title_id": "PPSA99911", "title_name": "Local fixture", "content_id": "EP0000-PPSA99911_00-BLACKWIRETEST0001",
        "installed": True, "installed_pkg": False, "managed": True, "source_available": True, "image_backed": False}]}))
    return smp, apps, database, icon


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="bfp-optional-01")
    parser.add_argument("--sdk", type=Path, default=Path("/opt/ps5-payload-sdk"))
    parser.add_argument("--lz4-source", type=Path, help="Explicit corresponding-source directory if this developer checkout lacks vendored LZ4")
    args = parser.parse_args()
    if not re.fullmatch(r"bfp-optional-[0-9]+", args.name):
        raise ValueError("Use a new bfp-optional-NN output name")
    out = ROOT / "build/native-dashboard" / args.name
    out.mkdir(parents=True, exist_ok=False)
    library, service = ROOT / "src/blackwire/library", HERE.parent
    model, package = service.parent / "model", service.parent / "package"
    vendor = ROOT / "vendor"
    if not (vendor / "sqlite/sqlite3.c").is_file():
        vendor = ROOT / "src/vendor"
    sqlite = vendor / "sqlite"
    # LZ4 is part of this project's corresponding-source dependency closure.
    lz4 = args.lz4_source.resolve(strict=True) if args.lz4_source else vendor / "ampr/third_party/lz4/lib"
    if not args.lz4_source and not (lz4 / "lz4.c").is_file():
        lz4 = ROOT / "src/vendor/ampr/third_party/lz4/lib"
    if not (lz4 / "lz4.c").is_file():
        raise ValueError("Provide the corresponding LZ4 source directory with --lz4-source")
    files = [*HERE.glob("*.c"), Path(__file__), *(service / p for p in (
        "dashboard_service.c", "dashboard_service.h", "catalog_adapter.c", "catalog_adapter.h", "resident.c")),
        model / "dashboard_model.c", model / "dashboard_model.h", package / "native_publish.c", package / "native_publish.h",
        package / "native_install.c", library / "main.c", library / "native_backend.c"]
    before = {p.relative_to(ROOT).as_posix(): pin(p) for p in files}
    dependencies = {"sqlite3.c": pin(sqlite / "sqlite3.c"), "lz4.c": pin(lz4 / "lz4.c"), "lz4.h": pin(lz4 / "lz4.h")}
    commands = []
    def command(argv, label):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=120,
                                env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        (out / (label + ".log")).write_bytes(result.stdout + result.stderr)
        commands.append({"label": label, "argv": list(map(str, argv)), "exitCode": result.returncode})
        if result.returncode:
            raise RuntimeError(label + " failed; retained log")
        return result.stdout
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-g"]
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections"]
    includes = ["-I" + str(p) for p in (library, ROOT / "src/blackwire/streamreader", service, model, package,
                                         vendor / "libsmb2/include", sqlite, lz4, out)]
    result = {"passed": False, "consoleContacted": False, "nativeRuntimeProved": False}
    try:
        frontend = (service / "frontend_identity.c").read_text()
        def extract(signature):
            start = frontend.index(signature)
            end = frontend.index("{", start) + 1
            depth = 1
            while depth:
                depth += (frontend[end] == "{") - (frontend[end] == "}")
                end += 1
            return frontend[start:end] + "\n"
        fingerprint = out / "frontend-fixture-fingerprint.c"
        fingerprint.write_text('#include "frontend_identity.h"\n#include "copy_sha256.h"\n' +
            extract("static void hash_u64(") + extract("static void package_fingerprint(").replace(
                "static void package_fingerprint(", "void fixture_package_fingerprint("))
        command(["clang-18", "-std=gnu11", "-O0", "-g", "-DSQLITE_THREADSAFE=1", "-DSQLITE_OMIT_LOAD_EXTENSION",
                 "-DSQLITE_TEMP_STORE=3", "-c", sqlite / "sqlite3.c", "-o", out / "sqlite-host.o"], "sqlite-host")
        common = [service / p for p in ("catalog_adapter.c", "catalog_snapshot.c", "frontend_identity.c", "local_http.c")]
        common += [model / "dashboard_model.c", library / "source_param.c", library / "registration_db.c",
                   library / "native_request.c", ROOT / "src/blackwire/streamreader/manifest.c",
                   ROOT / "src/blackwire/streamreader/diagnostics/game_stop_identity.c"]
        command(["clang-18", *flags, *sanitize, *includes, service / "dashboard_service.c", *common, fingerprint,
                 HERE / "host_bfp_service.c", out / "sqlite-host.o", "-Wl,--gc-sections", "-lpthread", "-lm", "-o", out / "host-service"], "service-build")
        names = ["default-local", "default-pc", "default-send", "default-native-handoff", "default-source-change-handoff",
                 "default-busy-game", "default-reservation-busy", "default-source-change", "default-guard-refusal",
                 "optional-busy", "optional-malformed", "optional-timeout", "optional-status", "optional-not-ok", "optional-duplicate", "optional-type",
                 "pc-browser", "send-browser", "native-wait", "browser-queues-native", "local-browser", "local-unknown-recovery",
                 "native-timeout", "native-rewind", "external-game-before-handoff", "source-change-before-handoff", "frontend-unknown-before-handoff",
                 "bad-token", "stale-generation", "changed-native-source", "busy-game", "busy-transfer", "frontend-without-hello",
                 "hello-wrong-identity", "hello-unknown-field", "action-duplicate-field", "send-local-refused", "definite-native-refusal",
                 "unknown-native-no-replay", "reservation-release-failure", "cover-local", "cover-pc", "cover-path-injection"]
        cases = []
        for name in names:
            with tempfile.TemporaryDirectory(prefix="blackwire-bfp-") as temporary:
                paths = fixture(Path(temporary))
                cases.append(json.loads(command([out / "host-service", name, *paths], name)))
        catalog_sources = [service / "catalog_adapter.c", model / "dashboard_model.c", library / "source_param.c",
                           library / "registration_db.c", ROOT / "src/blackwire/streamreader/manifest.c"]
        command(["clang-18", *flags, *sanitize, *includes, *catalog_sources, HERE / "host_source_changes.c", out / "sqlite-host.o",
                 "-Wl,--gc-sections", "-Wl,--wrap=bw_registration_db_match", "-lpthread", "-lm", "-o", out / "host-source"], "source-build")
        mutations = []
        for mode, allowed in [("stable", True), ("stable", False), ("boot", True), ("source-param", True),
                              ("registered-param", True), ("mount-link", True), ("database", True)]:
            with tempfile.TemporaryDirectory(prefix="blackwire-source-") as temporary:
                smp, apps, database, _ = fixture(Path(temporary))
                value = json.loads(command([out / "host-source", mode, smp, apps, database,
                                            "allowed" if allowed else "withheld"], "source-" + mode + str(int(allowed))))
                assert value["allAssetsHashed"] == 0 and value["observed"] == 0
                if mode == "stable" and allowed:
                    assert value["flags"] == 3 and value["metadataVerified"] == 1
                else:
                    assert value["flags"] & 2 == 0
                    if mode != "stable":
                        assert value["changes"] == 1 and value["metadataVerified"] == 0
                    else:
                        assert value["exclusions"] & 4 == 0  # Unknown guard is not asserted active transfer.
                mutations.append({"mode": mode, "sourceChecksAllowed": allowed, **value})
        (out / "native_bundle.h").write_text('#include "native_install.h"\n#include "frontend_identity.h"\n'
            'extern const struct bd_native_bundle bd_embedded_bundle;\nextern const struct bd_frontend_package bd_embedded_frontend_package;\n')
        command(["clang-18", *flags, *sanitize, *includes, HERE / "host_publish_guard.c", package / "native_install.c", lz4 / "lz4.c",
                 "-Wl,--gc-sections", "-o", out / "host-publish"], "publish-build")
        with tempfile.TemporaryDirectory(prefix="blackwire-publish-") as temporary:
            home, private = Path(temporary) / "home", Path(temporary) / "private"
            home.mkdir(); private.mkdir(mode=0o700)
            publish = json.loads(command([out / "host-publish", home, private], "publish-cases"))
        sdk_sources = [service / "dashboard_service.c", service / "catalog_adapter.c", service / "resident.c",
                       model / "dashboard_model.c", package / "native_publish.c", library / "main.c"]
        for index, path in enumerate(sdk_sources):
            command([args.sdk / "bin/prospero-clang", *flags, *includes, "-DBW_NATIVE_DASHBOARD=1", "-DBW_NATIVE_BACKEND=1",
                     "-DBW_NATIVE_PRELOAD=1", "-DBW_NATIVE_PLAYBACK=1", "-DBW_GTA_AUDIO_BOOTSTRAP=1", "-c", path,
                     "-o", out / (str(index) + ".sdk.o")], "sdk-" + path.stem)
        assert before == {p.relative_to(ROOT).as_posix(): pin(p) for p in files}
        result.update(passed=True, serviceCases=len(cases), serviceChecks=sum(c["checks"] for c in cases),
                      observations=cases, actualScopedSourceMutations=mutations, publisherGuardAndInstaller=publish,
                      strictSdkUnits=len(sdk_sources), sources=before, dependencySources=dependencies, sourceBeforeAfterMatch=True,
                      modeledBackendSonyAndFrontendIdentity=True, actualServiceModelCatalogFilesystemSQLite=True,
                      externalTransferCompletenessClaimed=False, registrationDispatchOnConsole=False)
    except BaseException as error:
        result.update(error=repr(error), partialOutputsRetained=True)
        raise
    finally:
        result["commands"] = commands
        (out / "proof.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": True, "serviceCases": len(cases), "serviceChecks": result["serviceChecks"],
                      "sourceCases": len(mutations), "publisherChecks": publish["checks"], "proof": pin(out / "proof.json")}))


if __name__ == "__main__":
    main()
