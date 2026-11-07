# Runtime gate repairs: round 1

Reviewed `91e0c41..3b9247e` (commits `7a8ec02`, `3b9247e`), head
`3b9247e768de175241a115f3aa8a935cdff953c9`. Earlier quality work is outside this review.

Verdict: **CLEAR**. No P0/P1 findings.

The raw allocator creates no C ownership record, so its fixture must use
`__bennett_nz_free`; the runtime ownership check is unchanged. Both new trap cases
require SIGILL and fault 7, and missing traps return distinct failure codes. SQLite
now links the existing effect materialisation into the application before runtime
closure selection; those definitions call the published device models, whose
unbound paths still fault. No ownership check, assertion, fixture coverage, suite
selection or promised capability was weakened, skipped or removed.

Verification (headless, job-local TMPDIR; no full gate run by reviewer):

- `timeout 180 python3 -I tests/test_runtime.py`: LLVM 18/23 PASS, including the
  normal path and all nine traps.
- `timeout 60 python3 -I tests/test_free_ownership.py`: LLVM 18/23 PASS; C free of
  an unowned primitive allocation faults before primitive-state mutation.
- `timeout 900 python3 -I tests/test_sqlite_memdb.py`: LLVM 18/23 PASS; all 1,026
  rowids against the independent predicate, immutable/read-only image and static
  libc closure.
- `timeout 30 python3 -I scripts/quality.py`: PASS. Published runtime source and
  bitcode hashes match the current sources and manifests for both LLVM majors.
- Failure controls without switching branches: compiling the main-side allocator
  fixture against the same publication reproduces SIGILL/exact fault 7 on both
  majors. Reconstructing main-side SQLite TU selection on LLVM 18 retains all four
  non-console effect imports and fails the unchanged closure assertion.

The main checkout's existing gate record
`.tmp/gate/records/3b9247e768de175241a115f3aa8a935cdff953c9.json` reports an isolated
full gate PASS (739.8 s) and ratchet PASS (0.2 s); this is prior gate evidence.

P2+ nits: **brt-6nq** (P3), `tests/test_runtime.py:36`: the inherited substring assertion, extended to the new cases, accepts `fault 70` for `fault 7` (byte-string repro confirmed); prefer an exact diagnostic or a complete numeric token.
No P0/P1 beads.

Coverage: read closely all three changed files, C/native allocator ownership,
SQLite fixtures/oracle, device materialisation/models and native-link adapters;
skimmed build provenance and suite wiring. Checked every COMMON anti-slop rule;
plan items 9–11 hold: ratchet wired and passing, HANDOFF retired with central
README link, no new litter/process identifiers or semantics-bearing slop.

Provenance: implementer gpt-6.1-sol; reviewer gpt-6.1-sol, fresh session (D9).
