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
published only in `bennett-runtimes-native.bc` and `lib/native-support.bc`,
never in device bitcode. Both include `_start`; `lib/start.bc` is also available
as standalone startup. No extra start.S is needed with these modules. The driver
prepares the §19.2 device entry itself. See the root README for the required
resolved-layout table and materialised-native link order.

Musl exit/atexit/stdio flush are real upstream code. `_Exit` uses exit_group;
N28–N30 must materialise this nonreturning path with the dedicated returning
`__bennett_rt_exit` helper. That helper sets the specified §14.4 EH state
`{i32 active=4, i32 selector=status, ptr object=null}`. The compiler removes
noreturn/unreachable assumptions and propagates the pseudo-exception; the
whole-program entry catches it (§14.7). The original native sysroot test path
uses a Linux exit_group transport and verifies buffered flush and exit status.
It does not prove the driver's materialised exit path.

Remaining libc/VFS/thread/runtime families retain scope under brt-q1k/brt-zct.
Materialised native comparisons use `scripts/native-check 18|23 INPUT.bc
CHECKED.bc` after the materialised device module is fixed and before merging
`musl-sysroot/lib/native-support.bc`. The adapter adds checks to a separate
output; producer inputs, region identities and ISA mapping keep the original
bytes. `scripts/native-check-link 18|23 MATERIALISED.bc OUTPUT LAYOUT.bc
[COMPARISON.ld]` supplies the standalone strict static link. It preserves
materialised definitions and adds the major-matched freestanding libc closure.
The checked output disables builtin/libcall and allocator inference so code
generation cannot replace a materialised libc body or delete allocation
history and its possible faults. Production allocator annotations are retained.

The checks use individual global objects and exact live primitive heap
records, including zero-size records, and preflight complete memcpy-family
ranges. They detect loads/stores outside a live object, use after free,
invalid/double free, division/remainder faults (5/6), reached unreachable/trap
(4), and explicit fault codes. Native libc allocas and process input bytes have
separate private bookkeeping; device shadow frames remain ordinary heap
allocations. No host allocator or arbitrary allocation-record cap is used.

Host mapping/bookkeeping failures print `bennett infrastructure failure:` and
exit 125. They must be reported as infrastructure failures, separately from
`bennett runtime fault C` and successful dialect outcomes. Source-definedness
classification remains a separate original-input sanitizer/reference build;
these materialised checks do not recover undefined operations erased by
source optimisation or claim the deferred full ASan port.

