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
"""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-checked-varargs'; TMP.mkdir(parents=True, exist_ok=True)

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
        return subprocess.run([exe], capture_output=True, timeout=10)

    # O0 preserves the frontend ABI boundaries rather than folding known sums.
    result = checked('varargs', ROOT/'tests/varargs.c', 'O0')
    assert (result.returncode, result.stdout, result.stderr) == (0, b'', b''), (major, result)
    over = TMP/f'overread-{major}.c'; over.write_text(OVERREAD)
    result = checked('overread', over, 'O0')
    assert result.returncode == -4 and result.stderr == b'bennett runtime fault 7\n', (major, result)
    print(f'LLVM{major}: checked raw varargs register/overflow exact; over-read faults 7 PASS', flush=True)
    src = TMP/f'printf-{major}.c'; src.write_text(PRINTF)
    for level in ('O0', 'O2'):
        result = checked('printf', src, level)
        assert (result.returncode, result.stdout, result.stderr) == (0, EXPECTED, b''), (major, level, result)
        print(f'LLVM{major} {level}: checked formatted integer printf stdout exact PASS', flush=True)
