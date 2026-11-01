# Current state — musl and console runtime publication

Updated UTC: 2026-11-01T13:19:05Z (`date -u`).

brt-1ft is committed as 7c1cd4c. brt-cgb implementation is gated locally.
Both LLVM majors publish combined bennett-runtimes.bc, a complete musl header
sysroot and native startup bitcode/support. Musl 1.2.5 archive SHA-256:
a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4.
No build fetches anything. Sources remain upstream, with separate generated
platform headers; musl never replaces this repo's zero-filling allocator.

scripts/gate PASS: byte-identical bitcode/manifests/full sysroots; allocator
attributes/address/zero-fill/history and seven fault vectors per major;
recorded+fresh rung-2 link/LTO and ten golden native cases; static musl hello,
formatted printf (including floating point), atexit, return/explicit-exit flush,
write and putchar. Native musl tests link matching arith, no host CRT/libc/libm,
and have no undefined symbols or dynamic imports. start.bc is native startup;
§19.2 device launch preparation and §17.7 materialised comparison remain driver
responsibilities. N28–N30 must use the returning __bennett_rt_exit helper and
propagate/catch the §14.7 EH state, removing noreturn/unreachable constraints.

The console syscall bridge maps fd 1/2 writes to an undefined effect marker,
with writev aggregate preflight and vector-order emission (§11.2). Production
contains no marker/primitive definition. R tape stays in the ISA; the L model
has ordinary bound buffer/capacity/length state (§11.3), distinct unbound and
bound-zero-capacity cases. Private oracle output truncates; bounded action
output faults before mutation. State and test history layouts are documented
in device-models/README.md. Published-model store instrumentation checks
byte-exact device/tape reset from nonzero preimages and deterministic replay,
including tampered bytes. This is native-model validation, not an executor's
whole-program state proof or full DR/DE conformance.

Dialect b-dialect-spec-lbq records both readings of startup, console framing,
terminal metadata and tape ABI; exported alone in dialect commit 7159683.
Implementation follows the explicit per-tier text. brt-zct and brt-q1k remain
open for full runtime/libc/VFS/thread/device families. Odd-sized typed C
allocation alignment still needs b-dialect-spec-jjr; current §16.9 byte-bump
history is preserved. No unsupported-symbol trap stubs replace libc functions.

The requested frozen rung-3 consumer run and pre-push validation follow this
commit. Its default runtime publication is frozen at 6808622, not moving HEAD;
its headers come from this working publication. No conformance/sibling source
was edited. No worklog block (orchestrator owns it), push, remote sync, CI or
display process was performed.
