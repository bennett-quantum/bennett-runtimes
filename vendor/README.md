# musl 1.2.5 (unmodified upstream source archive)

URL: https://musl.libc.org/releases/musl-1.2.5.tar.gz
SHA-256: a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4

Downloaded once on 2026-11-01. No build fetches anything. COPYRIGHT from the
archive is reproduced as MUSL-COPYRIGHT and in both published sysroots.
`scripts/musl.py` verifies the archive digest before extracting it locally.
The explicit source list is the first stdio/exit slice, not a replacement libc.
All upstream headers are published; other source families remain under brt-q1k
and brt-zct. Platform headers for syscalls and process context are generated
outside the upstream source tree. No upstream source is patched.

## compiler-rt UBSan minimal (LLVM 18 and 23, x86_64)

`compiler-rt/llvmM/libclang_rt.ubsan_minimal-x86_64.a` is an unchanged offline
copy of `/usr/lib/llvm-M/lib/clang/M/lib/linux/` from the installed toolchain.
Corresponding installed Debian copyright/provenance files are adjacent.
These are compiler-rt under Apache-2.0 WITH LLVM-exception; no hosted libc is
bundled. `scripts/build.py` pins and verifies each SHA before publishing:

- LLVM 18: `66e08178e5231422dbe6717cbb0bd427ee4bb678175830ce1064d9d0b50781da`
- LLVM 23: `006cb02eeb8aeda11a20fa6c0d1bdcfadcb59b924be5755f7229dfcdcb8b82d6`

Only this working UBSan subset is published. Installed ASan archives are probed
by the gate to record why they fail strict links; they are never accepted as a
working runtime or replaced with host libraries. The port remains brt-nuu.2.
