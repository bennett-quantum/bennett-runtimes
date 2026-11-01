# bennett-runtimes

Ordinary runtime C compiled to LLVM bitcode, pinned to dialect **0.2.10**
(`023f5a771a814270634db3be9cfde88ded69f975`). This publication covers the
rung-2 hashtable's measured `malloc`, `calloc`, `free`, `llvm.memset` closure.
Exports also include native `memset` and N26's `__bennett_rt_memset` /
`__bennett_rt_memmove`. No defined symbol delegates to host libc.

## Build and verify

Requires Python 3 and the installed `/usr/lib/llvm-{18,23}/bin` tools:

```sh
scripts/build 18
scripts/build 23
scripts/install-hooks.sh
scripts/gate
```

Commands work from any directory when invoked by absolute path. They unset all
four desktop variables and use repository-local `.tmp/`. Each build publishes
`dist/llvm<M>/{bennett-runtimes.bc,manifest.json,SHA256SUMS}`; manifests record
bitcode/source SHA-256 digests, compiler flags, tool versions and exports.
`scripts/build M OUTPUT_DIRECTORY` builds without replacing the publication.

The gate reproduces both publications, audits exports and allocation attributes,
links the sibling conformance repo's recorded and fresh rung-2 IR with both
runtime libraries, checks post-LTO
closure, exercises allocation/fault semantics, and reproduces all ten golden
native cases using the published bitcode. Set `BENNETT_CONFORMANCE_ROOT` to use a
conformance checkout outside the sibling location (`BENNETT_ARITH_ROOT` similarly
selects arith). Pre-push gates pushed commits
in clean detached worktrees; it never pushes.

## Consume

The driver links **matching-major** runtimes and arith bitcode with the program
before post-link LTO and the plugin (dialect §19.1):

```sh
/usr/lib/llvm-18/bin/llvm-link program.bc dist/llvm18/bennett-runtimes.bc \
  ../bennett-arith/dist/llvm18/bennett-arith.bc --only-needed -o linked.bc
/usr/lib/llvm-18/bin/opt '-passes=lto<O3>' linked.bc -o postlink.bc
```

Use the LLVM 23 tools and `llvm23` artifacts together for that major. Allocation
wrappers reach the undefined `__bennett_nz_alloc/free/fault` primitives (§3.1,
§16.9); the plugin lowers them, preserving ISA allocation history. Production
bitcode defines neither primitives nor markers. Allocation zeroes bytes; overflow
faults with code 2; free(NULL) is inert; the primitive faults with code 7 on
invalid/double free. Storage is never reclaimed (§17.1/17.5, ISA §7.1).

`src/native/heap.c` is a **comparison-only x86_64 Linux primitive backend**;
compile it with `-O2 -ffreestanding -fno-builtin -no-pie` and link with
`-Wl,--section-start=.bennett_heap=0x10000000`. Configure
`BENNETT_NATIVE_HEAP_SIZE` (default 16 MiB) and `BENNETT_NATIVE_ALLOCATIONS`
(default 65536) to match the comparison layout. It uses direct diagnostic writes
and traps on failure. Retain the complete runtimes bitcode at native codegen:
`--only-needed` can discard `memset` before LLVM forms a native libcall.
The gate audits the native corpus/backend closure and dynamic imports.

This tests native comparison **support**, not a driver-materialised §17.7 binary
or full runtime conformance. Device bounds instrumentation and comparison layout
remain driver/plugin responsibilities. Exact byte-bump allocation follows the
§16.9 example; odd-sized typed C allocation alignment needs contract resolution
(`b-dialect-spec-jjr`). Broader libc and all remaining runtime contracts retain
scope under `brt-q1k` / `brt-zct`; no unsupported-symbol trap stubs are published.

## Rung 3 musl publication (brt-1ft)

The combined `bennett-runtimes.bc` now includes the explicit musl 1.2.5
stdio/exit closure from the unchanged, vendored source archive. Its SHA-256 is
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`;
see `vendor/README.md` for provenance/licence and `scripts/musl.py` for source
selection. Build is offline, using each major's Clang/llvm-as/llvm-link/opt.
All musl headers are in `dist/llvm<M>/musl-sysroot/include`; the manifest hashes
every sysroot file and records tools, flags, sources and exports. The combined
artifact keeps the existing §19.1 consumer link sufficient. There is no musl
malloc: public malloc/calloc/free remain this repo's zero-filling wrappers;
musl's internal calloc alias routes to the same implementation (§17.1).

`musl-sysroot/lib` supplies comparison-only Linux x86_64 start.S/start.c,
syscall.c and the primitive heap backend. `scripts/native-link M INPUT.bc OUTPUT`
uses these with `-nostdlib -nostdinc -static -O2`, without host CRT/libc/libm.
This tests native support; the driver still owns §17.7 materialisation/layout,
§19.2 argv/envp preparation and §14.7 pseudo-exception propagation. The static
single-thread musl process context supplies errno and locale without host TLS.
The separately hashed allocator bitcode under the sysroot is a regression-test
artifact; consumers use the combined runtime, not two allocator definitions.

The musl platform syscall header routes to `__bennett_rt_syscall`; the console
bridge is tracked by brt-cgb. No production marker is defined. Startup and
writev/terminal-query readings are recorded in dialect bead b-dialect-spec-lbq.
The gate byte-compares the entire sysroot and both bitcode publications, tests
ordinary hello, formatted printf (including floating point), real atexit handlers,
stdio flush and static native closure. Remaining musl families stay within
brt-q1k/brt-zct; this source closure is not a claim of full DR conformance.
