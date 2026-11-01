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
- `bennett-runtimes-native.bc`: complete comparison-only musl, allocator,
  console twin, `_start` and Linux primitive/exit closure. Never a device input.
- `musl-sysroot/lib/native-support.bc`: startup/primitives/native transport only,
  for an already materialised module (see the exact link order below).
- `manifest.json` and `SHA256SUMS`: both artifact/source/sysroot SHA-256, tool
  versions, flags, complete external export/import lists and musl provenance.
- `musl-sysroot/include`: all upstream musl headers, with generated x86_64 bits.
- `musl-sysroot/lib/start.bc`: legacy standalone startup bitcode (already
  includes `_start`; do not combine with start.S). Native sources/header are
  under `lib/native/`, with the shared bridge under `lib/musl/`.
  `lib/bennett-allocator.bc` preserves the allocator-only regression artifact.
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
semantics and ten native golden cases plus rung-3 hello per major, under strict static
links with two resolved comparison layouts. Native tests require configuration,
exercise zero capacity/65,537 zero allocations, and reject invalid or colliding
layouts with typed diagnostics. Musl tests compile ordinary
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
scripts/native-link 18 postlink.bc hello-native driver-layout.bc comparison.ld
```

Use LLVM 23 with llvm23 artifacts. `native-link M INPUT OUTPUT LAYOUT.bc
[COMPARISON.ld]` is a convenience for **original** bitcode. Configuration is
mandatory: it never supplies a heap default. It retains the entire native
runtime before major-matched arith `--only-needed`, then uses
`-nostdlib -nostdinc -static -no-pie`. No host CRT/libc/libm is used.
`BENNETT_ARITH_ROOT` selects another arith checkout;
`BENNETT_CONFORMANCE_ROOT` selects the gate corpus.

### Driver: materialised native comparison

Keep `bennett-runtimes.bc` in the production §19.1 link. The native artifact is
separate bitcode so native primitive definitions cannot enter the device
pipeline (§3.1), and the same LLVM major performs codegen (§17.7). For an
already materialised module, use the **support** artifact, preserving its
materialised functions and L console model:

```sh
llvm-link device.bc bennett-runtimes.bc bennett-arith.bc --only-needed -o base.bc
llvm-link base.bc driver-layout.bc --override=musl-sysroot/lib/native-support.bc -o adapted.bc
llvm-link adapted.bc bennett-runtimes.bc bennett-arith.bc --only-needed -o comparison.bc
opt -passes=strip-dead-prototypes,verify comparison.bc -o comparison.bc
clang -O2 -nostdlib -nostdinc -static -no-pie -fno-merge-all-constants \
  comparison.bc -Wl,-T,comparison.ld -Wl,-e,_start -Wl,--no-undefined -o comparison
```

All names above are matching-major publication paths/tools. The last bitcode
link is required: startup introduces exit/environ/stdio-flush dependencies.
`--override` changes the comparison syscall transport and adds startup and
primitives; it defines **no effect marker** and preserves the device's
materialised exit helper/EH globals. Materialisation must already have lowered
all effect calls and arranged entry/argv/EH handling (§§11,14.7,19.2). This
support does not rematerialise inlined marker calls. Do not replace materialised
runtime functions wholesale with the full native artifact; the full artifact
is for native original-input baselines. Native codegen can introduce libcalls:
retain required ABI memory definitions if the materialiser removed them.

`__bennett_rt_native_layout` is a required driver-emitted, constant, 8-byte
aligned object with three LP64 little-endian `uint64_t` fields, in order:
`{abi_version=1, heap_base, heap_limit_max}`. See the shipped `lib/native/layout.h`.
Emit the **resolved §19.5 values**: base is the first page boundary after data;
limit is the largest heap budget, including shadow stack. A linker relocation
for the base is supported, as tested with two actual comparison linker scripts.
The driver must place text/arena/interfaces disjointly and replicate addresses
in device layout. `__bennett_rt_blk.<f>` is k-byte block identity (§14.9), never
this table. The existing driver does not yet emit this table or perform the
support merge; consumer integration remains necessary. No driver code changed.

The native heap maps exactly the configured range using Linux
MAP_FIXED_NOREPLACE; bad ABI/alignment/overflow, collisions and OS low-address
policy fail loudly with fault 7, never relocate or overwrite mappings. Zero
capacity is valid and the first allocation faults 2. History has one record
per heap byte (the maximum possible count because every allocation consumes
`max(n,1)`); no independent fixed allocation cap can fault before heap capacity.
The private anonymous history mapping is native bookkeeping, outside device
addresses. Failure to reserve it faults 2. Allocation zero-fills, free(NULL)
is inert, invalid/interior/double free faults 7, and dead storage is not reclaimed
(§§17.1,17.5; ISA §7.1). Every explicit fault emits its full decimal code on
stderr and traps before subsequent source effects (§11.5). The native console
twin routes top-level output to stdout; private L writes still use the same
bound buffer/capacity/truncation model. Arbitrary syscalls never pass to Linux.

Dialect **b-dialect-spec-0na** records both readings of the §3.1 native primitive
exception/configuration ownership and §19.5 transport. This implementation
follows the explicit native-comparison requirement with production primitives
left undefined. ASan/UBSan runtime closure remains tracked in brt-nuu.1 (brt-nuu stays open); this
publication does not silently link hosted sanitizer libraries. The driver also
continues to own §17.7 division checks and complete shared layout/launch.

The syscall bridge maps stdout/stderr write/writev to the **undefined** console
marker (§11.2). R retains ISA effect/tape semantics; L calls the published
`__bennett_dev_console_write` over bound ordinary reversible state (§11.3).
See [device-models/README.md](device-models/README.md) for state layout, binding,
capacity, fault, native-model history and replay rules. Dialect bead
b-dialect-spec-lbq records both readings of startup/syscall framing/tape details;
the implementation follows the spec's tier distinction. No unsupported-symbol
trap stubs replace musl implementations. Remaining VFS/device/thread and broader
libc families remain promised scope under brt-zct/brt-q1k.
