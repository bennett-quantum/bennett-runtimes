# Handoff — strict O0 UBSan minimal comparison subset

Updated UTC: 2026-11-01T14:42:16Z (`date -u`). Offline/headless; no push; driver read-only.
Pinned dialect **0.2.10 @023f5a771a814270634db3be9cfde88ded69f975**, ISA **0.8**.
First implementation commit **504abe5**; following refinement/handoff commit
adds complete Apache licence and tests the resolved comparison linker script.
Initial checkout was d8f7591, the consumer bead-only update after 5c045f9.

## Publication and exact limits

Both majors publish the unchanged, SHA-pinned compiler-rt UBSan minimal archive,
`sanitizer-support.bc`, compiler-rt copyright/provenance and full Apache licence.
The archive imports exactly **abort, strlen, write**. Existing real musl supplies
strlen/write; native-only abort resets/unblocks SIGABRT and terminates via Linux
tgkill. No sanitizer handler is faked. Native musl write(2) now preserves stderr
instead of redirecting sanitizer diagnostics to stdout. All production bitcode
SHAs remain unchanged; native/full and native-support SHAs change for stderr.

| LLVM | Path under dist/llvmM | SHA-256 |
|---|---|---|
| 18 | `libclang_rt.ubsan_minimal-x86_64.a` | `66e08178e5231422dbe6717cbb0bd427ee4bb678175830ce1064d9d0b50781da` |
| 18 | `sanitizer-support.bc` | `42e67e298b707090a66b331f47c63916e8364292a5f75a83106c88346d5f9878` |
| 18 | `bennett-runtimes-native.bc` | `9a40f1b1de32a54848a3913e4576e700a260e2d066cf40a16e71a24c3d679dd6` |
| 18 | `musl-sysroot/lib/native-support.bc` | `5aa89fa079f037369d32aad98b6e95dcb43b169400da6c278eaa87336133f313` |
| 23 | `libclang_rt.ubsan_minimal-x86_64.a` | `006cb02eeb8aeda11a20fa6c0d1bdcfadcb59b924be5755f7229dfcdcb8b82d6` |
| 23 | `sanitizer-support.bc` | `5444e22b761e4cf1fa50321dc24c4b0572771298f1edc6a351a2a60aaa19f9ed` |
| 23 | `bennett-runtimes-native.bc` | `b58659c456b8ebb75db5a307860e5ee9d4c18ce277b7d5f510b26b4cd5dbb3ff` |
| 23 | `musl-sysroot/lib/native-support.bc` | `d09e2ad33fe17bc353afe74aebed22da8c7caa8dc1127cbaf942add1af894e9f` |

`manifest.json` and `SHA256SUMS` include all artifacts/licences; manifests retain
source/tool/flag provenance, native export/import lists and complete sysroot
hashes. Vendored archive inputs and fixed hashes are under vendor/compiler-rt
and scripts/build.py. Builds never select hosted runtime libraries.

This is **UBSan minimal**, not `O0-ASan-UBSan`. Instrument original user inputs
with matching Clang `-O0 -fsanitize=undefined -fsanitize-minimal-runtime
-fno-sanitize-recover=all`, using the published musl headers. Published runtime
internals remain uninstrumented. Minimal reports are terse and lack source/type
metadata and vptr checks. There are no ASan allocation bounds, heap/stack/global
redzones or UAF checks. Array index UBSan checks do not imply memory safety.
No ASan-positive execution or faulty-fixture ASan report is claimed.

Actual strict compiler-rt ASan probes fail on both majors: the runtime explicitly
rejects static linkage via `_DYNAMIC` (AsanDoesNotSupportStaticLinkage), alongside
unresolved dlsym/dlvsym/dladdr, pthread TLS/stack/atfork, getrlimit and broader
libc/libm. Do not fake `_DYNAMIC`, supply no-op interceptors or use host libc.
A real port must also initialize constructors/shadow mappings and support the
comparison allocator without colliding with the resolved device heap.
**brt-nuu.2** retains the full ASan implementation and acceptance obligation.
**brt-nuu.1** is closed for the explicitly allowed, documented subset;
**brt-nuu remains open** because full ASan remains, beyond driver cyg.3.

## Driver consumption

README gives full commands. For original-input diagnostics:

1. Compile each original TU (including main) with the flags above; llvm-link TUs.
2. `llvm-link input.bc driver-layout.bc
   --override=bennett-runtimes-native.bc -o native.bc` (retain native closure).
3. `llvm-link native.bc bennett-arith.bc --only-needed -o closed.bc`.
4. Matching Clang `--target=x86_64-unknown-linux-musl -O0 -nostdlib -nostdinc
   -static -no-pie -ffreestanding -fno-builtin -fno-sanitize-link-runtime
   -Wl,-e,_start -Wl,--no-undefined closed.bc sanitizer-support.bc
   libclang_rt.ubsan_minimal-x86_64.a -Wl,-T,comparison.ld -o diagnostic`.

`scripts/native-sanitizer-link M ubsan-minimal INPUT OUTPUT LAYOUT [SCRIPT]`
implements this order; `plain` omits sanitizer objects and is the O0 control.
Unsupported variants, including asan, fail loudly. Never relabel this subset
as ASan or silently downgrade a requested ASan row.

The required `__bennett_rt_native_layout` remains an 8-byte-aligned three-u64
constant `{1, heap_base, heap_limit_max}` with resolved §19.5 addresses/capacity;
base may be a linker relocation. No default, relocation on failure or host
allocator fallback. Full native bitcode is for original-input baselines only.
Keep materialised comparisons on README's support-override-before-final-closure
order; do not overwrite materialised functions with full native bitcode.

Driver integration belongs to cyg.3. Parent bead d8f7591 records that the driver
now consumes native support/layout and passes strict O0/O2 allocation probes;
actual plugin corpus is blocked on bp-xyn and LLVM18 hello on bp-ozp. This
session does not claim device-pipeline or plugin-materialised acceptance.
The driver must separately consume this honest diagnostic subset; production
§19.1 runtime linking and §17.7 O2 materialised baseline remain as documented.

## Evidence

RED: tests/test_sanitizers.py failed on missing LLVM18 sanitizer publication.
GREEN: both-major O0 plain and minimal UBSan run unchanged rung-2 hashtable's ten
golden cases and rung-3 hello with exact golden stdout and empty stderr.
Tests use a resolved comparison linker script and relocated heap-base table.
Deliberate **global array** out-of-bounds write runs silently in plain; minimal
UBSan emits `ubsan: out-of-bounds` on stderr and terminates with SIGABRT.
The faulty fixture crosses an ASan global redzone when the full port is ready;
current ASan tests record strict-link rejection, not successful diagnosis.
All accepted binaries are static EXEC with zero undefined symbols/dynamic
imports. Archive and support imports are audited independently.

Full scripts/gate passes both-major byte reproduction, including relocated
source builds, all previous native/musl/allocator/console checks, plus new
sanitizer cases. Receipt: `.tmp/gate-sanitizers.log`.
ASan rejection logs: `.tmp/sanitizer-tests/asan-link-{18,23}.log`.
No network, host libc/libm/CRT fallback, push, or cross-repository writes.
