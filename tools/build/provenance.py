"""Record actual compiler dependencies without asserting an installed SDK commit."""
from pathlib import Path
import shlex
from common import pin, save


def sdk_inputs(root, out, sdk):
    files = {}
    for dependency in out.rglob("*.d"):
        body = dependency.read_text().replace("\\\n", " ")
        if ":" not in body:
            continue
        for word in shlex.split(body.split(":", 1)[1].split("\n\n", 1)[0]):
            path = Path(word)
            if not path.is_absolute():
                continue
            if not path.is_file():
                continue
            resolved = path.resolve()
            if resolved.is_relative_to(sdk):
                files[resolved.relative_to(sdk).as_posix()] = pin(resolved)
    for name in ("bin/prospero-clang", "bin/prospero-llvm-config", "toolchain/prospero.mk",
                 "toolchain/prospero.cmake", "target/lib/libkernel.so",
                 "target/lib/libSceLibcInternal.so"):
        path = sdk / name
        if path.is_file():
            files[name] = pin(path)
    trace = out / "app-full-sdk-link.log"
    if trace.is_file():
        for line in trace.read_text(errors="replace").splitlines():
            # ld.lld --trace emits one selected input path, optionally with an
            # archive member suffix. No guessing a library from an -l label.
            value = line.split("(", 1)[0].strip()
            if not value.startswith(str(sdk) + "/"):
                continue
            path = Path(value)
            if path.is_file():
                files[path.relative_to(sdk).as_posix()] = pin(path)
    runtime = out / "sdk-runtime/result.json"
    result = {"kind": "actual-installed-sdk-inputs-v1", "files": dict(sorted(files.items())),
              "sourceRuntimeReceipt": pin(runtime) if runtime.is_file() else None,
              "sourceCheckoutCommitDoesNotAttestInstallation": True,
              "nativeRuntimeSupportProved": False}
    save(out / "sdk-inputs.json", result)
    return result
