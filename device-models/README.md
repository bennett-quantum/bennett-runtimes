# Console device model (dialect 0.2.10 @023f5a7)

`src/device/console.c` is ordinary C, compiled with LLVM 18 and 23 into each
combined runtime. No production effect marker is defined.

## Routes and tiers

Real musl `printf`/`vfprintf`, `puts`, `putc`, `write` and `writev` reach
`__bennett_rt_syscall` (`src/musl/syscall.c`). Per §11.2, write on descriptors
1/2 emits `__bennett_effect_console_write(ptr,len)`; writev emits one such call
per nonempty vector in vector order, after validating the whole length sum.
Zero-length writes emit no event. Return values are bytes consumed, including
bytes discarded by a private oracle console. Musl translates negative errno
results. TIOCGWINSZ returns ENOTTY, seek returns ESPIPE, and virtual close has no
host resource to release. These integration choices are recorded with both
readings in dialect bead **b-dialect-spec-lbq**. Other device/VFS syscalls remain
brt-zct scope and take the specified unmodelled-effect fault, never host Linux.

The compiler handles the marker according to §11.1:

- R: ISA `effect console.write`, with the ISA effect tape and whole-run history.
  This C library does not intercept R writes, truncate R output or own a second
  production tape. ISA validation, recording, reset and replay belong to the
  executor (§11.2, §17.6, §19.2).
- L: `__bennett_dev_console_write` with the same operands, lifted as ordinary
  code over the globals below (§11.3); its writes are reversible program state.
- Action: inherited virtual bindings and controlled transitions, with capacity
  failure before mutation (TDL-12). The materialiser supplies the declared
  bounded undo state/history for overwritten cells and controlled inversion;
  ordinary C compilation alone is not that materialisation.
- CC: the compiler's genuine console hostcall; native comparison uses the same
  materialised handling (§11.1, §17.7), not a production marker definition.

## Binding/state layout

Separate externally visible globals (not an ABI-packed struct), all initially
zero, with the following LP64 types and alignment:

| Symbol prefix `__bennett_dev_console_` | Type / alignment | Meaning |
|---|---|---|
| `buffer` | pointer / 8 | Mutable output bytes, supplied by wrapper |
| `L` | uint64 / 8 | Capacity in bytes |
| `n` | uint64 / 8 | Current length, initially zero |
| `bound` | uint32 / 4 | 0 unbound; 1 prepared binding |
| `action` | uint32 / 4 | 0 private oracle; 1 bounded action |

The oracle prologue prepares `buffer`, `L=n(q)/8`, `n=0`, `bound=1`, `action=0`
from the §8.3 binding (§11.3, §16.2). A bound null output q is legal: L=0,
buffer=null, bound=1. Absence of a binding is distinct: bound=0 faults 8, even
for a zero-length operation. Storage is zero-filled by the published allocator
(§17.1); the buffer bytes are declared output slices (§8.6). Binding preparation
is outside control, and is the compiler/driver's responsibility.

For an oracle, copy min(len,L-n) bytes at buffer+n, advance n by that count,
and discard excess as §11.3 requires. An action requires len<=L-n, else fault 2
before mutation. Invalid model state (n>L or nonzero L with null buffer), or a
null source with nonzero copied length, faults 7. Full memory-range checks are
the materialiser's §17 responsibility. Buffer contents and length are ordinary
mutable state; reversal must restore overwritten bytes, not just set n=0.
Nonzero preimages are covered by the tests.

## Runtime-level history/replay tests

`tests/test_console.py` reads the **published** bitcode, instruments each model
store with a test-only preimage recorder, then native-compiles that IR. The test
adapter implements the otherwise undefined marker, records a console event and
calls the real model. The journal stores (address, byte count, old bytes) and
reverses stores in reverse order; it covers both model writes and tape writes.
The test tape has 32 entries `{uint64 len; unsigned char bytes[128]}` plus a
uint64 length. Saved tape is immutable replay input, validated for event length,
order and byte contents before each transition. This bounded native test format
is **not** a published ISA tape ABI or a replacement for executor history.

Tests use actual musl puts/write/writev/putchar routes, compare hello bytes to
native, check byte-exact buffer/tape reset from nonzero preimages, deterministic
saved-tape replay and tampered-byte rejection, zero-capacity binding, truncation,
action capacity, writev aggregate preflight and unbound/unmodelled faults. Musl
stdio configuration is prepared outside the model run; model reset tests cover
device/tape state, not an executor's entire libc/stack state. The conformance
rung-3 gate owns whole-program forward/native, un-run and replay acceptance.

## Native comparison transport

The comparison-only publication compiles the same syscall bridge with
`BENNETT_NATIVE_COMPARISON`. Write/writev retain fd/aggregate/zero-length policy,
but top-level console events reach `__bennett_rt_native_console_write`, a Linux
stdout transport that handles short writes/EINTR and faults 8 on failure.
This is the native console twin, not an effect-marker definition. The ordinary
L/private console function above is unchanged. Exit uses Linux exit_group;
unknown operations still fault 8. Heap mapping and fault diagnostics use a
separate private Linux transport; user syscalls cannot access it through musl.
