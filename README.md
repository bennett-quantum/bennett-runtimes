# bennett-runtimes

Ordinary code, compiled through the driver into ISA objects with certificates:

- `allocator/` deterministic allocator
- `libc-shims/` libc / musl shims
- `device-models/` console, clock, entropy, block
- `julia-shim/` the Julia shim runtime
- bitcode builds of language runtimes (future)

Depends on: `bennett-dialect-spec`, and the driver as a build tool (binary).
Nothing here links against the compiler; it is plain C/Rust/Julia source.
