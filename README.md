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
