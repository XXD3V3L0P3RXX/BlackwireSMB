# Source-owned PS5 payload runtime subset

The portable builder compiles these 21 selected runtime units and their 91
source/header dependencies from public SDK revision
`f5d40f02770e9b0001dd9e18056eca70b3da8b40`. `runtime-source.json` pins every
file, the unit list, flags and materialized header aliases. Original notices
remain in each source. The four older flat runtime files remain retained;
the new build compiles only the nested `source/` tree.

From the project root, use the ordinary source build:

```sh
python3 tools/build.py --build --host-tests --name native-01
```

`tools/build/sdk_runtime.py` builds into that output's new `sdk-runtime/`.
It requires LLVM 18 but no SDK source checkout, historical artifact or network.
It produces `crt1.o`, `libc-selected.a` and `reader-arc4random.o`; the ordinary
resident link explicitly excludes implicit SDK CRT/libc. The reader takes the
source-built arc4 object with its existing private DATA-symbol rename applied
once. Per-unit dependency files reject external or unlisted runtime headers.

The source manifest transitively binds `runtime-source.json`, dependency pins,
notices and compiler flags. Inputs and metadata are checked before and after.
Artifacts from a different output or changed lineage are refused. All raw build
products remain available for rebuilding/relinking; none are executed here.

CRT and libc syscalls/mman/netdb are GPL-3.0-or-later. Arc4random is ISC-style,
strcasecmp is MIT/musl, assert uses its retained BSD three-condition notice;
headers retain their original terms. See [NOTICE.md](NOTICE.md) and [LICENSE](LICENSE).
The [LLVM record](../../metadata/llvm-runtime-reference.json) separately supplies
full notices and a qualified source reference. It does not attest the installed
C++ archives' source. OpenGL source obligations also remain separate. A source
build or host link does not qualify a PS5 runtime release.
