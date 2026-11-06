# Runtime quality review, round 1

Reviewed the whole `main..job/quality-runtimes` range (10 commits, `55dcf3e` through
`76877ed`), head **76877ed4f6e9923eea83b5c7278c764639e85de2**.
Verdict: **CLEAR**. P0/P1 findings: **none**.

The console transport now classifies failed host writes as infrastructure exit
125; the regression exercises real ENOSPC/EBADF/EAGAIN, EPIPE with SIGPIPE ignored,
and injected zero/oversized writes, plus successful EINTR/short-write recovery.
Unowned C free faults before primitive mutation. Integer width checks precede
operand access and VLA sizing. Reading the corresponding `git show main:<path>`
implementations confirms the new invalid-width, unowned-free and transport-error
regressions would fail there. Valid maximum-width arithmetic scope remains intact;
large valid scratch requirements are explicitly documented, not claimed solved.

Verification (headless, job-local TMPDIR/target, sequential builds; focused only):

- `timeout 30s python3 -I tests/<name>.py` passed for `test_quality_ratchet`,
  `test_build_cache`, `test_common`, `test_integer_width`, `test_free_ownership`,
  `test_transport_errors`, and `test_gate_selection`.
- `timeout 150s python3 -I tests/<name>.py` passed for `test_host_libc`,
  `test_control_helpers`, `test_integer_helpers`, and `test_fenv` on both majors;
  the integer oracle checks 217,459 cases per major and the 8,388,608-bit count.
- `timeout 130s python3 -I tests/<name>.py` passed for `test_alignment`,
  `test_global_ranges`, `test_realloc`, and `test_allocator_history` on both majors.
- `timeout 600s python3 -I tests/test_reproducibility.py` passed: relocated clean
  builds and two incremental builds per major match every publication byte;
  the second incremental builds require zero misses.
- `timeout 120s scripts/coverage` reproduced **20.93%** C line coverage
  (261/1,247), **11.90%** branch coverage, including unexecuted modules.
- `timeout 10s python3 -I scripts/quality.py` passed. The gate has one ratchet
  check; baseline parsing, length/mass/wrapper/litter/identifier regressions and
  the documented scope were inspected. No tracked litter or production process
  identifiers were found. `HANDOFF.md` is deleted and README links the meta handoff.
- `timeout 10s python3 -I tests/test_promised_helpers.py` exits 1 for the three
  missing helpers, naming `brt-4dk` / `brt-4dk.1`; `--report` reports those gaps
  and exits 0. Suite inventory comparison confirms all 29 original executable
  tests remain in `--all`. Source digests and both SHA256SUMS sets also match.

P2+ nits, one bead **brt-xii**: README.md:4 has a composite incorrect dialect SHA;
tests/test_promised_helpers.py:14 bypasses LLVM path overrides; unused/shadowed
common.run import in tests/production_support.py:6 and duplicate sys import in
tests/test_promised_helpers.py:7. No semantics-bearing anti-slop violation or
scope narrowing was confirmed.

Coverage of review: read closely all handwritten source/build/cache/pin/gate/
coverage/ratchet changes, all new tests and every changed test hunk, plus the
binding laws/brief/plan/audit; skimmed generated manifests, copied sources and
documentation baseline data; verified binary publications by hashes and fresh
byte comparisons. Full push gate and slow SQLite/native corpus suites were not
run, as required by the review brief; no claim of their overall result is made.

Provenance: implementer gpt-6.1-sol; reviewer gpt-6.1-sol, fresh session (D9).
