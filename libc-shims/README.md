# musl platform integration

The upstream musl 1.2.5 source archive and licence are in `vendor/`.
`scripts/musl.py` selects unchanged upstream stdio, string, conversion, errno,
lock and exit implementations, builds them with each LLVM major, and publishes
all upstream headers. `src/musl/context.c` supplies the static process context
(thread pointer, errno/locale backing, environ) and routes musl's internal
calloc to the published zero-filling allocator. No host TLS/libc is used.

Generated platform headers live outside the upstream source tree. Syscall
wrappers call `__bennett_rt_syscall`, whose console policy is documented in
`device-models/README.md`. The native-only Linux syscall backend is separate and
never published as device bitcode. `musl-sysroot/lib/start.bc` plus start.S
provide comparison startup; the driver prepares the §19.2 device entry itself.

Musl exit/atexit/stdio flush are real upstream code. `_Exit` uses exit_group;
N28–N30 must materialise this nonreturning path with the dedicated returning
`__bennett_rt_exit` helper. That helper sets the specified §14.4 EH state
`{i32 active=4, i32 selector=status, ptr object=null}`. The compiler removes
noreturn/unreachable assumptions and propagates the pseudo-exception; the
whole-program entry catches it (§14.7). The original native sysroot test path
uses a Linux exit_group transport and verifies buffered flush and exit status.
It does not prove the driver's materialised exit path.

Remaining libc/VFS/thread/runtime families retain scope under brt-q1k/brt-zct.
