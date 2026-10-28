# allocator

Deterministic allocator used by compiled programs: fixed base, monotonic bump, `free` marks dead (no reuse), so addresses are reproducible and reversible. Enables `push!`/`Dict` with no compiler awareness (rung 5).
