# pa-muse landing review (second eye) — BLOCK

Reviewed the whole runtimes range `origin/main..642c53c` independently:
`d6a4f801d6be3ee9f3743031c2cbc30293de1e79..642c53c5e74e84975ed5998aa3e2c7ab8159e21a`
(head `642c53c`; `dc3ee8d..642c53c` is the round-2 review document only).
Landing scope is runtimes only: driver `12549b2` diff against `origin/main` is empty.

Provenance: implementer gpt-6.1-sol; reviewer muse-wonder-full-1.0, fresh session (D9).
No implementation changes made. Every build/test ran from the owning worktree root
through `.tmp/pa/in-slot.sh 1800`; probe scripts and receipts are in
`.tmp/rev-pa-muse-landing-muse/` (`abi-extra`, `fp128-probe`, `fp128-boundary`,
`fp128-named`, `fp128-spill`, `bf16-probe` `.py`/`.txt`).

## Blocking finding

**P1-1 — oracle — brt-7qn — `scripts/native-check.cpp:123`.** The variadic
classifier treats every `fp128` operand like `x86_fp80`: unconditional
`memory(16,16)`, no SSE slot consumed. On x86-64 SysV `__float128` is SSE
class — one xmm register when available (plain-caller asm: `movaps …,%xmm0`,
`%al=1`, only the int slot stored to the stack), a 16-byte stack slot only
when SSE is exhausted. Published overflow extents are therefore wrong
whenever an `fp128` operand is register-passed, in both directions, on
LLVM 18 and 23:

- Over-publish (accepts OOB): `take(0, 2.0Q, 1,2,3,4,5,6)` has one true
  8-byte overflow slot but publishes `va_push(24)`. A 7th and 8th
  `va_arg(ap,int)` are accepted (checked exit 0); a 9th faults 7 at +24.
  Controls: the same call without the `fp128` publishes 8 and faults 7;
  with a `long double` instead it publishes 24 and faults 7 (fp80 really
  is MEMORY class). Commands: `fp128-probe.py`, `fp128-boundary.py`.
- Under-publish (false fault): named `take(int, __float128, double×8, …)`
  reading a variadic 16-byte vector publishes 16 where the true suffix is
  24; the valid program runs 0 plain and faults 7 checked on both majors.
  Command: `fp128-named.py`.
- Spilled case is exact: `fp128` after eight doubles publishes 16 and a
  valid bit-exact read passes (`fp128-spill.py`), so the defect is only
  the register-passed class. The committed test's "exact extents" claim
  does not cover `fp128`. Same determining shape as brt-zni.

## Prior rounds re-verified

Reran `.tmp/rev-pa-muse-r1/{abi-probes,wide-vector}.py`,
`.tmp/rev-pa-muse-r2/runtime-controls.py` (baseline-red against `d6a4f80`
faults 7 on the first raw-varargs case; `va_push(8)`/`va_push(32)` extents;
checked-native, varargs-fixture, native-byval, integer-printf suites pass),
`tests/test_native_checked_varargs.py` (all cases, both majors, printf exact
bytes `[  musl] -00042 37 002a !\ntail:007` O0/O2), `scripts/quality.py`, and
round-1 `publication-check.py` (SHA256SUMS, manifest source hashes, published
heap.c exact): all pass. brt-zni and brt-1gt are fixed; bennett-driver-6bs is
moot. Reran the driver evidence `bennett-driver-pa-muse/.tmp/pa/printf-evidence.py`
unchanged at `642c53c`: hello and integer printf exact on 18/23 O0/O2, exit 0.

Independent probes beyond rounds 1–2 (`abi-extra.py`): i128 register-pair
valid/boundary/overread, aggregates (`{long,double}`, `{double,double}`,
`{int,int}`), `_Complex double` and `_Complex long double`, and nested
variadic push/pop (va_start after an inner variadic call returns) all match
plain-native controls on both majors; clang coerces aggregates/complex to
scalar operands the classifier handles. (LLVM 18 plain native itself
mis-returns the i128-boundary valid case; checked agrees with plain.)
Whole register-save-area registration is settled P2 (brt-7id/brt-q5l), not re-raised.

P2+ nits: **brt-i2e** (priority 3) only — a `bfloat` variadic operand aborts
checked linking in the classifier; not a blocker because checked bf16
programs already fail to link at base on undefined `__truncsfbf2`.

Coverage: read closely the full source diff (`scripts/native-check.cpp`,
`src/native/heap.c` incl. `__bennett_rt_native_check`, the new test,
`suites.toml`, quality baseline), round-1/2 docs and receipts, and the
classifier's ABI classes probe-by-probe; skimmed dist manifests and verified
their hashes. Checked every COMMON anti-slop rule against the diff: no
finding beyond the above (no process identifiers in source, named hang
guard, exact-byte assertions). `git diff --check origin/main...HEAD` passes.
No full gate run, as instructed.
