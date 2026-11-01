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

