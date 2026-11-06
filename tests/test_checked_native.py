#!/usr/bin/env python3
"""Materialised outcomes, independent of original-input sanitizer coverage."""
from pathlib import Path
import hashlib
import os
import resource
import signal
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'checked-native'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))

CASES={
    'valid': ('%q = getelementptr i8, ptr %p, i64 2\n store i8 19, ptr %q\n %r = load i8, ptr %q',0),
    'address-model': ('%q = call ptr @__bennett_nz_alloc(i64 3)\n %r = getelementptr i8, ptr %p, i64 3\n store i8 19, ptr %r',0),
    'oob-load': ('%q = getelementptr i8, ptr %p, i64 3\n %r = load i8, ptr %q',7),
    'oob-store': ('%q = getelementptr i8, ptr %p, i64 3\n store i8 19, ptr %q',7),
    'spanning': ('%q = call ptr @__bennett_nz_alloc(i64 3)\n %r = load i32, ptr %p, align 1',7),
    'uaf-load': ('call void @__bennett_nz_free(ptr %p)\n %r = load i8, ptr %p',7),
    'uaf-store': ('call void @__bennett_nz_free(ptr %p)\n store i8 19, ptr %p',7),
    'double-free': ('call void @__bennett_nz_free(ptr %p)\n call void @__bennett_nz_free(ptr %p)',7),
    'invalid-free': ('%q = getelementptr i8, ptr %p, i64 1\n call void @__bennett_nz_free(ptr %q)',7),
    'zero': ('%q = call ptr @__bennett_nz_alloc(i64 0)\n %r = load i8, ptr %q',7),
    'null': ('%r = load i8, ptr null',7),
    'wrap': ('%r = load i128, ptr inttoptr (i64 -1 to ptr), align 1',7),
    'global-valid': ('%r = load i16, ptr @bytes, align 1',0),
    'global-oob': ('%r = load i32, ptr @bytes, align 1',7),
    'stack-valid': ('%q = alloca i16\n store i16 19, ptr %q\n %r = load i16, ptr %q',0),
    'stack-oob': ('%q = alloca i16\n %r = load i32, ptr %q, align 1',7),
    'stack-after-return': ('%q = call ptr @dead()\n %r = load i16, ptr %q',7),
    'copy-valid': ('call void @llvm.memcpy.p0.p0.i64(ptr %p, ptr @bytes, i64 2, i1 false)',0),
    'copy-oob': ('call void @llvm.memcpy.p0.p0.i64(ptr %p, ptr @bytes, i64 3, i1 false)',7),
    'move-oob': ('call void @__bennett_rt_memmove(ptr %p, ptr @bytes, i64 3)',7),
    'set-oob': ('call void @__bennett_rt_memset(ptr %p, i8 0, i64 4)',7),
    'empty-ranges': ('call void @__bennett_rt_memmove(ptr null, ptr null, i64 0)',0),
    'writev-prefix': ('%r = call i64 @__bennett_rt_syscall(i64 20, i64 1, i64 ptrtoint (ptr @bad_iov to i64), i64 2, i64 0, i64 0, i64 0)',7),
    'null-iov': ('%r = call i64 @__bennett_rt_syscall(i64 20, i64 1, i64 0, i64 2, i64 0, i64 0, i64 0)\n %q = icmp ne i64 %r, -14\n %status = zext i1 %q to i32\n ret i32 %status',0),
    'stderr-console': ('%r = call i64 @__bennett_rt_syscall(i64 1, i64 2, i64 ptrtoint (ptr @bytes to i64), i64 2, i64 0, i64 0, i64 0)',0),
    'iov-overflow': ('%r = call i64 @__bennett_rt_syscall(i64 20, i64 1, i64 ptrtoint (ptr @overflow_iov to i64), i64 2, i64 0, i64 0, i64 0)\n %q = icmp ne i64 %r, -22\n %overflow_status = zext i1 %q to i32\n ret i32 %overflow_status',0),
    'udiv-zero': ('%r = udiv i64 29, 0',5),
    'urem-zero': ('%r = urem i64 29, 0',5),
    'sdiv-zero': ('%r = sdiv i64 -29, 0',5),
    'srem-zero': ('%r = srem i64 -29, 0',5),
    'sdiv-overflow': ('%r = sdiv i64 -9223372036854775808, -1',6),
    'srem-overflow': ('%r = srem i64 -9223372036854775808, -1',6),
    'unreachable': ('unreachable',4),
    'trap': ('call void @llvm.trap()\n unreachable',4),
    'explicit': ('call void @__bennett_nz_fault(i64 18446744073709551615)\n unreachable',18446744073709551615),
    'explicit-zero': ('call void @__bennett_nz_fault(i64 0)\n unreachable',4),
    'unused-allocation': ('%r = call ptr @malloc(i64 1)',24),
    'libcall-body': ('%r = call i64 @strlen(ptr @shortstr)',23),
}


for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    # Each case is already materialised IR. Faulting operations remain present
    # even when original-source optimisation would have erased them.
    ir=['target triple = "x86_64-unknown-linux-musl"',
        '@bytes = global [2 x i8] [i8 23, i8 41], align 1',
        '@shortstr = constant [4 x i8] c"abc\\00", align 8',
        '@bad_iov = global [2 x { ptr, i64 }] [{ ptr, i64 } { ptr @bytes, i64 2 }, { ptr, i64 } { ptr @bytes, i64 3 }], align 8',
        '@overflow_iov = global [2 x { ptr, i64 }] [{ ptr, i64 } { ptr null, i64 9223372036854775807 }, { ptr, i64 } { ptr @bytes, i64 1 }], align 8',
        'declare ptr @__bennett_nz_alloc(i64)', 'declare void @__bennett_nz_free(ptr)',
        'declare void @__bennett_nz_fault(i64)',
        'declare void @__bennett_rt_memmove(ptr, ptr, i64)',
        'declare void @__bennett_rt_memset(ptr, i8, i64)',
        'declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)',
        'declare void @llvm.trap()',
        'declare i64 @__bennett_rt_syscall(i64, i64, i64, i64, i64, i64, i64)',
        'define ptr @dead() noinline { %slot = alloca i16\n ret ptr %slot }',
        'define i64 @strlen(ptr %s) { call void @__bennett_nz_fault(i64 23)\n unreachable }',
        'define ptr @malloc(i64 %n) allockind("alloc") allocsize(0) { call void @__bennett_nz_fault(i64 24)\n unreachable }',
        'define i32 @main(i32 %argc, ptr %argv) {',
        '%slot = getelementptr ptr, ptr %argv, i64 1', '%arg = load ptr, ptr %slot',
        '%which = load i8, ptr %arg', '%p = call ptr @__bennett_nz_alloc(i64 3)',
        'switch i8 %which, label %bad [ '+ ' '.join(f'i8 {i+33}, label %case{i}' for i in range(len(CASES)))+' ]']
    for i,(label,(body,code)) in enumerate(CASES.items()):
        # Local names are unique because all cases belong to one function.
        import re
        body=re.sub(r'%([qr])\b',lambda m:f'%{m[1]}{i}',body)
        ir.extend([f'case{i}:',body])
        if not body.endswith('unreachable') and '\n ret i32' not in body: ir.append('ret i32 0')
    ir.extend(['bad:', 'ret i32 99', '}'])
    source=TMP/f'materialised-{major}.ll'; source.write_text('\n'.join(ir)+'\n')
    original=hashlib.sha256(source.read_bytes()).hexdigest()
    fail=subprocess.run([ROOT/'scripts/native-check',str(major),source,source],capture_output=True)
    assert fail.returncode and b'separate output' in fail.stderr,fail
    config=TMP/f'layout-{major}.ll'
    write_layout(config, 469762048, 4096)
    exe=TMP/f'checked-{major}'
    run(ROOT/'scripts/native-check-link',major,source,exe,config)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==original,'adapter modified materialised input'
    for i,(label,(_,code)) in enumerate(CASES.items()):
        result=subprocess.run([exe,chr(i+33)],capture_output=True,timeout=10)
        if code:
            assert result.returncode==-signal.SIGILL and result.stderr==f'bennett runtime fault {code}\n'.encode(),(major,label,result)
        else:
            assert result.returncode==0 and not result.stderr,(major,label,result)
        expected=b'\x17)' if label in ('writev-prefix','stderr-console') else b''
        assert result.stdout==expected,(major,label,result)
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    assert b'NEEDED' not in run('readelf','-d',exe)
    # Exhaust all small offset/length pairs around exact heap records, a
    # zero-size hole and a pinned global, before/after free. Catch faults in a
    # hosted test harness without replacing the checker itself.
    flags=['-O2','-ffreestanding','-fno-builtin','-emit-llvm','-c']
    objects=[]
    for name in ('heap','os'):
        bc=TMP/f'range-{name}-{major}.bc'
        run(llvm/'clang',*flags,ROOT/f'src/native/{name}.c','-o',bc); objects.append(bc)
    harness=TMP/f'ranges-{major}.bc'
    run(llvm/'clang',*flags,ROOT/'tests/checked_ranges.c','-o',harness)
    merged=TMP/f'ranges-linked-{major}.bc'
    run(llvm/'llvm-link',*objects,'--override='+str(harness),'-o',merged)
    ranges=TMP/f'ranges-{major}'
    run(llvm/'clang',merged,'-o',ranges)
    subprocess.run([ranges],check=True,timeout=10)
    print(f'LLVM {major}: {len(CASES)} materialised memory/division/fault cases, exact codes, no-host closure PASS')
    print(f'LLVM {major}: exhaustive small live/dead heap/global range matrix PASS')
