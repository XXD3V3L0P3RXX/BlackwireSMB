# Third-party source and distribution notices

Original Blackwire integration is GPL-3.0-or-later. Preserve [LICENSE](LICENSE), [NOTICE.md](NOTICE.md), imported notices, and the matching source/build transformations with any binary delivery.

| Component | Terms and retained material |
| --- | --- |
| Ampr and Blackwire reader integration | GPL-3.0-or-later; exact upstream source, modifications, transformation/build scripts and GPL text. |
| ps5-payload-manager-derived process query | Retained GPL notice and corresponding modified source. |
| libsmb2 core | LGPL-2.1-or-later; exact source/local fixes and a practical rebuild/relink path for statically linked binaries; individual files retain their notices. |
| SQLite | Retained public-domain dedication and version/check-in. |
| Miniz inflate subset | MIT copyright, grant and disclaimer in the imported files. |
| LZ4 library | BSD-2-Clause library notices; upstream directories retain their respective terms. |
| Selected SDK CRT/libc | Retained GPL, ISC, MIT/musl and other per-file terms; pinned source/transitive headers and rebuild/relink recipe under [vendor/sdk-runtime](vendor/sdk-runtime/README.md). |
| LLVM compiler/C++/unwind runtimes | Apache-2.0 WITH LLVM-exception and component notices under [LICENSES](LICENSES/); installed-tool/source correspondence is a separate record. |

[LICENSES](LICENSES/) contains the retained notice and license texts. Attribution alone does not satisfy applicable corresponding-source or static-library relinking requirements. Binary distribution must retain exact corresponding source and documented external toolchain/provider inputs. See [build instructions](docs/BUILD.md), the [dependency record](metadata/dependencies.json), and the [relink manifest](metadata/relink-manifest.json).

No game files, console system/runtime modules, firmware, keys, saves, private SDK content or credentials may be included. The five ShadowMount runtime files are external prerequisites. Native hui/OpenGL/stb/font assets are not part of the standalone web product.
