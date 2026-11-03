# Ordinary runtime device models

`src/device/console.c` and `src/device/models.c` are ordinary C, compiled with
LLVM 18 and 23 into each combined runtime. The clock, entropy and block models
implement published dialect 0.2.12 §11.3. No production effect marker is defined.

## Routes and tiers

Real musl `printf`/`vfprintf`, `puts`, `putc`, `write` and `writev` reach
`__bennett_rt_syscall` (`src/musl/syscall.c`). Per §11.2, write on descriptors
1/2 emits `__bennett_effect_console_write(ptr,len)`; writev emits one such call
per nonempty vector in vector order, after validating the whole length sum.
Zero-length writes on an open descriptor emit no event. Return values are bytes consumed, including
bytes discarded by a private oracle console. Musl translates negative errno
results. TIOCGWINSZ returns ENOTTY, seek returns ESPIPE, and virtual close has no
host resource to release. A successful close marks that descriptor closed in
ordinary reversible state. Later write/writev/seek/ioctl/close on it returns
EBADF, while the other descriptor and virtual device binding remain usable.
Fresh process preparation restores the descriptor state. These integration choices are recorded with both
readings in dialect bead **b-dialect-spec-lbq**. Clock/time and random APIs use
the clock/entropy markers. A data-image VFS handles ordinary files, with
`/dev/random`, `/dev/urandom`, `/dev/block` and `/dev/vda` reaching their models.
Genuinely unmodelled operations fault 8, never pass through to host Linux.

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

The comparison-only publication retains the ordinary syscall, descriptor and
VFS bodies. Separate `__bennett_rt_transport_*` definitions replace only device
and exit transports after materialisation/checking. Console output handles
short Linux writes/EINTR and faults 8 on failure. Clock, entropy and block use
the same bound ordinary models and full operand-range checks. Caller scratch
stays in the materialised caller's registered objects; no broad private stack
admission or second host implementation of VFS semantics is needed. No effect
marker is defined/imported by the native publication. Native exit uses Linux
exit_group; private mapping/diagnostics remain inaccessible to user syscalls.
Consumers must re-materialise the runtime closure when updating these bytes,
including the new transport boundaries.

## Clock, entropy and block state

The selected module's wrapper prepares these globals outside control, just as
it prepares console state. All globals initially contain zero. Names below
have prefix `__bennett_dev_`; pointers and uint64 fields align to 8, uint32 to 4.

| Global suffix | Type | Initial binding value |
|---|---|---|
| `clock_bound` | uint32 | 1 when bound |
| `clock_c` | uint64 | descriptor clock value |
| `entropy_bound` | uint32 | 1 when bound |
| `entropy_base` | pointer to preserved bytes | entropy image |
| `entropy_size` | uint64 | entropy image byte count |
| `entropy_pos` | uint64 | 0 |
| `block_bound` | uint32 | 1 when bound |
| `block_base` | pointer to preserved bytes | disk image |
| `block_nsect` | uint64 | disk image sector count |
| `block_overlay` | pointer | null |

`clock_read(p)` stores eight little-endian bytes of c, allowing unaligned p,
then increments c modulo 2^64. `entropy_read(p,len)` copies the next len bytes
and advances pos; len greater than size-pos faults 10 before any mutation or
destination access. Empty entropy reads change nothing. A corrupt pos>size
faults 7. Every operation on an unbound model faults 8, including empty reads.

`block_read(s,p)` copies a present overlay sector, otherwise image sector s
when s<nsect, otherwise 512 zero bytes. `block_write(s,p)` creates or updates
an overlay sector, including sectors beyond the initial image and UINT64_MAX.
It never changes the image. The sparse list has no fixed entry/sector cap:
each first write uses ordinary `malloc` for a 528-byte record, containing
uint64 sector at offset 0, next pointer at 8 and 512 bytes at 16. Repeated
writes reuse that record. Its payload, allocator bookkeeping and heap budget
are ordinary runtime operations. The wrapper only sets the head to null; it
does not inspect private record contents. Missing nonempty buffers and
unrepresentable image offsets fault 7. The materialiser checks full logical
memory ranges; these C functions do not admit a broad host address range.

These globals and functions belong to the selected module's runtime closure.
They impose no process-wide launch/CRT or whole-binary requirement. A mixed
native/device fat object can bind the selected module's state while its host
code retains its own libc and host effects (bennett-4ht). Descriptor preparation,
R effect routing, action undo-state bounds and controlled inversion remain
compiler/driver/executor obligations. There is no additional production tape
or native host device access in these models.

`tests/test_devices.py` instruments stores in the published device and
allocator bitcode with a test-only preimage recorder. Independent vectors
cover clock wrap and byte order, every entropy position/length across a
17-byte tape, fault preflight, immutable disk sectors, absent/distant sectors,
repeated writes and full state/heap restoration followed by repeat execution.
An independent static checked-native harness exercises the same publication
and rejects short clock/entropy/sector/image objects with fault 7. This is
runtime component evidence; the test journal does not establish ISA history
or whole-program coherent/action acceptance.
