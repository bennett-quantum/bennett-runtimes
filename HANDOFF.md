# Current state — rung-2 runtime publication

Updated UTC: 2026-10-31T22:38:12Z (`date -u`).

`brt-3sa` is complete: `dist/llvm18/bennett-runtimes.bc` and
`dist/llvm23/bennett-runtimes.bc` close the measured hashtable allocation/libc
surface (`malloc`, `calloc`, `free`, `llvm.memset`). Native `memset` and N26's
`__bennett_rt_memset` / `__bennett_rt_memmove` are also defined. Each distribution
has a source/tool/export/digest manifest and SHA256SUMS. Dialect is pinned to
0.2.10 @023f5a771a814270634db3be9cfde88ded69f975; ISA to 0.8 @f88b32f.

Allocation wraps the three undefined ISA primitives (§3.1/16.9), explicitly
zeroes allocations, checks calloc overflow before mutation, and skips null free.
The comparison-only native backend implements exact nonreclaiming byte-bump
history, fixed-base storage, bounds/table preflights and diagnostic traps on
OOM/invalid/double free. It is never in the published device bitcode. See README
for build/consume/native-link flags and limitations.

Validation: `scripts/gate` PASS on both majors, also through the installed
`scripts/pre-push` in a clean detached checkout (88058b0). Byte-identical rebuilds,
export/allockind audits, exact recorded + fresh corpus links with arith, post-LTO
closure, seven failure scenarios per major, and all ten independent native golden
cases per major pass. The native corpus/backend has no unresolved allocation or
memory shim; only host harness I/O/startup uses libc. A discovered host-memset
leak was fixed by retaining runtime ABI definitions through native codegen;
`--only-needed` can drop one before a later libcall is formed.

Consumer gate was run once, exit **1**, with plugin c4ffcd3be30bf7ef9a354a4771aeaa237cd636ed
and VM 151f1ba5c48ab5b510df61eeeabca3cde4192070. Evidence/table/report:
`../bennett-conformance/.tmp/rung-2/20261031T223029.611386Z/`.
Both majors: fixtures/builds/native reference/C frontend/runtime bitcode link/
post-link LTO PASS; S1–S9 production FAIL (300-second timeout, bp-au6); launch,
admission, forward comparison and byte-exact reverse stages BLOCKED by bp-au6.
The actual consumer post-LTO IR has only qalloc/qfree and the three ISA primitives
as non-intrinsic declarations. Full driver-materialised §17.7 comparison and
executor/reversal success remain unverified. No consumer/plugin/VM source edited.

Future scope: brt-zct tracks remaining §§11/13/14/17/20 and integer-helper contracts;
brt-q1k tracks wider libc/allocator families. Dialect b-dialect-spec-jjr records
odd-sized typed allocation alignment versus exact primitive history (spec export
commit fc050a6); follow the executable §16.9 wrapper pending clarification.
No marker/primitive definitions or unsupported-symbol trap stubs are published.
No worklog block (orchestrator owns it), push, CI or display process was performed.
