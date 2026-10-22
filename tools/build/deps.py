"""Build dependencies from the corresponding source in this repository."""
from pathlib import Path
from common import pin, source_flags


def dependency_source_flags(root, out, sdk):
    """Keep output names and recorded command lines out of dependency DWARF."""
    return [*source_flags(root, sdk), "-gno-record-command-line",
            "-ffile-prefix-map=" + str(out) + "=/blackwire/dependency-build",
            "-fdebug-prefix-map=" + str(out) + "=/blackwire/dependency-build"]


def build(root, out, sdk, runner, jobs):
    root, out, sdk = Path(root), Path(out), Path(sdk)
    destination = out / "dependencies"
    destination.mkdir()
    smb = destination / "libsmb2"
    runner.run(["cmake", "-S", root / "vendor/libsmb2", "-B", smb,
                "-DCMAKE_TOOLCHAIN_FILE=" + str(sdk / "toolchain/prospero.cmake"),
                "-DBUILD_SHARED_LIBS=OFF", "-DENABLE_EXAMPLES=OFF", "-DENABLE_GSSAPI=OFF",
                "-DENABLE_LIBKRB5=OFF", "-DCMAKE_C_FLAGS=" + " ".join(["-O2", "-g", *dependency_source_flags(root, out, sdk)])], "dependency-smb-configure")
    runner.run(["cmake", "--build", smb, "--parallel", jobs], "dependency-smb-build")
    archive = smb / "lib/libsmb2.a"
    sqlite = destination / "sqlite.o"
    runner.run([sdk / "bin/prospero-clang", "-O2", "-g", "-fPIC", *dependency_source_flags(root, out, sdk),
                "-DSQLITE_THREADSAFE=1", "-DSQLITE_OMIT_LOAD_EXTENSION", "-DSQLITE_TEMP_STORE=3",
                "-DSQLITE_MAX_MMAP_SIZE=0", "-DSQLITE_DEFAULT_MEMSTATUS=0",
                "-c", root / "vendor/sqlite/sqlite3.c", "-o", sqlite], "dependency-sqlite-build")
    return {"smb": archive, "sqlite": sqlite,
            "products": {"libsmb2.a": pin(archive), "sqlite.o": pin(sqlite)}}
