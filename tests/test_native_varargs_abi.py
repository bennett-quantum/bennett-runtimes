#!/usr/bin/env python3
"""SysV fp128/SSE overflow extents, independently derived from ABI 3.2.3.

Keep C frontend optimisation at O0 so named and unnamed operands survive.
Check plain-native valid controls, checked results and the published extent.
The register save frame is a live allocation, not missing-argument detection;
negative cases deliberately read beyond the actual overflow allocation.
"""
import os
from pathlib import Path
import re
import subprocess
import unittest

from common import llvm_dir, run, write_layout

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-varargs-abi'
TMP.mkdir(parents=True, exist_ok=True)
# Same per-executable hang guard as common.run; verdicts use exact bytes/status.
RUN_HANG_GUARD_S = 180
OK = (0, b'', b'')
FAULT7 = (-4, b'', b'bennett runtime fault 7\n')

GP_VALID = r'''
#include <stdarg.h>
#include <string.h>
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    __float128 q = va_arg(ap, __float128);
    unsigned __int128 bits; memcpy(&bits, &q, sizeof bits);
    if (bits != (unsigned __int128)0x4000 << 112) return 1;
    for (int i = 1; i <= 6; ++i) if (va_arg(ap, int) != i) return 2;
    va_end(ap); return 0;
}
int main(void) { return take(0, 2.0Q, 1, 2, 3, 4, 5, 6); }
'''
GP_OVERREAD = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    volatile __float128 q = va_arg(ap, __float128); (void)q;
    for (int i = 0; i < READS; ++i) { volatile int x = va_arg(ap, int); (void)x; }
    va_end(ap); return 0;
}
int main(void) { return take(0, 2.0Q, 1, 2, 3, 4, 5, 6); }
'''
NAMED_VECTOR = r'''
#include <stdarg.h>
#include <string.h>
typedef float v4 __attribute__((vector_size(16)));
__attribute__((noinline)) static int take(int n, __float128 q,
    double a, double b, double c, double d, double e, double f, double g, double h, ...) {
    unsigned __int128 bits; memcpy(&bits, &q, sizeof bits);
    if (bits != (unsigned __int128)0x4000 << 112) return 1;
    if (a != 1. || h != 8.) return 2;
    va_list ap; va_start(ap, h); v4 v = va_arg(ap, v4); va_end(ap);
    return !(v[0] == 2.f && v[1] == 3.f && v[2] == 4.f && v[3] == 5.f);
}
int main(void) { return take(0, 2.0Q, 1., 2., 3., 4., 5., 6., 7., 8., (v4){2,3,4,5}); }
'''
SPILLED = r'''
#include <stdarg.h>
#include <string.h>
__attribute__((noinline)) static int take(int n, ...) {
    va_list ap; va_start(ap, n);
    for (int i = 1; i <= 8; ++i) if (va_arg(ap, double) != (double)i) return 1;
    __float128 q = va_arg(ap, __float128);
    unsigned __int128 bits; memcpy(&bits, &q, sizeof bits);
    va_end(ap); return bits != (unsigned __int128)0x4000 << 112;
}
int main(void) { return take(0, 1., 2., 3., 4., 5., 6., 7., 8., 2.0Q); }
'''


class VariadicABI(unittest.TestCase):
    def check_case(self, name, source, extent, expected):
        for major in (18, 23):
            with self.subTest(major=major, case=name):
                llvm = llvm_dir(major)
                src = TMP/f'{name}-{major}.c'; src.write_text(source)
                bc = src.with_suffix('.bc')
                run(llvm/'clang', '--target=x86_64-unknown-linux-musl', '-O0',
                    '-nostdinc', '-isystem', ROOT/f'dist/llvm{major}/musl-sysroot/include',
                    '-ffreestanding', '-fno-builtin', '-emit-llvm', '-c', src, '-o', bc)
                layout = TMP/f'layout-{major}.ll'; write_layout(layout, 0x1a000000, 1048576)
                for script in (('native-link', 'native-check-link') if expected == OK
                               else ('native-check-link',)):
                    exe = TMP/f'{name}-{major}-{script}'
                    run(ROOT/'scripts'/script, major, bc, exe, layout)
                    result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
                    self.assertEqual((result.returncode, result.stdout, result.stderr), expected,
                                     (major, name, script))
                self.check_extent(major, bc, extent)

    def check_extent(self, major, bc, extent):
        checked = bc.with_name(bc.stem+'-checked.bc')
        run(ROOT/'scripts/native-check', major, bc, checked)
        ir = run(llvm_dir(major)/'llvm-dis', checked, '-o', '-').decode()
        self.assertEqual(re.findall(r'call void @__bennett_rt_native_va_push\(i64 (\d+)\)', ir),
                         [str(extent)], (major, bc))

    def test_fp128_register_gp_valid(self):
        # q occupies XMM0; five ints occupy remaining GP registers, one spills.
        self.check_case('fp128-gp-valid', GP_VALID, 8, OK)

    def test_fp128_register_first_overread(self):
        self.check_case('fp128-gp-overread', GP_OVERREAD.replace('READS', '7'), 8, FAULT7)

    def test_fp128_register_second_overread(self):
        self.check_case('fp128-gp-overread2', GP_OVERREAD.replace('READS', '8'), 8, FAULT7)

    def test_named_fp128_vector(self):
        # q + seven doubles fill XMM0..7. Named h occupies stack[0:8];
        # vector occupies stack[16:32], so overflow at +8 has a 24-byte suffix.
        self.check_case('named-fp128-vector', NAMED_VECTOR, 24, OK)

    def test_fp128_spilled(self):
        self.check_case('fp128-spilled', SPILLED, 16, OK)

    def test_fp80_memory_control(self):
        # x87 arguments use 16 stack bytes even with all XMM registers free.
        source = GP_OVERREAD.replace('__float128', 'long double').replace('2.0Q', '2.0L')
        self.check_case('fp80-gp-overread', source.replace('READS', '7'), 24, FAULT7)

    def test_small_float_sse_slots(self):
        # ABI 3.1.2 gives bfloat the same passing rules as half. These direct
        # IR probes isolate classification from optional conversion libcalls.
        for ty, value in (('half', '0xH3E00'), ('bfloat', '0xR3FC0')):
            for doubles, extent in ((7, 8), (8, 16)):
                for major in (18, 23):
                    with self.subTest(major=major, ty=ty, doubles=doubles):
                        args = ', '.join(['double 1.0']*doubles+[f'{ty} {value}', 'double 2.0'])
                        ir = TMP/f'{ty}-{doubles}-{major}.ll'
                        ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                                      'target triple = "x86_64-unknown-linux-musl"\n'
                                      'declare void @take(i32, ...)\n'
                                      'define void @caller() {\n'
                                      f'  call void (i32, ...) @take(i32 0, {args})\n'
                                      '  ret void\n}\n')
                        self.check_extent(major, ir, extent)


if __name__ == '__main__':
    unittest.main()
