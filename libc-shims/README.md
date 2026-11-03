# musl platform integration

The upstream musl 1.2.5 source archive and licence are in `vendor/`.
`scripts/musl.py` selects upstream stdio, string, conversion, errno,
lock and exit implementations with the recorded patches below, builds them with each LLVM major, and publishes
all upstream headers. `src/musl/context.c` supplies the static process context
(thread pointer, errno/locale backing, environ) and routes musl's internal
calloc to the published zero-filling allocator. No host TLS/libc is used.

Generated platform headers live outside the upstream source tree. Syscall
wrappers call `__bennett_rt_syscall`, whose device policy is documented in
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

Remaining VFS/thread/runtime families retain scope under brt-zct.
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

Private global metadata uses a sorted index and prefix maximum end addresses.
For a range starting at p, the index only considers objects beginning at or
before p and requires one of their ends to cover the whole access. It preserves
the original single-object predicate for overlapping or duplicate globals and
never joins adjacent objects. An exhaustive independent byte-set oracle checks
that equivalence, including zero and wrapping ranges. The index is private
native support, outside device identity and heap budgets.

Implicit byval argument copies are separate callee-local objects, registered
at entry even in functions without explicit allocas. Their extents use the
DataLayout allocation size, including scalar and aggregate padding; the frame
mark removes them on every materialised return while preserving caller
objects. Each byval call first checks its whole source range, before native
codegen performs the implicit copy. Nested, empty, unaligned, large, multiple
argument and escaped-copy cases are tested at O0/O2 on both majors. Native
fault and infrastructure diagnostics also avoid stack initialisers that O0
could lower to instrumented memcpy on their private, unregistered stack.

Host mapping/bookkeeping failures print `bennett infrastructure failure:` and
exit 125. They must be reported as infrastructure failures, separately from
`bennett runtime fault C` and successful dialect outcomes. Source-definedness
classification remains a separate original-input sanitizer/reference build;
these materialised checks do not recover undefined operations erased by
source optimisation or claim the deferred full ASan port.

The floating-point environment is one runtime definition of
`__bennett_rt_fenv`, LLVM `{ i32, i32 }`, align 4, with round at offset 0 and
flags at offset 4, initially zero. Arith declares and updates the same object.
`__bennett_rt_fenv_get_round/get_flags/get_env`, corresponding `set_*` helpers,
and `reset/reset_round/reset_flags` expose its state as ordinary bitcode.
The packed environment has round in the low 32 bits and flags in the high 32.
Invalid helper state faults with code 4 before either field changes.

The standard C `fe*` functions use that object, including snapshots,
hold/update, and `FE_DFL_ENV`. The target header retains musl's public rounding
and exception encodings and adds `FE_TONEARESTAWAY` (0x1000). `FE_ALL_EXCEPT`
is 61, the five IEEE exceptions implemented by the deterministic profile;
the x87 denormal-operand extension is excluded. The runtime translates these
C encodings to published round codes 0..4 and IEEE bits 1,2,4,8,16. These
tests establish shared-state layout and behavior, not arith's full-domain
accuracy or exact-flag obligations.

Static entry preparation is `void __bennett_rt_prepare_process(int argc,
char **argv, char **envp)`. The caller supplies fresh linked static images and
nonnull, null-terminated vectors, with `argv[argc] == NULL` and nonnull strings
for all earlier arguments. Preparation installs `environ` and musl/GNU program
names (basename after the last slash), resets errno and fenv, and establishes
the static main thread, C locale, and 4096-byte libc page size. It performs no
Linux CRT, TLS, auxv, or device operation. The driver invokes preparation before
the whole-program entry; stdio objects and exit-handler lists begin at their
linked static images. The helper does not reset descriptor-bound model state.

The closure also publishes the unchanged upstream musl `locale/localeconv`
and `legacy/getpagesize` implementations. The C-locale record contains "."
as its decimal point, empty grouping/monetary strings and CHAR_MAX for the
unspecified monetary fields. The x86_64 musl target's page size is 4096,
matching the prepared libc state. These use the pinned target headers and
upstream data/code; they make no host locale, sysconf or auxv query. The API
test checks every lconv field and the page size before and after explicit
process preparation, with static checked-native O0/O2 builds on both majors.
Further locale/runtime capabilities remain within brt-zct's full scope.

Comparison `_start` registers its Linux input objects, calls preparation, then
uses `exit(main(argc, argv, environ))`. A main return and explicit `exit` run
handlers in reverse registration order and flush buffered stdio once;
`_Exit` runs neither. Device entry uses the published normalised exit/EH path
and inherited ISA effect tape. Native/context and store-history tests do not
establish full R execution under the repo's ISA 0.8 pin (BLK-5).


The ordinary data-image VFS stores its namespace, inodes, sparse 4096-byte
pages, descriptors and shared open-file descriptions in runtime globals and
allocator memory. It has no host filesystem or fixed file/page/descriptor
table cap. Descriptors close and can be reused; dup shares offsets. Regular
open/read/write/pread/pwrite/seek, metadata, append, truncation, rename/unlink
and FILE operations use this state. Sparse reads return zero, shrinking a
file removes pages/zeros its last page, and later growth does not reveal old
bytes. Unlink retains data for live open descriptions. Directory path walking
and openat preserve directory checks, and directory rename updates cwd and
child names. Process preparation installs fresh standard descriptors.

Upstream clock_gettime/gettimeofday/time and getrandom/getentropy reach the
clock/entropy effects. Random-device file reads use the same entropy tape.
Block-file reads/writes use sector effects, including read-modify-write for
partial sectors and absent-sector zero reads. Native comparison replaces only
the named device transports; VFS and descriptor instructions remain the same
checked materialised code. Test-only production fixtures retain original
marker calls and supply model adapters plus primitive support. A same-source
host fixture writes only under the job TMPDIR and checks independent file
results; the component checks do not claim whole-program R/action acceptance.

The SQLite closure adds upstream musl bcmp/strncmp/strspn/strcspn/strrchr,
the bounded-memory helpers they use, and a UTC time-conversion profile.
`localtime[_r]` and `gmtime[_r]` use unmodified musl `__secs_to_tm`, set DST and
GMT offset to zero, and use the zone name UTC. Unrepresentable `tm_year` values
return NULL with EOVERFLOW; no host clock, TZ file or timezone lookup is used.

The archive stays byte-pinned. `vendor/musl-exact-string-bounds.patch` selects
musl's existing scalar fallbacks for strlen, strchrnul and memchr under the
recorded runtime profile. Their word-at-a-time paths can read beyond a small
live string before noticing its terminator. Archive and patch provenance are
recorded; SQLite's source and vectorisation flags are unchanged.

The FILE closure includes upstream fmemopen/open_memstream and read, write,
seek/tell/position, EOF/error, append and close operations. The versioned
`vendor/musl-zero-memory-file.patch` prevents a write through a zero-sized
fmemopen buffer and reports capacity failures as ENOSPC/F_ERR with partial
write counts, instead of losing buffered output while reporting success.
These are ordinary FILE, buffer and allocator state; no host file is opened.

The unmodified SQLite 3.54.1 archive is a pinned test input (see
`vendor/SQLITE-README.md`). The component test uses the spike's OS_OTHER
memory-only VFS profile and the actual SQLite memdb implementation. The query
takes caller image/length/rowid parameters; the input region is padded to a
power of two, and tests use both the original image and a writable unaligned
copy. Every one of 1026 rowids starts in a fresh cold process and is checked
against an independent predicate; an attempted UPDATE must return READONLY
and preserve input bytes. The stock frontend and LTO vectorise normally;
N21 scalarisation and the published N23 explicit va-block layout precede native
memory/fault checks. Independent test support prepares that varargs phase;
scalar, pointer, FP, large-byval and va_copy vectors validate its ABI. It is
not a production normaliser. A comparison-only
test adapter transports already inlined console markers. This is a static
libc/memdb component check, not complete driver materialisation, quantum
execution, generic disk/filesystem support, or full rung-4 conformance.
