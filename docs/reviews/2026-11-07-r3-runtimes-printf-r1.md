# Integer printf fixtures — review round 1

Verdict: **CLEAR**. No P0/P1 findings; blocking ids: none.

Reviewed range: `6893225a90d29661becba195282ecd0dbee19d7f..8b728635a6c026a8fb0b587041f2e51b2118c594`
(`main..job/r3-runtimes-printf`, commits `235b717`, `8b72863`). Reviewed head:
`8b728635a6c026a8fb0b587041f2e51b2118c594`; checkout was clean.

The fixtures cover the requested integer conversions, fields, limits, lengths
and register/stack variadic walk. Expected bytes/status are literal,
hand-derivable and independently checked against host glibc. Frontend IR retains
all printf calls. `RETURN\n` is a direct write after the buffered final printf;
the prefix alone is explicitly flushed. The native startup calls ordinary
musl exit after main. The documents correctly reserve executor/reset/replay
acceptance for the rung-3 consumer and do not narrow its obligations.

Verification (headless reviewer TMPDIR/CARGO_TARGET_DIR; compiler runs in
resource-checked user build scopes; no full gate):

- `timeout 360 python3 -I tests/test_printf_integer.py` — PASS, three host and
  three native fixtures per LLVM major, 18 and 23; exact stdout/status, empty
  stderr, no undefined symbols or dynamic imports.
- `timeout 60 python3 -I scripts/quality.py` and
  `timeout 30 python3 -I tests/test_gate_selection.py` — PASS.
- `git diff --check main...8b728635a6c026a8fb0b587041f2e51b2118c594` — PASS.
- Reexecuted test-first `235b717:tests/test_printf_integer.py` in a fresh
  temporary directory: host conversion table passes, then the missing native
  golden raises the expected FileNotFoundError. Main lacks these fixtures,
  tables and registered test; no branch switching was used.
- Scratch conversion mutations: an early fflush before RETURN is rejected on
  the production-LTO/native route; `_Exit(0)` skips cleanup and loses FINAL on
  the original-bitcode/native control, which the literal checker rejects.

P2+ nits: **brt-9ak** (priority 3, review) records the pre-existing post-production-LTO `_Exit` native-comparison hang (`scripts/native-link:24`, `tests/test_printf_integer.py:88`; `timeout 3 .tmp/rev-r3-runtimes-printf-r1/tmp/no-cleanup-18` exits 124; IR contains inlined device-EH stores and a self-loop), outside this rung's normal-return path; and the doc drift guard's omission of exit-status headings (`tests/test_printf_integer.py:56`, changing Fields status 7 to 8 in memory leaves all tuple checks passing). Current fixture outputs and documented statuses are correct.

Coverage: read closely all seven changed files, common/pin/native-link/startup
support, main-side musl tests and suite registration; skimmed surrounding
syscall/transport code and quality implementation. Checked every COMMON
anti-slop rule; no blocking violation. Plan 9–11: ratchet registered and
passing, per-repo HANDOFF absent, README points to the meta handoff.

Provenance: implementer gpt-6.1-sol; reviewer gpt-6.1-sol, fresh session (D9).
