#!/usr/bin/env python3
"""Named-vector caller legalization and exact variadic overflow boundaries."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir, run, write_layout

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'native-named-vector'
TMP.mkdir(parents=True, exist_ok=True)
RUN_HANG_GUARD_S = 180  # individual execution, same guard as common.run
OK = (0, b'', b'')
FAULT7 = (-4, b'', b'bennett runtime fault 7\n')


class NamedVector(unittest.TestCase):
    def check_case(self, major, bits, attributes, xmm_pieces, named_doubles=0, past=False, execute=True):
        name = f'v{bits}-{named_doubles}-{past}-{major}-'+hashlib.sha256(attributes.encode()).hexdigest()[:12]
        ir = TMP/f'{name}.ll'
        ty = f'<{bits//32} x float>'
        # Eight variadic doubles: each occupied XMM slot spills one double.
        # Six variadic i64s spill one, then the final marker spills another.
        extent = 16+8*min(8, named_doubles+xmm_pieces)
        named = ''.join(f',double %d{i}' for i in range(named_doubles))
        named_args = ',double 1.0'*named_doubles
        loads = (f'%past = getelementptr i8, ptr %overflow, i64 {extent}\n'
                 '%unused = load volatile i8, ptr %past, align 1\n') if past else ''
        ir.write_text('target datalayout = "e-m:e-i64:64-i128:128-f80:128-n8:16:32:64-S128"\n'
                      'target triple = "x86_64-unknown-linux-musl"\n'
                      'declare void @llvm.va_start(ptr)\n'
                      f'define i32 @take(i32 %n{named},{ty} %v,...) noinline {attributes} {{\n'
                      '%ap = alloca [24 x i8], align 16\n'
                      'call void @llvm.va_start(ptr %ap)\n'
                      '%pp = getelementptr i8, ptr %ap, i64 8\n'
                      '%overflow = load ptr, ptr %pp, align 8\n'
                      f'%tail = getelementptr i8, ptr %overflow, i64 {extent-8}\n'
                      '%value = load volatile i64, ptr %tail, align 8\n'
                      + loads + '%bad = icmp ne i64 %value, 9\n'
                      '%r = zext i1 %bad to i32\nret i32 %r\n}\n'
                      f'define i32 @main() {attributes} {{\n'
                      f'%r = call i32 (i32{",double"*named_doubles},{ty},...) @take(i32 0{named_args},{ty} zeroinitializer,'
                      + ','.join(['double 1.0']*8+['i64 1']*6+['i64 9'])
                      + ')\nret i32 %r\n}\n')
        run(llvm_dir(major)/'llc', '-O0', ir, '-o', ir.with_suffix('.s'))
        layout = TMP/f'layout-{major}.ll'
        write_layout(layout, 0x1a000000, 1048576)
        for script in (('native-check-link',) if past else ('native-link', 'native-check-link')):
            exe = TMP/f'{name}-{script}'
            run(ROOT/'scripts'/script, major, ir, exe, layout)
            if execute:
                result = subprocess.run([exe], capture_output=True, timeout=RUN_HANG_GUARD_S)
                self.assertEqual((result.returncode, result.stdout, result.stderr), FAULT7 if past else OK,
                                 (major, bits, attributes, named_doubles, script, past))
        checked = ir.with_suffix('.bc')
        run(ROOT/'scripts/native-check', major, ir, checked)
        text = run(llvm_dir(major)/'llvm-dis', checked, '-o', '-').decode()
        self.assertEqual(re.findall(r'__bennett_rt_native_va_push\(i64 (\d+)\)', text), [str(extent)])

    def test_default_target_valid_and_first_past_end(self):
        # The reviewer controls: no AVX needed; 2/4 XMM pieces displace doubles.
        for major in (18, 23):
            for bits in (256, 512):
                for named_doubles in (0, 7):
                    for past in (False, True):
                        with self.subTest(major=major, bits=bits, named_doubles=named_doubles, past=past):
                            self.check_case(major, bits, '', bits//128, named_doubles, past)

    def test_avx_target_valid_and_first_past_end(self):
        # Legalized 256-bit pieces use memory in a variadic calling convention,
        # including a 512-bit operand split into two such pieces.
        attributes = '"target-features"="+avx"'
        for major in (18, 23):
            for bits in (256, 512):
                for past in (False, True):
                    with self.subTest(major=major, bits=bits, past=past):
                        self.check_case(major, bits, attributes, 0, past=past)

    def test_avx512_target_link_and_layout(self):
        attributes = '"target-features"="+avx,+avx512f"'
        execute = 'avx512f' in Path('/proc/cpuinfo').read_text()
        for major in (18, 23):
            for past in (False, True):
                with self.subTest(major=major, past=past, execute=execute):
                    self.check_case(major, 512, attributes, 0, past=past, execute=execute)

    def test_cpu_defaults_and_feature_overrides(self):
        for major in (18, 23):
            for attributes, pieces in (('"target-cpu"="haswell"', 0),
                                       ('"target-cpu"="haswell" "target-features"="-avx,-avx2"', 2),
                                       ('"target-features"="+avx2"', 0)):
                with self.subTest(major=major, attributes=attributes):
                    self.check_case(major, 256, attributes, pieces)


if __name__ == '__main__':
    unittest.main()
