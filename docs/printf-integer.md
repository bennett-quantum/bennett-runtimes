# Integer printf native fixtures

[`tests/test_printf_integer.py`](../tests/test_printf_integer.py) tests three
ordinary C `main(void)` programs against hand-derived byte/status literals,
host glibc printf, and the published musl native/original-bitcode route for
LLVM 18 and 23. The full console tables, argument coverage and observable
normal-return flush point are in
[`device-models/printf-integer.md`](../device-models/printf-integer.md).

For each major the test compiles ordinary source against the matching musl
sysroot, checks that every variadic printf call survives frontend compilation,
links the production runtime and gate-pinned arithmetic with `--only-needed`,
runs `lto<O3>`, and uses `scripts/native-link` with an explicit resolved layout.
The result must have no undefined symbols or dynamic imports. No runtime,
sysroot or published bitcode is modified.

The literal oracle derives int limits from 2^31/2^32 and LP64 limits from
2^63/2^64. Field padding is counted from signs and digits, not sampled from
the implementation. The independent host comparison compiles the same source
against glibc with the matching Clang and builtins disabled. All selected
formats have defined, agreeing integer/string semantics; **no format is excluded
from the host comparison**. There are no floats, locale grouping, null `%s`,
out-of-range varargs, positional arguments, `%p` or other implementation-defined
renderings. A host-musl wrapper is not required; musl 1.2.5 source is available
in the vendored archive, while glibc provides the cheap independent differential.

Focused check (use the project's resource-slot/user-scope rules when running):

```sh
mkdir -p .tmp/printf-integer
TMPDIR="$PWD/.tmp/printf-integer" python3 -I tests/test_printf_integer.py
```

Each run writes native and host `.stdout`, `.stderr` and `.json` receipts
(exit status, stdout byte count and SHA-256) beneath `$TMPDIR/printf-integer/`,
alongside original/linked bitcode and executables. Six native goldens (three
per major) must exactly equal the independent tables, including the final
non-newline bytes, empty stderr and exit statuses 0, 7 and 23. The receipts
are generated evidence, not oracle input. The test belongs to the slow suite.

The test-first commit predates the native build route; its initial run fails
on the missing LLVM 18 native golden after the host conversion table passes.
These are native component results. Both-executor rung-3 acceptance, R startup,
normal-return cleanup, whole-run reversal and tape replay remain obligations
of the conformance ladder.
