# Current state — musl and console runtime publication

Updated UTC: 2026-11-01T13:24:06Z (`date -u`).

brt-1ft is committed as 7c1cd4c. brt-cgb is committed as 095aa2c. brt-et9 fixes relocated LLVM 23 reproduction.
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

The requested frozen consumer ran exactly once, exit 1. Evidence:
`../bennett-conformance/.tmp/rung-3/20261101T131913.989380Z/`.
It uses the new working sysroot headers, but archives runtime commit 6808622
by default: its libc/console audit still names the old missing exports, not
this publication. All current-publication export/route tests pass locally.
No conformance or sibling source was edited; only the authorised dialect
bead export was committed. No push, remote sync, CI or display process occurred.

| Stage | LLVM 18 | LLVM 23 |
|---|---|---|
| fixtures | PASS | PASS |
| plugin build | PASS | PASS |
| VM build | PASS | PASS |
| driver build | PASS | PASS |
| musl C front end | PASS | PASS |
| runtime bitcode link | PASS | PASS |
| post-link LTO | PASS | PASS |
| reversible production | FAIL · bp-ivy | FAIL · bp-ivy |
| launch artefacts | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| native comparison | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| reference admission | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| reference forward = native | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| reference reset | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| reference tape replay | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| VM admission | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| VM forward = native | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| VM reset | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| VM tape replay | BLOCKED · bp-ivy | BLOCKED · bp-ivy |
| runtime libc/console audit (diagnostic) | FAIL · brt-1ft, brt-cgb | FAIL · brt-1ft, brt-cgb |
| R-mode probe (diagnostic) | FAIL · bp-ivy | FAIL · bp-ivy |
| driver probe (diagnostic) | FAIL · bennett-driver-cyg | FAIL · bennett-driver-cyg |
| ISA-0.8 R contract (prerequisite) | FAIL · b-isa-spec-221 | FAIL · b-isa-spec-221 |
| reference device/reset control (diagnostic) | PASS | PASS |
| reference replay API (diagnostic) | FAIL · b-isa-spec-04d | FAIL · b-isa-spec-04d |
| VM device/reset control (diagnostic) | PASS | PASS |
| VM replay API (diagnostic) | FAIL · bennett-vm-u8m | FAIL · bennett-vm-u8m |

The first detached pre-push failed for LLVM 23: Clang !guid metadata for
internal symbols had already hashed the absolute input file before our IR
source_filename normalisation. Same-checkout reproduction hid the defect.
brt-et9 adds a red-then-green relocated-source comparison to scripts/gate and
passes source inputs relative to an explicit subprocess cwd. Metadata is kept,
not stripped. scripts/gate now passes across relocated source paths, including
all sysroot files and both majors. The final detached pre-push result follows
in the session report. Orchestrator owns the worklog; none was written here.

Determining dialect sentences and repository completion criteria:

- §4.5 item 2: “The shim is an ordinary device function”. §19.1: “The driver
  links all bitcode inputs (§19.8), links `bennett-runtimes` and `bennett-arith`
  bitcode of the link's major M”. §17.7: “never host libc or libm”. brt-1ft
  fixed means actual musl headers/bitcode, ordinary printf/puts/write/exit
  closure with our zero-filled allocator and static native startup/comparison
  support, reproducible for both majors. It does not mean a seed libc.
- §11.2: “`bennett-runtimes`' musl maps system calls onto the markers”. §11.3:
  “In an L variant, an effect marker call becomes a call of the runtimes
  function `__bennett_dev_<model>_<op>` with the same operands.” brt-cgb fixed
  here means that marker bridge, ordinary bound model state, specified faults,
  and runtime-level reverse/replay vectors. R keeps ISA tape and executor
  whole-program acceptance remains the conformance gate's responsibility.
