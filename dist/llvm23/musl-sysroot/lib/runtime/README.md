# Ordinary materialised helper bodies

`shadow.c` implements the published stack operations over one primitive
backing allocation plus ordinary metadata. Frames and dynamic allocations have
separate extents. Pop and restore invalidate younger entries; a reused address
is valid for the new live extent. New payloads are zeroed. Alignment and
capacity are checked before metadata/payload changes. Init is idempotent.

The native checker consults these logical entries for a range in shadow
backing storage, then retains its physical allocation checks. It admits no
unused capacity or range crossing distinct entries. Primitive zero-fill and
metadata storage have their explicit internal paths; no runtime-name blanket
exemption is used. SP/rsp/x2 state tracks the current absolute shadow pointer.
The table/entry C types are producer-internal, not a consumer ABI. The opaque
table-handle contract for the normaliser remains a published-contract request.

`integer_core.c` and `integer_arithmetic.c` implement bit operations, funnels,
shifts, saturation, deposit/extract, carryless/ordinary multiplication,
division/remainder, fixed-point and multiplication-overflow arithmetic. Limbs
are least-significant first, with the true partial top limb masked. Signed
magnitudes avoid C signed overflow. Division zero/INT_MIN÷−1 report 5/6;
unrepresentable nonsaturating fixed division is source UB and faults 4.
Fixed multiplication chooses arithmetic-right-shift rounding; division chooses
truncation toward zero. Out-of-width poison shifts and zero-undefined counts
use the dialect's zero convention. No hardware FP, host bigint or numerical
placeholder is used.

`scripts/integer_helpers.py` emits exact typed LLVM entries. Scalar entries
retain their declared iW/aggregate signatures; wide runtime rows use canonical
8-byte limb slots. Arithmetic overflow tuples retain the N27 signature and
undergo N22 legalisation. The prebuilt widths are a cache, not a capability
limit. The private emitter has tests at arbitrary admitted widths including
8,388,608 bits; its operation selector and Python API do not define the pending
consumer instance request/delivery contract.

`control.ll` preserves exact aggregate/pointer signatures for EH resume,
jump-buffer state and trampolines, with field offsets from DataLayout and
unaligned record stores. `control.c` supplies type-ID metadata and the stack
guard/failure path. The main-thread TLS symbol aliases the prepared musl thread.

Focused tests cover actual-publication logical lifetime faults, independent
Python-integer oracles, exact typed/slot adapters and record guards. The shared
math fixture has no host imports; raw wide arithmetic tuples are verified as
LLVM and their kernels tested through limb adapters before N22. These are
component results, not full driver/ISA or coherent-action acceptance.

Full EH personality matching, fiber rewind, and platform/trap closure remain
owed. Filter extents, the caller/rewind record ABI, platform inventory/code-map
handoffs and configurable global delivery need published agreement between
the normaliser and runtime. Undefined helper symbols have not been replaced by
trap/no-op implementations. The determining witnesses and requests are tracked
in the repository's issue database.
