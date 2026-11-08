#!/usr/bin/env python3
"""Raw (not N23-materialised) varargs storage under the native checker.

The checked adapter must register every variadic callee's backend-created
register save area and overflow argument area, with exact extents. The
varargs fixture covers GP register/overflow boundary, SSE save area,
MEMORY-class long double and a 40-byte byval struct, va_copy and the empty
list. The printf case asserts hand-derived bytes: %6.4s pads "musl" with
two spaces, %06d counts the minus sign in width 6, %.4x pads 0x2a to four
digits, %03d pads 7 to three digits; the final line has no newline and is
flushed only when main returns. Reading past the exact overflow extent
must still fault 7: registration is an exact extent, not an envelope.

Exactness cuts both ways. The overflow pointer va_start reports already
skips any named arguments the caller placed on the stack, so the published
extent is only the variadic suffix of the stack argument area: with seven
named ints (six in registers, one in the first stack slot) and one variadic
int, exactly one further 8-byte slot is live and a second va_arg read must
fault 7. Stack operands keep their ABI alignment inside that suffix: a
16-byte vector spilled after one double occupies bytes 16-32, and a
32-byte-aligned byval vector is passed on the stack (the caller realigns
the stack for it), whether the callee reads it or not.
"""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-checked-varargs'; TMP.mkdir(parents=True, exist_ok=True)
# Hang guard only, with the same per-command bound common.run applies to
# build commands; every verdict below is exit status and exact bytes.
RUN_HANG_GUARD_S = 180

PRINTF = r'''
#include <stdio.h>
int main(void) {
    printf("[%6.4s] %06d %u %.4x %c\n", "musl", -42, 37u, 0x2au, '!');
    printf("tail:%03d", 7);
    return 0;
}
'''
EXPECTED = b'[  musl] -00042 37 002a !\ntail:007'

# 1 named + 8 variadic GP arguments: 5 register-saved, 3 in the 24-byte
# overflow area. Reading a 9th variadic int touches overflow+24 and must
# fault 7 before the load.
OVERREAD = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    int total = 0;
    while (n--) total += va_arg(ap, int);
    va_end(ap); return total;
}
int main(void) { return take(9, 1, 2, 3, 4, 5, 6, 7, 8); }
'''

# Seven named ints and one variadic int: the seventh named int fills the
# first stack slot, the variadic int the second. One va_arg read is exact;
# a second reads past the caller's stack argument area.
NAMED_STACK_VALID = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int a, int b, int c, int d, int e, int f, int g, ...) {
    va_list ap; va_start(ap, g);
    volatile int x = va_arg(ap, int);
    va_end(ap); return x != 8;
}
int main(void) { return take(1, 2, 3, 4, 5, 6, 7, 8); }
'''
NAMED_STACK_OVERREAD = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int a, int b, int c, int d, int e, int f, int g, ...) {
    va_list ap; va_start(ap, g);
    volatile int x = va_arg(ap, int); volatile int y = va_arg(ap, int);
    (void)x; (void)y; va_end(ap); return 0;
}
int main(void) { return take(1, 2, 3, 4, 5, 6, 7, 8); }
'''
# Register-only control: the named int and five variadic ints fill all six
# GP registers and no stack slot was passed, so a sixth read must fault 7.
REGISTER_ONLY_OVERREAD = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    for (int i = 0; i < 6; i++) { volatile int x = va_arg(ap, int); (void)x; }
    va_end(ap); return 0;
}
int main(void) { return take(6, 1, 2, 3, 4, 5); }
'''

# The 32-byte vector is MEMORY class at O0 (byval align 32). A variadic
# call carrying it must link and run even when the callee never va_starts;
# when the callee does read it, the values are exact, and a second read
# past the single passed vector faults 7.
WIDE_VECTOR_IGNORED = r'''
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int ignore(int n, ...) { return n; }
int main(void) { return ignore(0, (V){1, 2, 3, 4, 5, 6, 7, 8}); }
'''
WIDE_VECTOR_READ = r'''
#include <stdarg.h>
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n); V v = va_arg(ap, V); va_end(ap);
    return !(v[0] == 1.0f && v[7] == 8.0f);
}
int main(void) { return take(0, (V){1, 2, 3, 4, 5, 6, 7, 8}); }
'''
WIDE_VECTOR_OVERREAD = r'''
#include <stdarg.h>
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    V v = va_arg(ap, V); volatile float a = v[0]; (void)a;
    V w = va_arg(ap, V); volatile float b = w[0]; (void)b;
    va_end(ap); return 0;
}
int main(void) { return take(0, (V){1, 2, 3, 4, 5, 6, 7, 8}); }
'''
# Named stack slot, alignment padding and the wide vector in one call:
# the overflow pointer starts after the seventh named int and the vector
# is aligned up from there; the read must still land inside the extent.
NAMED_WIDE_READ = r'''
#include <stdarg.h>
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int take(int a, int b, int c, int d, int e, int f, int g, ...) {
    va_list ap; va_start(ap, g); V v = va_arg(ap, V); int x = va_arg(ap, int); va_end(ap);
    return !(g == 7 && v[0] == 1.0f && v[7] == 8.0f && x == 99);
}
int main(void) { return take(1, 2, 3, 4, 5, 6, 7, (V){1, 2, 3, 4, 5, 6, 7, 8}, 99); }
'''
# Nine doubles exhaust the SSE registers; the ninth spills to overflow
# bytes 0-8 and the 16-byte vector aligns up to bytes 16-32.
SSE_VECTOR_OVERFLOW_VALID = r'''
#include <stdarg.h>
typedef float v4 __attribute__((vector_size(16)));
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    for (int i = 0; i < 9; i++) { volatile double x = va_arg(ap, double); if (x != 1.0) return 1; }
    v4 v = va_arg(ap, v4); va_end(ap);
    return v[0] != 2.0f || v[3] != 5.0f;
}
int main(void) { return take(0, 1., 1., 1., 1., 1., 1., 1., 1., 1., (v4){2, 3, 4, 5}); }
'''

FAULT7 = (-4, b'', b'bennett runtime fault 7\n')
CASES = [
    ('named-stack-valid', NAMED_STACK_VALID, (0, b'', b'')),
    ('named-stack-overread', NAMED_STACK_OVERREAD, FAULT7),
    ('register-only-overread', REGISTER_ONLY_OVERREAD, FAULT7),
    ('wide-vector-ignored', WIDE_VECTOR_IGNORED, (0, b'', b'')),
    ('wide-vector-read', WIDE_VECTOR_READ, (0, b'', b'')),
    ('wide-vector-overread', WIDE_VECTOR_OVERREAD, FAULT7),
    ('named-wide-read', NAMED_WIDE_READ, (0, b'', b'')),
    ('sse-vector-overflow-valid', SSE_VECTOR_OVERFLOW_VALID, (0, b'', b'')),
]

for major in (18, 23):
    llvm = llvm_dir(major); dist = ROOT/f'dist/llvm{major}'
    layout = TMP/f'layout-{major}.ll'
    write_layout(layout, 0x1a000000, 1048576)

    def checked(name, source, level):
        bc = TMP/f'{name}-{major}-{level}.bc'; exe = TMP/f'{name}-{major}-{level}'
        run(llvm/'clang', '--target=x86_64-unknown-linux-musl', f'-{level}',
            '-nostdinc', '-isystem', dist/'musl-sysroot/include',
            '-ffreestanding', '-fno-builtin', '-emit-llvm', '-c', source, '-o', bc)
        run(ROOT/'scripts/native-check-link', major, bc, exe, layout)
        return subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)

    # O0 preserves the frontend ABI boundaries rather than folding known sums.
    result = checked('varargs', ROOT/'tests/varargs.c', 'O0')
    assert (result.returncode, result.stdout, result.stderr) == (0, b'', b''), (major, result)
    over = TMP/f'overread-{major}.c'; over.write_text(OVERREAD)
    result = checked('overread', over, 'O0')
    assert result.returncode == -4 and result.stderr == b'bennett runtime fault 7\n', (major, result)
    print(f'LLVM{major}: checked raw varargs register/overflow exact; over-read faults 7 PASS', flush=True)
    for name, source, expected in CASES:
        case = TMP/f'{name}-{major}.c'; case.write_text(source)
        result = checked(name, case, 'O0')
        actual = (result.returncode, result.stdout, result.stderr)
        assert actual == expected, (major, name, actual)
        print(f'LLVM{major}: {name} exact PASS', flush=True)
    src = TMP/f'printf-{major}.c'; src.write_text(PRINTF)
    for level in ('O0', 'O2'):
        result = checked('printf', src, level)
        assert (result.returncode, result.stdout, result.stderr) == (0, EXPECTED, b''), (major, level, result)
        print(f'LLVM{major} {level}: checked formatted integer printf stdout exact PASS', flush=True)
