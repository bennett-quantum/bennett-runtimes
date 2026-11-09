# Native variadic storage on System V AMD64

The checked adapter describes the storage of the **same materialised LLVM
module** that the matching native backend executes. It does not lower or
normalise arguments. The ABI specifies classification; backend legalization
specifies the representation of direct LLVM operands. Source C types alone
cannot determine that representation.

Authority: [System V AMD64 ABI, §§3.1.2, 3.2.3 and 3.5.7](https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex).
INTEGER uses six GP registers, SSE uses eight XMM registers, SSEUP shares its
preceding SSE register, and X87/X87UP arguments use memory. Byval operands are
already lowered memory-class copies. Source aggregates are classified before
this boundary; do not reclassify a byval pointer as INTEGER.

## Layout inputs and target rules

Read `Module::getDataLayout()` from the module being checked, never from the
adapter executable, host C `sizeof`, a sibling module or a guessed version
string. Store size and allocation size are distinct: a byval object includes
DataLayout tail padding. Use `getTypeAllocSize(byvalType)` and the call's
explicit parameter alignment (or `getABITypeAlign(byvalType)` when absent).
Round stack slots and their alignments to at least eight bytes.

**DataLayout alone is insufficient to infer calling-convention alignment.**
For example, changing `i128:128` to `i128:64` or `i128:256` does not change either
backend's direct i65 stack layout. The target calling convention overrides
ordinary object alignment for legalized integer/vector slots. Do not replace
such target rules with `getABITypeAlign(originalType)`. The adapter is built
with the same LLVM major as native code generation; use `LLVM_VERSION_MAJOR`
to select the two independently verified legalization rules below.

For integer `iN`, read N (or equivalently the module's type size in bits).
N <= 64 is promoted into one GP slot; 65 <= N <= 128 uses two 64-bit pieces.
Their occupied stack size is two eightbytes, not the allocation size of an
arbitrary over-aligned iN object. LLVM 18 marks the first piece split: allocate
it only from GP0..4; on spill reserve GP5 as a shadow and align the first
stack piece to 16, then place the second piece at alignment 8. This prevents
using GP5 for a later argument. LLVM 23 treats **exactly i128** atomically:
use two registers if both fit, otherwise a 16-byte slot aligned to 16 without
consuming a remaining single GP register. Other direct 65..127-bit integers
use independent eightbyte pieces, possibly split between GP5 and the stack,
and stack alignment 8. These are backend representation rules, including
LLVM 18's deviation at the GP boundary; the checker must match them.

The relevant target definitions are
[LLVM 18 CC_X86_64_C](https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/X86/X86CallingConv.td),
[LLVM 23 CC_X86_64_C](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/X86/X86CallingConv.td),
and `functionArgumentNeedsConsecutiveRegisters` in
[X86ISelLoweringCall.cpp](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/X86/X86ISelLoweringCall.cpp).

## Complete classification at this boundary

For direct scalars, read the type store size with this module's DataLayout,
then round to the legalized target slot: eightbytes for small scalars,
16 bytes for f80/f128. Their stack alignments are target rules, independent
of object alignment. Object allocation layout (byval) and target register
assignment must remain separate phases.

| LLVM operand | Register / memory class | Stack fallback, both majors unless specified |
| --- | --- | --- |
| pointer; i1..i64 | one INTEGER | one 8-byte slot, align 8 |
| i65..i127 | INTEGER pieces | LLVM 18 split/shadow rule above; LLVM 23 independent 8-byte pieces |
| i128 | two INTEGER pieces | LLVM 18 split/shadow rule; LLVM 23 atomic 16 bytes, align 16 |
| iN, N > 128 | legalization not yet classified here | deferred explicit infrastructure diagnostic; no invented extent |
| half, bfloat, float, double | one SSE | legalized scalar slot: 8 bytes, align 8 |
| fp128 | SSE/SSEUP, one XMM | 16 bytes, align 16 (legalized f128 slot) |
| x86_fp80 | X87/X87UP, MEMORY | 16 bytes, align 16 (legalized f80 slot) |
| `<1 x i1/i8/i16/i32>` | scalarized INTEGER | 8 bytes, align 8 |
| `<1 x i64>` | legalized MMX, MEMORY on this target | 8 bytes, align 8 |
| `<1 x half/bfloat>` | widened SSE vector | 16 bytes, align 16 |
| `<1 x float/double/fp128/x86_fp80>` | scalarized as its element | same as the scalar row |
| power-of-two vectors <=128 bits, canonical integer/FP elements | one SSE/SSEUP | widened 128-bit vector: 16 bytes, align 16 |
| named direct 256/512-bit canonical vectors | backend MEMORY at variadic calls | legalized 32/64 bytes, align 32/64 |
| unnamed wide vectors | frontend byval MEMORY | DataLayout allocation size, explicit/ABI alignment |
| any byval object | MEMORY, never a GP pointer | DataLayout allocation size, explicit/ABI alignment |
| `nest` | static-chain register R10 | no GP/SSE/stack consumption |
| other representations, including noncanonical/scalable vectors | not classified here | deferred explicit infrastructure diagnostic |

Canonical vector elements are i1/i8/i16/i32/i64, half, bfloat, float, double;
i1 vectors have at most 16 lanes here. A single integer element is accepted
only through i32 or exactly i64. Named 256/512-bit vectors require non-i1
canonical elements and power-of-two lanes. Their backend stack placement
at variadic calls differs from source-ABI named AVX SSE/SSEUP classification;
it consumes no XMM register. All other unclassified operands retain the
existing deferred diagnostic: ignored calls still link and run, and executing
`va_start` reports that no reliable extent can be registered. Direct iN >128
and other deferred representations remain a coverage limit, not an empty
extent or a claim of source undefinedness. Bfloat needs no conversion libcall
for classification; optional conversion-helper availability is separate.

## Extent and evidence requirements

Walk all operands in order, including named operands, to track consumed banks
and stack alignment. Snapshot the stack end after the last named operand;
`va_start` already skips those bytes. Publish `finalEnd - namedEnd`, including
interargument alignment gaps but excluding final caller-frame alignment or
unrelated caller allocas. A callee's live register-save object is separate;
this allocation checker does not claim missing-register-argument detection.

Fresh caller assembly receipts are in `.tmp/pa-reland-spec/assembly/`, generated
by `python3 -B .tmp/pa-reland-spec/assembly.py` using each major's `llc -O0`.
For `take(i32 0, six i64 1, i65/i96 (2^64+7), i64 9)`, the files
`i65-gp6-standard-{18,23}.s` and `i96-gp6-standard-{18,23}.s` show:
LLVM 18 stores at 0,16,24,32 (end 40); LLVM 23 at 0,8,16,24 (end 32).
Without the trailing 9 the exact ends are 32 and 24. The i128a8/i128a32
receipts confirm that changing object alignment cannot select this rule.
`fp128-align{64,128,256}-{18,23}.s` and the analogous x86_fp80 files
likewise show target 16-byte scalar slots regardless of object alignment.
`i65-gp4-standard-{18,23}.s` show LLVM 18 stack-passing both pieces and the
trailing integer, while LLVM 23 uses GP5 for the low piece and stack for the
high piece and trailing integer. `i128-gp4-standard-{18,23}.s` distinguish
LLVM 18 shadow consumption from LLVM 23 atomic rollback. Scalar and vector
receipt files cover all table families; existing named AVX/byval fixtures
supply their frontend-generated caller forms. Assembly is private evidence,
not a generated product file.

Before implementation, retain the four LLVM 18 i65/i96 full-storage failures
and add boundary tests for both majors, arbitrary integer widths through 128,
module layout independence of target slots, byval tail padding/alignment,
and exact end/first byte past end. Valid controls must compare values with
plain native execution; out-of-bounds controls must fault 7 checked. No test
may derive its expected extent from the adapter's output.
