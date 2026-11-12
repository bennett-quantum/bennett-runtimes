#!/usr/bin/env python3
"""Native caller boundaries plus separately labelled synthetic DataLayout units."""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir, run, write_layout

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-varargs-layout'
TMP.mkdir(parents=True, exist_ok=True)
LAYOUT = 'e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128'
# Individual executable/tool hang guard, shared with common.run.
RUN_HANG_GUARD_S = 180
OK = (0, b'', b'')
FAULT7 = (-4, b'', b'bennett runtime fault 7\n')


class VariadicLayout(unittest.TestCase):
    def check_extents(self, major, name, calls, expected, layout=LAYOUT, globals=''):
        ir = TMP/f'{name}-{major}.ll'
        ir.write_text(f'target datalayout = "{layout}"\n'
                      'target triple = "x86_64-unknown-linux-musl"\n'
                      + globals + '\ndeclare void @take(i32, ...)\n'
                      'define void @caller() {\n'
                      + ''.join(f'call void (i32, ...) @take(i32 0, {args})\n' for args in calls)
                      + 'ret void\n}\n')
        checked = ir.with_suffix('.bc')
        run(ROOT/'scripts/native-check', major, ir, checked)
        text = run(llvm_dir(major)/'llvm-dis', checked, '-o', '-').decode()
        actual = [int(n) for n in re.findall(r'call void @__bennett_rt_native_va_push\(i64 (\d+)\)', text)]
        self.assertEqual(actual, expected, (major, name))

    def test_every_accepted_integer_width(self):
        # For prefixes of 3/4/5/6 i64s after the named i32, the last
        # trailing i64 exposes register reservation as well as stack extent.
        # These tables are hand-derived from the independent caller receipts.
        for major in (18, 23):
            for integer_alignment in (64, 128, 256):
                with self.subTest(major=major, integer_alignment=integer_alignment):
                    calls, expected = [], []
                    for bits in range(1, 129):
                        if bits <= 64:
                            ends = (0, 8, 16, 24)
                        elif major == 18:
                            ends = (8, 24, 24, 40)
                        elif bits == 128:
                            ends = (8, 16, 24, 40)
                        else:
                            ends = (8, 16, 24, 32)
                        for count, end in zip((3, 4, 5, 6), ends):
                            calls.append(', '.join(['i64 1']*count+[f'i{bits} 1', 'i64 9']))
                            expected.append(end)
                    self.check_extents(major, f'integers-align{integer_alignment}', calls, expected,
                                       LAYOUT.replace('i128:128', f'i128:{integer_alignment}'))

    def check_boundary(self, major, bits, count, words, past):
        name = f'i{bits}-gp{count}-past{past}-{major}'
        ir = TMP/f'{name}.ll'
        loads, checks = [], []
        for index, (offset, value) in enumerate(words):
            loads.append(f'%p{index} = getelementptr i8, ptr %overflow, i64 {offset}\n'
                         f'%v{index} = load volatile i64, ptr %p{index}, align 8\n'
                         f'%c{index} = icmp eq i64 %v{index}, {value}\n')
            checks.append(f'%ok{index} = and i1 {"true" if index == 0 else "%ok"+str(index-1)}, %c{index}\n')
        end = words[-1][0]+8
        if past:
            loads.append(f'%past = getelementptr i8, ptr %overflow, i64 {end}\n'
                         '%unused = load volatile i8, ptr %past, align 1\n')
        ir.write_text(f'target datalayout = "{LAYOUT}"\n'
                      'target triple = "x86_64-unknown-linux-musl"\n'
                      'declare void @llvm.va_start(ptr)\n'
                      'define i32 @take(i32 %n, ...) {\n'
                      '%ap = alloca [24 x i8], align 16\n'
                      'call void @llvm.va_start(ptr %ap)\n'
                      '%pp = getelementptr i8, ptr %ap, i64 8\n'
                      '%overflow = load ptr, ptr %pp, align 8\n'
                      + ''.join(loads+checks)
                      + f'%bad = xor i1 %ok{len(words)-1}, true\n'
                      '%result = zext i1 %bad to i32\nret i32 %result\n}\n'
                      'define i32 @main() {\n'
                      '%result = call i32 (i32, ...) @take(i32 0, '
                      + ', '.join(['i64 1']*count+[f'i{bits} 18446744073709551623', 'i64 9'])
                      + ')\nret i32 %result\n}\n')
        layout = TMP/f'layout-{major}.ll'
        write_layout(layout, 0x1a000000, 1048576)
        for script in (('native-check-link',) if past else ('native-link', 'native-check-link')):
            exe = TMP/f'{name}-{script}'
            run(ROOT/'scripts'/script, major, ir, exe, layout)
            result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
            self.assertEqual((result.returncode, result.stdout, result.stderr), FAULT7 if past else OK,
                             (major, name, script))
        # The input has one call to the variadic take.
        checked = ir.with_suffix('.bc')
        run(ROOT/'scripts/native-check', major, ir, checked)
        text = run(llvm_dir(major)/'llvm-dis', checked, '-o', '-').decode()
        self.assertEqual(re.findall(r'call void @__bennett_rt_native_va_push\(i64 (\d+)\)', text), [str(end)])

    def test_integer_values_and_first_byte_past_end(self):
        for major in (18, 23):
            for bits in (65, 96, 127, 128):
                for count in (4, 6):
                    if count == 4:
                        words = ((0, 7), (8, 1), (16, 9)) if major == 18 else (
                            ((0, 7), (8, 1)) if bits == 128 else ((0, 1), (8, 9)))
                    elif major == 18 or bits == 128:
                        words = ((0, 1), (16, 7), (24, 1), (32, 9))
                    else:
                        words = ((0, 1), (8, 7), (16, 1), (24, 9))
                    for past in (False, True):
                        with self.subTest(major=major, bits=bits, count=count, past=past):
                            self.check_boundary(major, bits, count, words, past)

    def test_scalar_float_slots_override_object_alignment(self):
        for major in (18, 23):
            for alignment in (64, 128, 256):
                with self.subTest(major=major, alignment=alignment):
                    types = ('half', 'bfloat', 'float', 'double', 'fp128', 'x86_fp80',
                             '<1 x half>', '<1 x bfloat>', '<1 x float>', '<1 x double>',
                             '<1 x fp128>', '<1 x x86_fp80>')
                    # One GP spill precedes each scalar/vector; the last i64
                    # exposes its exact end independently of frame padding.
                    ends = [24, 24, 24, 24, 40, 40, 40, 40, 24, 24, 40, 40]
                    calls = [', '.join(['i64 1']*6+['double 1.0']*8+
                                      [ty+' zeroinitializer', 'i64 9']) for ty in types]
                    layout = LAYOUT.replace('f80:128', f'f80:{alignment}')
                    layout += ''.join(f'-{ty}:{alignment}' for ty in ('f16', 'f32', 'f64', 'f128'))
                    self.check_extents(major, f'float-align{alignment}', calls, ends, layout)

    def test_synthetic_byval_datalayout_unit(self):
        # These synthetic layouts check DataLayout arithmetic only. LLVM codegen
        # replaces them with its target layout, so they are not native evidence.
        for major in (18, 23):
            for i64_alignment, size in ((64, 24), (32, 16)):
                for alignment, start in ((8, 8), (32, 32)):
                    with self.subTest(major=major, i64_alignment=i64_alignment, alignment=alignment):
                        args = ', '.join(['i64 1']*6+[f'ptr byval({{i8, i64, i8}}) align {alignment} @object'])
                        self.check_extents(major, f'byval-{i64_alignment}-{alignment}', [args], [start+size],
                                           LAYOUT.replace('i64:64', f'i64:{i64_alignment}'),
                                           '@object = global {i8, i64, i8} zeroinitializer\n')


    def test_native_byval_values_and_boundary(self):
        for major in (18, 23):
            for alignment, start in ((None, 8), (1, 8), (8, 8), (16, 16), (32, 32), (64, 64)):
                for past in (False, True):
                    with self.subTest(major=major, alignment=alignment, past=past):
                        name = f'native-byval-{alignment}-{past}-{major}'
                        ir = TMP/f'{name}.ll'
                        # Compatible AMD64 layout: field offsets0/8/16,
                        # allocation24, one preceding GP spill and final marker.
                        end = start+24+8
                        attr = f'align {alignment} ' if alignment else ''
                        ir.write_text(f'target datalayout = "{LAYOUT}"\n'
                                      'target triple = "x86_64-unknown-linux-musl"\n'
                                      '@object = global {i8,i64,i8} {i8 3,i64 7,i8 4}, align 16\n'
                                      'declare void @llvm.va_start(ptr)\n'
                                      'define i32 @take(i32 %n,...) noinline {\n'
                                      '%ap = alloca [24 x i8], align 16\n'
                                      'call void @llvm.va_start(ptr %ap)\n'
                                      '%pp = getelementptr i8,ptr %ap,i64 8\n'
                                      '%p = load ptr,ptr %pp,align 8\n'
                                      f'%body = getelementptr i8,ptr %p,i64 {start+8}\n'
                                      '%value = load volatile i64,ptr %body,align 8\n'
                                      f'%tail = getelementptr i8,ptr %p,i64 {end-8}\n'
                                      '%marker = load volatile i64,ptr %tail,align 8\n'
                                      + (f'%past = getelementptr i8,ptr %p,i64 {end}\n'
                                         '%unused = load volatile i8,ptr %past,align 1\n' if past else '')
                                      + '%c1 = icmp eq i64 %value,7\n%c2 = icmp eq i64 %marker,9\n'
                                      '%ok = and i1 %c1,%c2\n%bad = xor i1 %ok,true\n'
                                      '%r = zext i1 %bad to i32\nret i32 %r\n}\n'
                                      'define i32 @main() {\n'
                                      '%r = call i32 (i32,...) @take(i32 0,'
                                      + ','.join(['i64 1']*6+[f'ptr byval({{i8,i64,i8}}) {attr}@object','i64 9'])
                                      + ')\nret i32 %r\n}\n')
                        asm = run(llvm_dir(major)/'llc','-O0',ir,'-o','-').decode()
                        ir.with_suffix('.s').write_text(asm)
                        # Distinct final9 is a separate independent assembly oracle.
                        marker = re.findall(r'movq\s+\$9, (?:(\d+))?\(%(?:rsp|rax)\)',asm)
                        self.assertEqual(marker,[str(end-8)])
                        layout = TMP/f'layout-{major}.ll'
                        write_layout(layout,0x1a000000,1048576)
                        for script in (('native-check-link',) if past else ('native-link','native-check-link')):
                            exe = TMP/f'{name}-{script}'
                            run(ROOT/'scripts'/script,major,ir,exe,layout)
                            result = subprocess.run([exe],capture_output=True,timeout=RUN_HANG_GUARD_S)
                            self.assertEqual((result.returncode,result.stdout,result.stderr),FAULT7 if past else OK)
                        checked = ir.with_suffix('.bc')
                        run(ROOT/'scripts/native-check',major,ir,checked)
                        text = run(llvm_dir(major)/'llvm-dis',checked,'-o','-').decode()
                        self.assertEqual(re.findall(r'__bennett_rt_native_va_push\(i64 (\d+)\)',text),[str(end)])

if __name__ == '__main__':
    unittest.main()
