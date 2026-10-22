"""Compile and run an offline host fixture for the vendored SMB receive path.

Run with Python 3 and a Linux host C compiler (including WSL). This never calls
the project build, the payload SDK, a console, or a network service.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", type=Path, help="libsmb2 source directory")
    parser.add_argument("--sanitize", action="store_true", help="enable ASan and UBSan")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parent.parent
    vendor = args.vendor
    if vendor is None:
        vendor = next((p for p in (root / "src/vendor/libsmb2", root / "vendor/libsmb2")
                       if (p / "lib/socket.c").is_file()), None)
    if vendor is None or not (vendor / "lib/socket.c").is_file():
        parser.error("a vendored libsmb2 source directory is required")
    vendor = vendor.resolve()
    macros = """ARPA_INET_H DLFCN_H FCNTL_H INTTYPES_H NETDB_H NETINET_IN_H
NETINET_TCP_H POLL_H SOCKADDR_STORAGE LINGER STDDEF_H STDINT_H STDIO_H STDLIB_H STRINGS_H
STRING_H SYS_IOCTL_H SYS_POLL_H SYS_SOCKET_H SYS_STAT_H SYS_TYPES_H SYS_UIO_H
TIME_H SYS_TIME_H UNISTD_H ERRNO_H""".split()
    # socket.c is included by the fixture, replacing only readv with an in-memory
    # byte stream. The library parsers, vectors, signing and AES-CCM stay real.
    sources = sorted(p for p in (vendor / "lib").glob("*.c") if p.name != "socket.c")
    with tempfile.TemporaryDirectory(prefix="blackwire-smb-host-") as temporary:
        out = Path(temporary)
        (out / "config.h").write_text(
            "\n".join("#define HAVE_" + name + " 1" for name in macros)
            + '\n#define STDC_HEADERS 1\n#define VERSION "host-transport-test"\n', encoding="utf-8")
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-O1", "-g",
                   "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                   "-DHAVE_CONFIG_H", "-D_GNU_SOURCE", "-D_U_=__attribute__((unused))",
                   "-I" + str(out), "-I" + str(vendor / "lib"),
                   "-I" + str(vendor / "include"), "-I" + str(vendor / "include/smb2")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += [str(here / "transport_signing_test.c"), *map(str, sources),
                    "-o", str(out / "transport-signing-test")]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        subprocess.run([str(out / "transport-signing-test")], env=environment, check=True)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
