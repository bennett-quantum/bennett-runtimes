# julia-shim

Shim runtime letting Julia code run on the allocator and device models (rung 5).

`alloc.c` implements the Julia 1.13 allocation entries named in dialect §5.1.
Objects are nonmoving and remain live in the nonreclaiming device heap. One
ordinary C allocation includes a 16-byte prefix, with the type tag in its last
8 bytes and a 16-byte aligned returned payload. The integer-tag ijl_* signatures
match the stock frontend's emitted IR; julia.gc_alloc_obj takes a pointer tag.
Small/big sizes include Julia's header count, and backing storage also honors
the frontend's full dereferenceable(size) forward window. This extra storage,
alignment probes, padding and allocator bookkeeping are all explicitly charged.

Tests compare the emitted signatures with both publications and execute a stock
Julia 1.13 boxed-object method under the LLVM23 materialised native checker,
including its real type-header and payload stores. This verifies the allocation
boundary. Complete Julia process/TLS/safepoint, GC-statistics, exceptions and
standard-library integration remain broader runtime work tracked in the issue database; this
test is not whole-program rung-5 evidence.
