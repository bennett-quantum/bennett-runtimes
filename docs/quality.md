# Runtime quality measurements

The default test command is `timeout 180 scripts/gate` (`--fast` is equivalent).
It passed for LLVM 18 and 23 in **171.77 s** wall time on the shared host, measured
with `/usr/bin/time -p -o .tmp/quality-runtimes/fast.time timeout 180 scripts/gate`.
The subset includes the Python integer oracle (217,459 cases per major and the
8,388,608-bit count), allocator boundary checks, global byte-set oracle, host
write failures, EH/control records, fenv vectors, independent host-glibc
stdio/allocator differential, and harness/cache/ratchet tests.

`tests/suites.toml` assigns every `test_*.py` exactly once. `scripts/gate --all`
retains all original tests and full publication reproduction. `--slow` includes
SQLite memory-db (~1,500 s), native corpus tests, sanitizers and the other expensive
checks; none was removed. The full push gate is reserved for orchestration.
`--promised` is red by design for the three missing helper definitions, tracked
by brt-4dk and brt-4dk.1 in `tests/promised-helpers.toml`. Ordinary runs report
these gaps without representing them as passing capability tests. This work
pins dialect 0.2.13; later contract changes are outside this batch.

## Coverage

`timeout 120 scripts/coverage` measured **20.93% handwritten C line coverage**
(261 / 1,247 lines), LLVM 23 source instrumentation with `llvm-profdata merge`
and `llvm-cov report`. Branch coverage: **11.90%** (168 / 1,412).
All handwritten C under `src/` and `julia-shim/alloc.c` is compiled with coverage
mapping; unexecuted objects remain in the denominator. Vendored musl, headers,
LLVM IR/assembly and test fixtures are excluded. This is a conservative executed
host-harness number, not the audit's structural estimate and not full gate
coverage. Reports are regenerated under `$TMPDIR/coverage/report.txt`; profiling
never modifies `dist/` or adds a production import.

The harness executes real allocator alignment/budget histories, the independent
byte-set global-range oracle, and exhaustive 1–8-bit unsigned arithmetic/count/
reverse cases. Five least-covered module groups and reasons:

| Group (0% in this profile) | Existing executed evidence and instrumentation gap |
| --- | --- |
| `src/native/syscall.c` | Real ENOSPC/EBADF/EPIPE and invalid write returns tested on both majors; exit 125 bypasses hosted profile flush. |
| `src/musl/context.c`, `vfs.c`, `syscall.c` | Process, descriptor, file and libc-route tests execute the publication; musl TLS/process setup needs an instrumentation-only entry/flush boundary. |
| `src/device/` | Actual published-model history reversal and checked range tests execute model code; their store instrumentation is separate from profiling. |
| `src/allocator/language.c`, `julia-shim/alloc.c` | Language/Julia ABI tests execute emitted bitcode and checked boxed-object stores; host profile currently exercises C allocation only. |
| `src/runtime/shadow.c` | O0/O2 publication tests execute live/dead/range cases; profile needs the ordinary shadow heap/native-check boundary. |

Broader instrumentation is filed as **brt-8lk**, with a design to reuse the
existing pinned publication tests and a hosted profile-flush boundary. There is
no coverage threshold.

## Test oracles and bounds

`test_host_libc.py` builds the same C program against host glibc and the runtime's
musl publication, then compares exit status, stdout and stderr exactly. It covers
nine allocator size boundaries, calloc zero fill, alignment and realloc's live
prefix, plus 35 formatting/stdio records (including truncation). It compares
standard guarantees; runtime-specific nonreclaiming history/zero/move policies
retain their independent primitive-history tests. Structural export/layout
checks remain supplementary to executed tests.

Invalid internal integer widths are checked before operand reads and VLA sizing:
LLVM admits widths 1 through 2^23. A 64 KiB-stack regression exercises invalid
0, 2^23+1 and UINT32_MAX for bit, multiplication and division paths, requiring
fault 4 rather than SIGSEGV. Valid maximum-width count execution remains tested.
The bound does not make large valid automatic arrays small: the largest core
scratch is 1 MiB and division scratch is about 6 MiB, so callers still need an
appropriate execution stack. C free rejects missing live C ownership before
primitive mutation; direct primitive users retain their primitive free entry.

## Build reproducibility

`--incremental` caches intermediates by source/input content, the complete
prepared header set, tool identity and flags. The cache is private under
`TMPDIR`; its contents and paths never enter the publication. Source/header
mutation tests require misses and byte-preserving hits. Clean builds remain the
default. Both `TMPDIR` and `BENNETT_LLVM18_BIN` / `BENNETT_LLVM23_BIN` are honored.
`timeout 600 python3 -I tests/test_reproducibility.py` passed: a relocated
clean build, an incremental build, and a second incremental build of both majors
were compared using `cmp` for every output file, then compared with `dist/`.
The second incremental build was required to report zero misses. Each major has
298 cacheable commands. LLVM 23 frontend input names are relative to the prepared
source root before IR emission, keeping local-symbol GUIDs independent of TMPDIR.
The unit cache test also checks source/header changes invalidate entries.

## Quality ratchet

`python3 -I scripts/quality.py` is the single additional `gate.toml` check.
`--record` explicitly refreshes the JSON baseline below after reviewed work.
Metrics cover handwritten `src/`, Python/build tooling in `scripts/`, and the
Julia shim; tests/vendor/generated publications are excluded from complexity.
Python uses AST function spans, nesting-weighted conditional/loop/exception
nodes and Boolean chains. C uses comment/string-stripped brace-delimited
function bodies, nesting-weighted decision tokens and Boolean chains. This is a
stable lexical proxy, not a claim of Sonar-compatible cognitive complexity.
C body spans begin at the opening brace; Python spans include the definition.
The distribution buckets are recorded per file. The gate rejects increases in
maximum function length, counts above 20/50/100/300 lines, complexity mass or trivial forwarding
wrappers per file, and in tracked litter/process-identifier counts. New files
have a zero baseline. A forwarding wrapper is a single call/return-call body.
Tests demonstrate each metric can reject a regression. No semantics-bearing
function exceeds 300 lines in this measured scope.

Reproduce litter inspection with `git ls-files | rg '(__pycache__|\.pyc$|\.log$|LAST-RUN|PROGRESS|(^|/)(LOCK|[^/]*\.lock)$|\.darc)'`.
Inspect production identifiers with `rg -n 'brt-[a-z0-9]+|sol-runtimes|quality-runtimes|shift [0-9]+' src scripts julia-shim allocator libc-shims device-models`;
the classifier's own patterns in `scripts/quality.py` are excluded from its count.
The baseline records zero tracked litter and zero production process identifiers.

<!-- quality-baseline -->
```json
{
  "files": {
    "julia-shim/alloc.c": {
      "complexity_mass": 3,
      "functions": 6,
      "lengths": {
        "1-20": 6
      },
      "max_lines": 6,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 1
    },
    "scripts/build": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/build.py": {
      "complexity_mass": 24,
      "functions": 3,
      "lengths": {
        "1-20": 2,
        "101-300": 1
      },
      "max_lines": 203,
      "over_100": 1,
      "over_20": 1,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 1
    },
    "scripts/build_cache.py": {
      "complexity_mass": 25,
      "functions": 2,
      "lengths": {
        "1-20": 1,
        "21-50": 1
      },
      "max_lines": 37,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/coverage": {
      "complexity_mass": 12,
      "functions": 2,
      "lengths": {
        "1-20": 1,
        "21-50": 1
      },
      "max_lines": 49,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 1
    },
    "scripts/gate": {
      "complexity_mass": 11,
      "functions": 2,
      "lengths": {
        "1-20": 1,
        "21-50": 1
      },
      "max_lines": 25,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/integer_helpers.py": {
      "complexity_mass": 58,
      "functions": 2,
      "lengths": {
        "1-20": 1,
        "21-50": 1
      },
      "max_lines": 50,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/musl.py": {
      "complexity_mass": 8,
      "functions": 1,
      "lengths": {
        "21-50": 1
      },
      "max_lines": 48,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/native-check": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/native-check-link": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/native-check.cpp": {
      "complexity_mass": 178,
      "functions": 17,
      "lengths": {
        "1-20": 9,
        "21-50": 7,
        "51-100": 1
      },
      "max_lines": 68,
      "over_100": 0,
      "over_20": 8,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 0
    },
    "scripts/native-link": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/native-sanitizer-link": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/pinned_sibling.py": {
      "complexity_mass": 3,
      "functions": 1,
      "lengths": {
        "1-20": 1
      },
      "max_lines": 13,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/pre-push": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "scripts/quality.py": {
      "complexity_mass": 108,
      "functions": 6,
      "lengths": {
        "1-20": 3,
        "21-50": 3
      },
      "max_lines": 39,
      "over_100": 0,
      "over_20": 3,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/allocator/bump.c": {
      "complexity_mass": 20,
      "functions": 7,
      "lengths": {
        "1-20": 6,
        "21-50": 1
      },
      "max_lines": 32,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 2
    },
    "src/allocator/language.c": {
      "complexity_mass": 7,
      "functions": 27,
      "lengths": {
        "1-20": 27
      },
      "max_lines": 5,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 8
    },
    "src/device/console.c": {
      "complexity_mass": 10,
      "functions": 1,
      "lengths": {
        "21-50": 1
      },
      "max_lines": 24,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/device/models.c": {
      "complexity_mass": 25,
      "functions": 5,
      "lengths": {
        "1-20": 5
      },
      "max_lines": 13,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/device/transports.c": {
      "complexity_mass": 1,
      "functions": 6,
      "lengths": {
        "1-20": 6
      },
      "max_lines": 3,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 5
    },
    "src/fenv/environment.c": {
      "complexity_mass": 11,
      "functions": 23,
      "lengths": {
        "1-20": 23
      },
      "max_lines": 8,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 3
    },
    "src/libc/memory.c": {
      "complexity_mass": 6,
      "functions": 3,
      "lengths": {
        "1-20": 3
      },
      "max_lines": 9,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/libc/utc.c": {
      "complexity_mass": 1,
      "functions": 4,
      "lengths": {
        "1-20": 4
      },
      "max_lines": 5,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 3
    },
    "src/musl/context.c": {
      "complexity_mass": 9,
      "functions": 3,
      "lengths": {
        "1-20": 3
      },
      "max_lines": 16,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 1
    },
    "src/musl/syscall.c": {
      "complexity_mass": 154,
      "functions": 5,
      "lengths": {
        "1-20": 3,
        "21-50": 1,
        "51-100": 1
      },
      "max_lines": 75,
      "over_100": 0,
      "over_20": 2,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 1
    },
    "src/musl/vfs.c": {
      "complexity_mass": 365,
      "functions": 16,
      "lengths": {
        "1-20": 12,
        "101-300": 1,
        "21-50": 3
      },
      "max_lines": 105,
      "over_100": 1,
      "over_20": 4,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 0
    },
    "src/musl/vfs.h": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/native/heap.c": {
      "complexity_mass": 89,
      "functions": 19,
      "lengths": {
        "1-20": 16,
        "21-50": 3
      },
      "max_lines": 32,
      "over_100": 0,
      "over_20": 3,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 2
    },
    "src/native/layout.h": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/native/os.c": {
      "complexity_mass": 0,
      "functions": 1,
      "lengths": {
        "1-20": 1
      },
      "max_lines": 9,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/native/sanitizer.c": {
      "complexity_mass": 2,
      "functions": 1,
      "lengths": {
        "1-20": 1
      },
      "max_lines": 12,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/native/start.S": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/native/start.c": {
      "complexity_mass": 0,
      "functions": 2,
      "lengths": {
        "1-20": 2
      },
      "max_lines": 8,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 1
    },
    "src/native/syscall.c": {
      "complexity_mass": 15,
      "functions": 7,
      "lengths": {
        "1-20": 7
      },
      "max_lines": 13,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/runtime/control.c": {
      "complexity_mass": 4,
      "functions": 2,
      "lengths": {
        "1-20": 2
      },
      "max_lines": 7,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 1
    },
    "src/runtime/control.ll": {
      "complexity_mass": 0,
      "functions": 4,
      "lengths": {
        "1-20": 4
      },
      "max_lines": 10,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/runtime/integer.h": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    },
    "src/runtime/integer_arithmetic.c": {
      "complexity_mass": 113,
      "functions": 13,
      "lengths": {
        "1-20": 12,
        "51-100": 1
      },
      "max_lines": 87,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 2
    },
    "src/runtime/integer_core.c": {
      "complexity_mass": 140,
      "functions": 3,
      "lengths": {
        "1-20": 2,
        "51-100": 1
      },
      "max_lines": 75,
      "over_100": 0,
      "over_20": 1,
      "over_300": 0,
      "over_50": 1,
      "trivial_wrappers": 1
    },
    "src/runtime/shadow.c": {
      "complexity_mass": 42,
      "functions": 9,
      "lengths": {
        "1-20": 9
      },
      "max_lines": 18,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 2
    },
    "src/runtime/shadow.h": {
      "complexity_mass": 0,
      "functions": 0,
      "lengths": {},
      "max_lines": 0,
      "over_100": 0,
      "over_20": 0,
      "over_300": 0,
      "over_50": 0,
      "trivial_wrappers": 0
    }
  },
  "litter": [],
  "process_identifiers": []
}
```
<!-- /quality-baseline -->
