# Handoff — resolved-layout native comparison closure

Updated UTC: 2026-11-01T14:21:41Z (`date -u`). No push/network; driver source read-only.
Pinned dialect **0.2.10 @023f5a771a814270634db3be9cfde88ded69f975**, ISA **0.8**.
Implementation commit **43bef24**. brt-nuu's requested native O2 closure is now
published; brt-nuu remains open for its sanitizer/consumer integration scope.

| LLVM | Path under dist/llvmM | SHA-256 |
|---|---|---|
| 18 | `bennett-runtimes.bc` | `6c1741c4e511a08a22eff47ed4380aed626557ee66225b38a884edc83e9f319d` |
| 18 | `bennett-runtimes-native.bc` | `33fe088ada8fe26e48b5073cd49728e23d7da36d6f541d1b2d047f65dd12569f` |
| 18 | `musl-sysroot/lib/native-support.bc` | `2dc059d487ba0214fe34bd7b90a6bf948c9fae3899eb1b828fad9342bd7641a6` |
| 23 | `bennett-runtimes.bc` | `a63f0421e1d40ebfe6bd391ffa9344db0807aa966a03eed8274e225710a54632` |
| 23 | `bennett-runtimes-native.bc` | `7065f87587205d0b3a414eaf0b7a5ddf41b3d353bc477cd06371c62cd9936442` |
| 23 | `musl-sysroot/lib/native-support.bc` | `e775ad18bf7ff43dc68018713dc62ef9e2f829769e7006c26eab1b5e37e4907d` |

Production bitcode SHAs are unchanged from M2. Native definitions are isolated
from production: no primitive or effect marker is defined in the device library.
Native bitcode uses real pinned musl 1.2.5, the same allocator and L console
model, a native build of the same syscall bridge, `_start`/Linux exit, native
console stdout transport and native alloc/free/fault. No host libc/libm/CRT or
hosted sanitizer library is linked. No unsupported-symbol trap stubs were added.

## Resolved layout and driver integration

The required driver-emitted `__bennett_rt_native_layout` constant is three
8-byte-aligned little-endian u64 fields: `{1, heap_base, heap_limit_max}`.
The base may be a linker relocation. Use the resolved §19.5 values: first page
after data and largest heap budget (including shadow stack). Do NOT read
`__bennett_rt_blk.<f>`: §14.9 gives those k-byte blockaddress identity only.
Heap mapping uses MAP_FIXED_NOREPLACE: exact configured address or typed loud
failure, including Linux low-address restrictions; no default/relocation.
History is private native bookkeeping, one record per configured byte, so
max(n,1) can never exhaust an independent smaller allocation table. Zero fill,
no reclaim, free(NULL), invalid/interior/double free and budget fault semantics
follow §§17.1/17.5 and ISA §7.1. Fault diagnostics preserve every uint64 code
before SIGILL; no subsequent source effect executes (§11.5).

Use full `bennett-runtimes-native.bc` for original-input native baselines, never
to overwrite materialised functions. For materialised device input, the order is:

1. `llvm-link device.bc bennett-runtimes.bc bennett-arith.bc --only-needed`.
2. Link that result plus `driver-layout.bc`, with
   `--override=musl-sysroot/lib/native-support.bc` (retain all support definitions).
3. Link the adapted module with production runtimes then arith `--only-needed`
   **again**: startup introduces exit/environ/stdio-flush imports.
4. `opt -passes=strip-dead-prototypes,verify`; same-major clang `-O2 -nostdlib
   -nostdinc -static -no-pie`, driver comparison script, `-Wl,-e,_start` and
   `-Wl,--no-undefined`. Retain any ABI memory helpers native codegen needs.

The support overrides only the syscall transport and adds native functions;
materialised exit helper/EH state and L model remain intact. No effect marker
is defined. The materialiser must already have lowered all effect calls,
including inlined ones. Native startup does not supply §19.2 device entry
preparation or replace §14.7 EH propagation. §17.7 division checks, common
native/device addresses and complete launch remain driver responsibilities.
README contains the exact commands and original-input `scripts/native-link`
usage (required configuration, optional comparison script).

The existing driver does not yet emit this table or implement the support
merge. No actual plugin-materialised driver comparison execution is claimed:
the new adapter tests an already-linked shape, alongside unmodified corpus
baselines. Existing R/ISA whole-program blockers remain as recorded in M2;
this publication does not resolve bp-ivy/BD-B-ISA7 or executor reset/replay.

## Verification and tracking

RED: `tests/test_native.py` initially failed on missing native publication.
GREEN: `scripts/gate` PASS, both majors: same-checkout and relocated-source
byte-for-byte bitcode/manifests/full sysroots; export/import and allocation
attributes; fresh+recorded rung-2 LTO closure; ten unmodified hashtable golden
cases plus rung-3 hello under strict static no-host links and two actual
§19.5 linker layouts. Support-adapter links and missing-layout refusal pass.
Native binaries have zero undefined symbols/dynamic imports. Additional
vectors cover 65,537 zero-size allocations, zero heap, invalid ABI/alignment,
null/overflow/colliding ranges, seven allocator fault cases and arbitrary
uint64 fault codes; musl printf float/atexit/flush/explicit exit/write/putchar
and instrumented console reset/replay/tamper tests pass. The existing allocator
and console unit executables now also use strict freestanding links.

Clean detached `scripts/pre-push` result: **PASS**, implementation SHA
`43bef243db4cbab624dbe773b4947e35a66a4317`. The subsequent
handoff/bead-only commit changes no tested code or publication bytes.
Root receipts: `.tmp/gate-native.log`, `.tmp/pre-push-native.log`.

Dialect **b-dialect-spec-0na**, labels consumer,dialect,runtimes, is filed and
committed as **eaad067** in the dialect repo (export only). It quotes §3.1 and
§19.5 with both readings: native lowering/configuration exception versus a
literal ban on native primitive and driver runtime-object definitions. We use
the reading supporting explicit §17.7 same-major native comparison, isolating
native support from ordinary device shims (§4.5). Accepted spec unchanged.
**brt-nuu.1** tracks the remaining real freestanding ASan/UBSan closure or
specified instrumentation adapter; no no-op sanitizer handlers or host fallback.
No bead closed prematurely. brt-zct/brt-q1k and allocator alignment dialect
b-dialect-spec-jjr retain their broader obligations.

Gotchas found: startup must precede final --only-needed closure; a custom
linker script must explicitly select `_start` (otherwise the first text symbol
can become ELF entry); dead marker prototypes need cleanup after transport
override. An unused malloc result was optimized away, so allocator fault tests
now make it observable through a volatile sink. `bd -C ... export -o` resolves
the output against the shell cwd; use `env -C REPO bd export ...` for sibling
exports. A temporary misdirected passive export was regenerated from the
correct runtime DB before commit; no database or unrelated file was changed.
