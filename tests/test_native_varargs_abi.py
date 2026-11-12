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
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
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
    def check_case(self, name, source, extent, expected, flags=(), execute=True, direct_main=None):
        for major in (18, 23):
            with self.subTest(major=major, case=name):
                llvm = llvm_dir(major)
                src = TMP/f'{name}-{major}.c'; src.write_text(source)
                bc = src.with_suffix('.bc')
                run(llvm/'clang', '--target=x86_64-unknown-linux-musl', '-O0',
                    *flags, '-nostdinc', '-isystem', ROOT/f'dist/llvm{major}/musl-sysroot/include',
                    '-ffreestanding', '-fno-builtin', '-emit-llvm', '-c', src, '-o', bc)
                if direct_main is not None:
                    # Remove the frontend's caller-side vector alloca. A
                    # past-argument pointer must not alias another registered
                    # live allocation in this allocation-boundary negative.
                    text = run(llvm/'llvm-dis', bc, '-o', '-').decode()
                    text, count = re.subn(r'(define [^\n]*@main\(\)[^\n]*\{\n).*?\n}',
                                          lambda m: m[1]+direct_main+'\n}', text, flags=re.S)
                    self.assertEqual(count, 1)
                    ir = bc.with_suffix('.ll'); ir.write_text(text)
                    run(llvm/'llvm-as', ir, '-o', bc)
                layout = TMP/f'layout-{major}.ll'; write_layout(layout, 0x1a000000, 1048576)
                for script in (('native-link', 'native-check-link') if expected == OK
                               else ('native-check-link',)):
                    exe = TMP/f'{name}-{major}-{script}'
                    run(ROOT/'scripts'/script, major, bc, exe, layout)
                    if execute:
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


    def test_named_avx_operands(self):
        # Named wide vectors arrive directly in IR and consume a single SIMD
        # register. Unnamed wide vectors instead arrive as stack byval copies.
        for width, flag in ((32, '-mavx'), (64, '-mavx512f')):
            for read in (False, True):
                source = ('#include <stdarg.h>\n'
                          f'typedef float V __attribute__((vector_size({width})));\n'
                          '__attribute__((noinline)) static int take(int n, V v, ...) {\n'
                          + ('va_list ap; va_start(ap,v); int x=va_arg(ap,int); va_end(ap);\n'
                             if read else 'int x=42;\n')
                          + f'return n+(v[{width//4-1}]!=8.f)+(x!=42); }}\n'
                          + f'int main(void) {{ V v={{0}}; v[{width//4-1}]=8.f; return take(0,v,42); }}\n')
                # Linking AVX-512 is required even on hosts unable to run it.
                execute = width == 32 or 'avx512f' in Path('/proc/cpuinfo').read_text()
                self.check_case(f'named-vector-{width}-{read}', source, 0, OK,
                                flags=(flag,), execute=execute)

    def check_direct(self, name, ty, value, extent, offset=None, expected=OK):
        # Six i64s after the named int force the last i64 onto stack[0:8].
        # These raw va_list reads test the actual passed storage, independently
        # of clang's C type coercions and va_arg lowering.
        for major in (18, 23):
            with self.subTest(major=major, case=name):
                ir = TMP/f'{name}-{major}.ll'
                integer_chunks = ty in ('i65', 'i96')
                argument_offset = 16 if major == 18 and integer_chunks else 8
                actual_extent = 32 if major == 18 and integer_chunks else extent
                actual_offset = actual_extent if offset is not None else None
                loads = (f'%q = getelementptr i8, ptr %p, i64 {argument_offset}\n'
                         '%v = load volatile i64, ptr %q, align 8\n')
                if ty in ('<1 x i1>', '<1 x i8>', '<1 x i16>', '<1 x i32>'):
                    loads = loads.replace('%v = load volatile i64, ptr %q, align 8',
                                          '%byte = load volatile i8, ptr %q, align 1\n%v = zext i8 %byte to i64')
                if integer_chunks:
                    loads += (f'%hi = getelementptr i8, ptr %p, i64 {argument_offset+8}\n'
                              '%upper = load volatile i64, ptr %hi, align 8\n')
                if offset is not None:
                    loads += (f'%past = getelementptr i8, ptr %p, i64 {actual_offset}\n'
                              '%unused = load volatile i8, ptr %past, align 1\n')
                expected_value = 1 if ty == '<1 x i1>' else 7
                ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                              'target triple = "x86_64-unknown-linux-musl"\n'
                              'declare void @llvm.va_start(ptr)\n'
                              'declare void @llvm.va_end(ptr)\n'
                              'define i32 @take(i32 %n, ...) {\n'
                              '%ap = alloca [24 x i8], align 16\n'
                              'call void @llvm.va_start(ptr %ap)\n'
                              '%pp = getelementptr i8, ptr %ap, i64 8\n'
                              '%p = load ptr, ptr %pp, align 8\n'
                              '%head = load volatile i64, ptr %p, align 8\n'
                              + loads + 'call void @llvm.va_end(ptr %ap)\n'
                              '%c1 = icmp eq i64 %head, 1\n'
                              f'%c2 = icmp eq i64 %v, {expected_value}\n'
                              '%lowok = and i1 %c1, %c2\n'
                              + ('%highok = icmp eq i64 %upper, 1\n%both = and i1 %lowok, %highok\n'
                                 if integer_chunks else '%both = and i1 %lowok, true\n')
                              + '%bad = xor i1 %both, true\n'
                              '%r = zext i1 %bad to i32\n ret i32 %r\n}\n'
                              'define i32 @main() {\n'
                              '%r = call i32 (i32, ...) @take(i32 0, '
                              + ', '.join(['i64 1']*6+[f'{ty} {value}'])
                              + ')\n ret i32 %r\n}\n')
                layout = TMP/f'layout-{major}.ll'; write_layout(layout, 0x1a000000, 1048576)
                for script in ('native-link', 'native-check-link'):
                    exe = TMP/f'{name}-{major}-{script}'
                    run(ROOT/'scripts'/script, major, ir, exe, layout)
                    result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
                    # LLVM18 caller assembly inserts 8 bytes of padding
                    # before direct i65/i96, unlike LLVM23's ABI chunk layout.
                    if script == 'native-link' or expected == OK:
                        want = OK
                    else:
                        want = expected
                    self.assertEqual((result.returncode, result.stdout, result.stderr), want,
                                     (major, name, script))
                self.check_extent(major, ir, actual_extent)

    def test_integer_chunks(self):
        for ty in ('i65', 'i96'):
            self.check_direct(f'{ty}-valid', ty, '18446744073709551623', 24)
            self.check_direct(f'{ty}-past-end', ty, '18446744073709551623', 24, 24, FAULT7)

    def test_single_element_stack_vector(self):
        self.check_direct('v1i64-valid', '<1 x i64>', '<i64 7>', 16)
        self.check_direct('v1i64-past-end', '<1 x i64>', '<i64 7>', 16, 16, FAULT7)

    def test_single_element_integer_vectors(self):
        # v1i1/v1i8/v1i16/v1i32 legalize to a GP scalar, not SSE.
        for bits in (1, 8, 16, 32):
            self.check_direct(f'v1i{bits}-spill', f'<1 x i{bits}>',
                              f'<i{bits} 7>' if bits > 1 else '<i1 1>', 16,
                              16, FAULT7)



    def test_unknown_class_diagnostic_is_deferred(self):
        for major in (18, 23):
            for started in (False, True):
                with self.subTest(major=major, started=started):
                    ir = TMP/f'unknown-{started}-{major}.ll'
                    ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                                  'target triple = "x86_64-unknown-linux-musl"\n'
                                  'declare void @llvm.va_start(ptr)\n'
                                  'define i32 @take(i32 %n, ...) {\n'
                                  + ('%ap = alloca [24 x i8], align 16\n'
                                     'call void @llvm.va_start(ptr %ap)\n' if started else '')
                                  + 'ret i32 0\n}\n'
                                  'define i32 @main() {\n'
                                  '%r = call i32 (i32, ...) @take(i32 0, i256 7)\n'
                                  'ret i32 %r\n}\n')
                    layout = TMP/f'layout-{major}.ll'; write_layout(layout, 0x1a000000, 1048576)
                    exe = TMP/f'unknown-{started}-{major}'
                    run(ROOT/'scripts/native-check-link', major, ir, exe, layout)
                    result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
                    want = ((125, b'', b'bennett infrastructure failure: unclassified SysV variadic argument storage\n')
                            if started else OK)
                    self.assertEqual((result.returncode, result.stdout, result.stderr), want)



    def test_named_avx_sse_exhaustion(self):
        # LLVM's SysV vararg backend stack-passes even a named AVX vector.
        # All eight doubles still fit XMM0..7. The next read must fault at
        # overflow+0; no overflow bytes were passed after the named operand.
        source = r"""
#include <stdarg.h>
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int take(int n, V v, ...) {
 va_list ap; va_start(ap,v);
 for(int i=0;i<8;i++) { volatile double d=va_arg(ap,double); if(d!=1.)return 1; }
 READ
 va_end(ap); return n+(v[7]!=8.f);
}
int main(void) {return take(0,(V){1,2,3,4,5,6,7,8},1.,1.,1.,1.,1.,1.,1.,1.);}
"""
        vector = '<8 x float> <'+', '.join(f'float {i}.0' for i in range(1, 9))+'>'
        direct_main = ('%r = call i32 (i32, <8 x float>, ...) @take(i32 0, '
                       + vector+', '+', '.join(['double 1.0']*8)+')\nret i32 %r')
        self.check_case('named-avx-sse-valid', source.replace('READ', ''), 0, OK,
                        ('-mavx',), direct_main=direct_main)
        self.check_case('named-avx-sse-overread', source.replace('READ',
                        'volatile double d=va_arg(ap,double); (void)d;'), 0, FAULT7,
                        ('-mavx',), direct_main=direct_main)

    def test_legalized_vector_spills(self):
        # Each of these is widened to 128 bits by both backend versions,
        # including single half/bfloat vectors and sub-64-bit integer vectors.
        for ty in ('<1 x half>', '<1 x bfloat>', '<2 x i8>', '<2 x i16>',
                   '<2 x i32>', '<2 x float>', '<2 x half>', '<2 x bfloat>', '<4 x i1>'):
            for major in (18, 23):
                for past in (False, True):
                    with self.subTest(ty=ty, major=major, past=past):
                        stem = re.sub(r'[^a-zA-Z0-9]', '_', ty)+f'-{past}-{major}'
                        ir = TMP/f'spill-{stem}.ll'
                        ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                                      'target triple = "x86_64-unknown-linux-musl"\n'
                                      'declare void @llvm.va_start(ptr)\n'
                                      'define i32 @take(i32 %n, ...) {\n'
                                      '%ap = alloca [24 x i8], align 16\n'
                                      'call void @llvm.va_start(ptr %ap)\n'
                                      '%pp = getelementptr i8, ptr %ap, i64 8\n'
                                      '%p = load ptr, ptr %pp, align 8\n'
                                      f'%q = getelementptr i8, ptr %p, i64 {16 if past else 15}\n'
                                      '%v = load volatile i8, ptr %q, align 1\n'
                                      'ret i32 0\n}\n'
                                      'define i32 @main() {\n'
                                      '%r = call i32 (i32, ...) @take(i32 0, '
                                      + ', '.join(['double 1.0']*8+[f'{ty} zeroinitializer'])
                                      + ')\nret i32 %r\n}\n')
                        layout = TMP/f'layout-{major}.ll'; write_layout(layout, 0x1a000000, 1048576)
                        for script in (('native-check-link',) if past else ('native-link', 'native-check-link')):
                            exe = TMP/f'spill-{stem}-{script}'
                            run(ROOT/'scripts'/script, major, ir, exe, layout)
                            result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
                            self.assertEqual((result.returncode, result.stdout, result.stderr), FAULT7 if past else OK)
                        self.check_extent(major, ir, 16)


if __name__ == '__main__':
    unittest.main()
