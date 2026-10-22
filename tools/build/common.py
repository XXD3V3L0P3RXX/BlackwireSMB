"""Shared source-only build helpers. No network or console operations."""
from pathlib import Path
import hashlib
import json
import os
import subprocess


def pin(path):
    data = Path(path).read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def tree(root):
    return {p.relative_to(root).as_posix(): pin(p) for p in sorted(Path(root).rglob("*"))
            if p.is_file() and "__pycache__" not in p.parts}


def copy(source, target):
    source, target = Path(source), Path(target)
    if source.is_symlink() or not source.is_file():
        raise ValueError("Not an ordinary source file: " + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source.read_bytes())


def source_flags(root, sdk):
    """Keep developer checkout names out of our generated debug/file strings."""
    return ["-ffile-prefix-map=" + str(root) + "=/blackwire",
            "-fdebug-prefix-map=" + str(root) + "=/blackwire",
            "-ffile-prefix-map=" + str(sdk) + "=/ps5-sdk",
            "-fdebug-prefix-map=" + str(sdk) + "=/ps5-sdk"]


class Runner:
    def __init__(self, root, out, sdk=None):
        self.root, self.out, self.sdk = Path(root), Path(out), sdk
        self.records = []

    def run(self, argv, label, cwd=None, timeout=600, binary=False):
        argv = [str(x) for x in argv]
        print(label, flush=True)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        if self.sdk:
            env["PS5_PAYLOAD_SDK"] = str(self.sdk)
        result = subprocess.run(argv, cwd=cwd or self.root, env=env,
                                capture_output=True, timeout=timeout)
        log = self.out / (label + ".log")
        log.write_bytes(result.stdout + result.stderr)
        self.records.append({"label": label, "argv": argv, "exitCode": result.returncode,
                             "log": log.relative_to(self.out).as_posix()})
        save(self.out / "commands.json", self.records)
        if result.returncode:
            raise RuntimeError(label + " failed; retained " + str(log))
        return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def verify_inputs(root, allow_source_changes=False):
    root = Path(root)
    source = json.loads((root / "metadata/source-manifest.json").read_text())
    deps = json.loads((root / "metadata/dependencies.json").read_text())
    recipe = json.loads((root / "tools/build/app-recipe.json").read_text())
    required = {"src/blackwire/reader/native-reader.c"}
    for row in recipe["commands"]:
        required.update(value.replace("{SRC}/", "src/blackwire/", 1) for value in row["argv"]
                        if value.startswith("{SRC}/") and value.endswith((".c", ".S")))
    for name in required:
        if name not in source["nativeSource"]:
            raise ValueError("Recipe source is not staged/pinned: " + name)
    changed = []
    groups = [(root, source["nativeSource"], True)]
    groups += [(root / "vendor" / name, deps[name]["files"], False)
               for name in ("ampr", "libsmb2", "sqlite")]
    groups.append((root / "vendor/sdk-runtime", deps["sdk"]["linkedRuntimeSource"], False))
    count = 0
    for base, rows, editable in groups:
        for name, expected in rows.items():
            path = base / name
            if not path.is_file() or path.is_symlink() or any(p.is_symlink() for p in path.parents):
                raise ValueError("Missing or linked source: " + str(path))
            if pin(path) != expected:
                if editable and allow_source_changes:
                    changed.append(path.relative_to(root).as_posix())
                else:
                    raise ValueError("Pinned source differs: " + str(path))
            count += 1
    import sdk_runtime
    runtime = sdk_runtime.verify(root)
    return {"checkedFiles": count, "changedNativeSource": changed,
            "sourceRuntimeInputs": len(runtime["sources"]), "runtimeManifest": runtime["manifestPin"],
            "compressedEarlyReader": False, "consoleContacted": False}
