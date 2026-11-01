# bennett-runtimes

Ordinary runtime C compiled to LLVM 18 and 23 bitcode, pinned to dialect
**0.2.10 @023f5a771a814270634db3be9cfde88ded69f975** and ISA **0.8**.
The publication contains the zero-filling allocator, memory helpers, a real
musl stdio/exit slice and the lifted console model. No device definition
delegates to host libc, and no production marker or ISA primitive is defined.
Broader runtime conformance remains in brt-zct and brt-q1k.

## Build and gate

Requires Python 3 and `/usr/lib/llvm-{18,23}/bin` tools:

```sh
scripts/build 18
scripts/build 23
scripts/gate
scripts/install-hooks.sh
```

Commands work from any directory by absolute path. They unset DISPLAY,
WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS and BROWSER and use local `.tmp/`.
Builds are offline. `scripts/build M OUTPUT_DIRECTORY` reproduces without
replacing dist. Pre-push gates actual pushed SHAs in clean detached worktrees;
it never pushes or fetches dependencies.

Each `dist/llvm<M>/` contains:

- `bennett-runtimes.bc`: combined allocator, memory helpers, selected musl and
  console definitions; one existing §19.1 runtime link supplies the libc closure.
- `manifest.json` and `SHA256SUMS`: artifact/source/sysroot SHA-256, tool
  versions, flags, complete external export/import lists and musl provenance.
- `musl-sysroot/include`: all upstream musl headers, with generated x86_64 bits.
- `musl-sysroot/lib/start.bc`, `start.S`, `start.c`, `syscall.c`, `heap.c`:
  comparison-only startup and Linux primitive/device transport; no host CRT.
  `bennett-allocator.bc` preserves the allocator-only regression artifact.
- `musl-sysroot/COPYRIGHT`: upstream licence.

Musl **1.2.5** comes from the unmodified vendored archive at
https://musl.libc.org/releases/musl-1.2.5.tar.gz, SHA-256
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`.
`scripts/musl.py` verifies/extracts it and compiles an explicit upstream closure
with each major's Clang/llvm-as; llvm-link and opt verify the combined result.
See [vendor/README.md](vendor/README.md) and
[libc-shims/README.md](libc-shims/README.md). There is no musl allocator object:
public malloc/calloc/free and musl's internal calloc use this repo's allocator.

The gate byte-compares both complete sysroots, bitcode and manifests, including
a second build from a relocated source tree (stable LLVM 23 local-symbol GUIDs). It checks
export/allocation attributes, recorded+fresh rung-2 link/LTO closure, allocation
semantics and ten native golden cases per major. Musl tests compile ordinary
hello plus formatted printf (including floating point), atexit, explicit exit,
return-time buffering and putchar/write. Native binaries are static, have no
undefined symbols or dynamic imports, and print the independent expected bytes.
Console tests instrument the published model's stores for native history/reset
and deterministic saved-tape replay, including negative vectors. Executor-level
acceptance remains the conformance rung-3 gate's job.

## Consume

Link matching-major runtime and arith before post-link LTO/plugin (§19.1):

```sh
/usr/lib/llvm-18/bin/clang --target=x86_64-unknown-linux-musl \
  --sysroot=dist/llvm18/musl-sysroot -nostdinc \
  -isystem dist/llvm18/musl-sysroot/include -O2 -flto -emit-llvm -c hello.c -o hello.bc
/usr/lib/llvm-18/bin/llvm-link hello.bc dist/llvm18/bennett-runtimes.bc \
  ../bennett-arith/dist/llvm18/bennett-arith.bc --only-needed -o linked.bc
/usr/lib/llvm-18/bin/opt '-passes=lto<O3>' linked.bc -o postlink.bc
scripts/native-link 18 postlink.bc hello-native
```

Use LLVM 23 with llvm23 artifacts. `native-link` is auditable native **support**
for original bitcode, not the driver-materialised §17.7 comparison build. It links
matching runtimes and arith with `-nostdlib -nostdinc -static -O2`, retains runtime
ABI definitions through native codegen (which can introduce libcalls), and
replaces only the syscall transport with the comparison-only Linux backend.
The driver still owns materialisation, bounds checks, argv/envp preparation,
§14.7 exit pseudo-exception propagation, and the common §19.5 layout.
`BENNETT_ARITH_ROOT` selects another arith checkout. Gate corpus dependencies
can also be selected with `BENNETT_CONFORMANCE_ROOT`.

Allocation wraps undefined `__bennett_nz_alloc/free/fault` primitives
(§3.1/16.9). Bytes are zeroed; calloc overflow faults 2 before mutation;
free(NULL) is inert; invalid/double free faults 7; storage is not reclaimed
(§17.1/17.5, ISA §7.1). The comparison heap is fixed at 0x10000000, with a
16 MiB default capacity and 65536 allocation records (configurable in heap.c).
Exact byte-bump history follows §16.9; odd-sized typed allocation alignment is
still dialect bead b-dialect-spec-jjr.

The syscall bridge maps stdout/stderr write/writev to the **undefined** console
marker (§11.2). R retains ISA effect/tape semantics; L calls the published
`__bennett_dev_console_write` over bound ordinary reversible state (§11.3).
See [device-models/README.md](device-models/README.md) for state layout, binding,
capacity, fault, native-model history and replay rules. Dialect bead
b-dialect-spec-lbq records both readings of startup/syscall framing/tape details;
the implementation follows the spec's tier distinction. No unsupported-symbol
trap stubs replace musl implementations. Remaining VFS/device/thread and broader
libc families remain promised scope under brt-zct/brt-q1k.
