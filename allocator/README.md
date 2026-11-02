# allocator

Deterministic allocator used by compiled programs: fixed base, monotonic bump, `free` marks dead (no reuse), so addresses are reproducible and reversible. Enables `push!`/`Dict` with no compiler awareness (rung 5).
The C wrappers round `max(n,1)` to `_Alignof(max_align_t)` with overflow
checked before allocation. Runtime code that needs an exact byte allocation
uses `__bennett_rt_alloc(n)`, which calls the unchanged `__bennett_nz_alloc(n)`
primitive and tracks its next byte address. If an odd runtime allocation
precedes a C allocation, the wrapper requests the minimal alignment padding as
an ordinary, separately charged primitive allocation. Padding and rounded
extents participate in the same nonreclaiming allocation history in both
builds. The cursor is ordinary device memory restored by reversal.


C allocation size/liveness is tracked in a linked list in ordinary heap
memory. Each pool slot is a separate explicit 32-byte primitive allocation,
charged to the same budget/history. Freed payloads leave reusable live pool
slots; only that bookkeeping data is reused, while payload addresses and
primitive history remain nonreclaiming. There is no fixed record cap. The
payload/padding prefix retains its exact requested extents; bookkeeping adds
visible allocations and must be included in heap budgets and address traces.

`realloc` always allocates a fresh aligned zero-filled payload, copies
`min(old requested size, new size)` bytes, and then frees the old payload.
Invalid/dead/interior C pointers fault with code 7 before allocation; overflow
faults with code 2. Null input uses malloc; zero size uses the same unique
allocation policy as malloc(0). The primitive supplies zero fill, so the wrapper
avoids a redundant byte-by-byte clearing loop. Actual published allocator/list
stores and primitive history are tested with reversal and identical execution.
