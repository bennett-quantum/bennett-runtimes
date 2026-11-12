#!/usr/bin/env python3
"""Cross-check overflow extents with independent LLVM caller assembly.

System V AMD64 psABI §§3.2.3 and 3.5.7 define register banks, stack
alignment and the variadic suffix. See the source specification at
https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex.
Direct LLVM vector legalization also matters: inspect the matching backend,
not C spelling or the native-check classifier, for the actual passed area.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir, pinned_sibling, run, write_layout
from test_native_varargs_abi import GP_VALID, NAMED_VECTOR, SPILLED

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-varargs-oracle'
TMP.mkdir(parents=True, exist_ok=True)

NAMED_STACK = r'''
#include <stdarg.h>
__attribute__((noinline)) static int take(int a,int b,int c,int d,int e,int f,int g,...) {
    va_list ap; va_start(ap,g);
    volatile int x=va_arg(ap,int); volatile int y=va_arg(ap,int);
    (void)x; (void)y; va_end(ap); return 0;
}
int main(void) { return take(1,2,3,4,5,6,7,8); }
'''
ALIGNED_IGNORED = r'''
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int ignore(int n,...) { return n; }
int main(void) { return ignore(0,(V){1,2,3,4,5,6,7,8}); }
'''
NAMED_AVX = r'''
#include <stdarg.h>
typedef float V __attribute__((vector_size(32)));
__attribute__((noinline)) static int take(int n,V v,...) {
    va_list ap; va_start(ap,v); int x=va_arg(ap,int); va_end(ap);
    return n+(v[7]!=8.f)+(x!=42);
}
int main(void) { return take(0,(V){1,2,3,4,5,6,7,8},42); }
'''


class VariadicCallerOracle(unittest.TestCase):
    def compile_c(self, major, name, source, flags=(), level='O0'):
        src = TMP/f'{name}-{major}.c'
        src.write_text(source)
        bc = src.with_suffix('.bc')
        run(llvm_dir(major)/'clang', '--target=x86_64-unknown-linux-musl', '-'+level,
            '-nostdinc', '-isystem', ROOT/f'dist/llvm{major}/musl-sysroot/include',
            '-ffreestanding', '-fno-builtin', *flags, '-emit-llvm', '-c', src, '-o', bc)
        return bc

    def check_caller(self, major, bc, extent, patterns):
        # llc sees the uninstrumented input; none of its evidence comes from
        # the extent under test. Retain the complete assembly for review.
        asm = run(llvm_dir(major)/'llc', '-O0', bc, '-o', '-').decode()
        bc.with_suffix('.s').write_text(asm)
        caller = asm.split('main:', 1)[1].split('.Lfunc_end', 1)[0]
        for pattern in patterns:
            self.assertRegex(caller, pattern, (major, bc, caller))
        checked = bc.with_name(bc.stem+'-checked.bc')
        run(ROOT/'scripts/native-check', major, bc, checked)
        ir = run(llvm_dir(major)/'llvm-dis', checked, '-o', '-').decode()
        self.assertEqual(re.findall(r'__bennett_rt_native_va_push\(i64 (\d+)\)', ir),
                         [str(extent)], (major, bc))

    def test_named_stack_suffix(self):
        # Six GP regs; named g at stack+0, variadic 8 at stack+8.
        # va_start skips g: only 8 bytes, never the 16-byte caller frame.
        for major in (18, 23):
            with self.subTest(major=major):
                bc = self.compile_c(major, 'named-stack', NAMED_STACK)
                self.check_caller(major, bc, 8,
                                  (r'movl\s+\$7, \(%rsp\)', r'movl\s+\$8, 8\(%rsp\)'))

    def test_aligned_byval_without_va_start(self):
        # MEMORY, align32, stack[0:32]. A callee need not execute va_start.
        for major in (18, 23):
            with self.subTest(major=major):
                bc = self.compile_c(major, 'aligned-ignored', ALIGNED_IGNORED)
                ir = run(llvm_dir(major)/'llvm-dis', bc, '-o', '-').decode()
                self.assertIn('byval(<8 x float>) align 32', ir)
                self.assertNotIn('@llvm.va_start', ir)
                self.check_caller(major, bc, 32,
                                  (r'andq\s+\$-32, %rsp',
                                   r'movq\s+%rcx, \(%rsp\)',
                                   r'movq\s+%rcx, 8\(%rsp\)',
                                   r'movq\s+%rcx, 16\(%rsp\)',
                                   r'movq\s+%rax, 24\(%rsp\)'))

    def test_fp128_register_spill_and_named(self):
        # q in XMM0 leaves only int6 on stack: end8. With eight doubles
        # q spills at0:16. Named q+eight doubles spills named h at0:8;
        # the vector aligns to16:32, leaving a 32-8=24-byte suffix.
        cases = (
            ('fp128-register', GP_VALID, 8,
             (r'movaps\s+[^\n]*, %xmm0', r'movb\s+\$1, %al', r'movl\s+\$6, \(%rax\)')),
            ('fp128-spilled', SPILLED, 16,
             (r'movq\s+%rsp, %rax', r'movaps\s+%xmm\d+, \(%rax\)', r'movb\s+\$8, %al')),
            ('fp128-named', NAMED_VECTOR, 24,
             (r'movq\s+%rsp, %rax', r'movabsq\s+\$4620693217682128896, %rcx',
              r'movq\s+%rcx, \(%rax\)', r'movaps\s+%xmm\d+, 16\(%rax\)',
              r'movb\s+\$8, %al')),
        )
        for major in (18, 23):
            for name, source, extent, patterns in cases:
                with self.subTest(major=major, case=name):
                    self.check_caller(major, self.compile_c(major, name, source), extent, patterns)

    def test_named_direct_avx(self):
        # The frontend emits a direct named vector. LLVM's variadic call
        # backend places that legalized 32-byte piece on stack, despite
        # psABI permitting named SIMD registers. It is all named storage;
        # variadic int42 uses RSI, so the true overflow suffix is empty.
        for major in (18, 23):
            with self.subTest(major=major):
                bc = self.compile_c(major, 'named-avx', NAMED_AVX, ('-mavx',))
                ir = run(llvm_dir(major)/'llvm-dis', bc, '-o', '-').decode()
                self.assertRegex(ir, r'call i32 \(i32, <8 x float>, \.\.\.\)')
                self.check_caller(major, bc, 0,
                                  (r'vmovups\s+%ymm\d+, \(%rsp\)', r'movl\s+\$42, %esi'))

    def test_single_i64_vector_stack(self):
        # Six i64s after named i32: five use GP registers, one at0:8.
        # <1 x i64> legalizes to stack-passed MMX at8:16, not XMM.
        for major in (18, 23):
            with self.subTest(major=major):
                ir = TMP/f'single-i64-vector-{major}.ll'
                ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                              'target triple = "x86_64-unknown-linux-musl"\n'
                              'declare void @take(i32, ...)\n'
                              'define i32 @main() {\n'
                              'call void (i32, ...) @take(i32 0, '
                              + ', '.join(['i64 1']*6+['<1 x i64> <i64 7>'])
                              + ')\nret i32 0\n}\n')
                self.check_caller(major, ir, 16,
                                  (r'movq\s+%rsp, %rax',
                                   r'movq\s+\$1, \(%rax\)', r'movq\s+\$7, 8\(%rax\)'))

    def test_rung_three_exact_bytes(self):
        hello = (pinned_sibling('bennett-conformance')/'ladder/rung-3/fixtures/hello.c').read_text()
        # String precision leaves musl's four bytes; width adds two spaces.
        # Integer width includes the minus sign. The final buffered printf
        # has no newline and must be flushed when main returns.
        printf = r'''
#include <stdio.h>
int main(void) {
    printf("[%6.4s] %06d %u %.4x %c\n", "musl", -42, 37u, 0x2au, '!');
    printf("tail:%03d", 7);
    return 0;
}
'''
        for major in (18, 23):
            layout = TMP/f'layout-{major}.ll'
            write_layout(layout, 0x1a000000, 1048576)
            for level in ('O0', 'O2'):
                for name, source, expected in (
                        ('hello', hello, b'hello, world\n'),
                        ('printf', printf, b'[  musl] -00042 37 002a !\ntail:007')):
                    bc = self.compile_c(major, name+'-'+level, source, level=level)
                    for script in ('native-link', 'native-check-link'):
                        with self.subTest(major=major, level=level, case=name, script=script):
                            exe = bc.with_name(bc.stem+'-'+script)
                            run(ROOT/'scripts'/script, major, bc, exe, layout)
                            # Same individual-command hang guard as common.run.
                            result = subprocess.run([exe], capture_output=True, timeout=180)
                            self.assertEqual((result.returncode, result.stdout, result.stderr),
                                             (0, expected, b''))


if __name__ == '__main__':
    unittest.main()
