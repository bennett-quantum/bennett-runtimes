# bennett-runtimes

Ordinary runtime C compiled to LLVM 18 and 23 bitcode, pinned to dialect
**0.2.13 @1c29c8d71a814270634db3be9cfde88ded69f975** and ISA **0.8**.
The publication contains the zero-filling allocator, memory helpers, a real
musl stdio/exit slice and the lifted console model. No device definition
delegates to host libc, and no production marker or ISA primitive is defined.
Broader runtime conformance remains tracked in the issue database.

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
this table. The consumer owns the table and support merge under driver cyg.3; this
repository does not modify driver code.

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
left undefined. The UBSan minimal subset below provides the implemented diagnostic subset; full freestanding ASan remains
tracked in the issue database. No hosted library fallback is used. The driver also
continues to own §17.7 division checks and complete shared layout/launch.

The syscall bridge maps stdout/stderr write/writev to the **undefined** console
marker (§11.2). R retains ISA effect/tape semantics; L calls the published
`__bennett_dev_console_write` over bound ordinary reversible state (§11.3).
See [device-models/README.md](device-models/README.md) for state layout, binding,
capacity, fault, native-model history and replay rules. Dialect bead
b-dialect-spec-lbq records both readings of startup/syscall framing/tape details;
the implementation follows the spec's tier distinction. No unsupported-symbol
trap stubs replace musl implementations. Remaining VFS/device/thread and broader
libc families remain promised scope in the issue database.

### O0 diagnostic: compiler-rt UBSan minimal subset (LLVM 18 and 23)

Each matching-major publication additionally contains the byte-pinned upstream
`libclang_rt.ubsan_minimal-x86_64.a`, `sanitizer-support.bc` (native Linux
SIGABRT implementation of `abort`), `COMPILER-RT-COPYRIGHT`, and `COMPILER-RT-LICENSE`.
`manifest.json` records artifact SHAs, flags and the archive's complete imports:
`abort`, `strlen`, `write`. The latter two are the existing real musl definitions;
native `write(2,...)` preserves stderr. No sanitizer handler is substituted.
Vendored archives are copied from the locally installed matching compiler-rt,
verified against fixed SHA-256s offline; no build consults host runtime libraries.

Instrument **original input translation units**, including main, before linking
un-instrumented published runtimes. These diagnostics are a separate §17.7
baseline; do not instrument or replace already materialised device functions.
For each major M, compile each input with the matching Clang:

```sh
clang --target=x86_64-unknown-linux-musl -nostdinc \
  -isystem dist/llvmM/musl-sysroot/include -O0 -ffreestanding -fno-builtin \
  -fno-pie -fsanitize=undefined -fsanitize-minimal-runtime \
  -fno-sanitize-recover=all -emit-llvm -c input.c -o input.bc
llvm-link input.bc main.bc -o original-instrumented.bc
scripts/native-sanitizer-link M ubsan-minimal original-instrumented.bc \
  diagnostic driver-layout.bc comparison.ld
```

The exact order implemented by that helper is:

1. `llvm-link original-instrumented.bc driver-layout.bc
   --override=bennett-runtimes-native.bc -o native.bc` (retain startup/musl).
2. `llvm-link native.bc bennett-arith.bc --only-needed -o closed.bc`.
3. Matching Clang `--target=x86_64-unknown-linux-musl -O0 -nostdlib -nostdinc
   -static -no-pie -ffreestanding -fno-builtin -fno-sanitize-link-runtime
   -Wl,-e,_start -Wl,--no-undefined closed.bc sanitizer-support.bc
   libclang_rt.ubsan_minimal-x86_64.a -Wl,-T,comparison.ld -o diagnostic`.

The layout table and linker script have the same resolved §19.5 contract as the
O2 baseline above; there is no sanitizer heap default. The helper's `plain`
variant omits sanitizer support/archive for an O0 control. It rejects `asan`
and other unsupported variants loudly. Never label this row `O0-ASan-UBSan`.
Minimal UBSan retains compiler-generated undefined-behavior checks with terse
`ubsan: ...` reports and fatal SIGABRT; it lacks full source/type diagnostics,
vptr checking, ASan heap/stack/global redzones and use-after-free detection.
Only user inputs are instrumented; published musl/allocator internals are not.
An array-index bounds check is not proof of general memory safety.

**ASan is unavailable under this publication's strict link.** Actual pinned
compiler-rt probes on both majors fail: `AsanDoesNotSupportStaticLinkage`
requires `_DYNAMIC`, and loader APIs (`dlsym`, `dlvsym`, `dladdr`), pthread
TLS/stack/atfork and additional libc/libm symbols remain unresolved. Adding
those names as stubs would not provide shadow setup, constructors, allocator
poisoning or working interceptors. No ASan runtime artifact or passing ASan
execution is claimed. The open ASan work retains the complete port and memory-fault
acceptance obligation; driver cyg.3 must consume this subset honestly while
keeping full ASan acceptance outstanding.

`tests/test_sanitizers.py` gates both-major O0 plain and UBSan hashtable (all
ten golden cases), hello under a resolved comparison linker script, zero imports/dynamic dependencies, and a deliberately
faulty array write: plain is silent, minimal UBSan reports out-of-bounds and
aborts. It also records real ASan strict-link failures under
`.tmp/sanitizer-tests/asan-link-M.log`; those are rejection evidence, not an
ASan-positive faulty-fixture result. Full same-path and relocated-source byte
reproduction includes the new artifacts and manifests.

Current coordination: [bennett/HANDOFF.md](../bennett/HANDOFF.md).

Run `scripts/gate` (or `scripts/gate --fast`) for the bounded LLVM 18/23 subset.
`scripts/gate --all` retains every slow test and publication reproduction for the push gate;
`--slow` runs the slow tests alone. `--promised` deliberately fails while the helper
contract gaps listed in `tests/promised-helpers.toml` remain open.

Build with `scripts/build 18 --incremental` (or 23) to reuse content-addressed
intermediates under `TMPDIR`. Omit `--incremental` for a clean build.
`TMPDIR`, `BENNETT_LLVM18_BIN`, and `BENNETT_LLVM23_BIN` are caller-overridable.
Sibling inputs resolve only through `gate.toml` pins.
Coverage and the quality baseline are recorded in [docs/quality.md](docs/quality.md).
