# bennett-runtimes

## What this repo is

ordinary code compiled through the driver into ISA objects with certificates: allocator, libc/musl shims, device models (console, clock, entropy, block), the Julia shim runtime, bitcode builds of language runtimes.

Part of the Bennett reboot: many small repositories, two published contracts
(`bennett-dialect-spec`, `bennett-isa-spec`). Depends on: bennett-dialect-spec; bennett-driver as a build tool.

## Where the spec is

The published specs, at the versions in `SPEC_VERSIONS.toml`. Source documents
currently live in the Bennett.jl repo (`Bennett-NorthStar.md`,
`Bennett-ISA-Spec.md`) until the spec repos are split out. The spec wins over
any code, including code in this repo.

## Rules

The one rulebook is bennett/docs/process/COMMON.md; it binds here. Gate: `scripts/pre-push`. Tasks: `bd` (`bd prime` lists its commands).
