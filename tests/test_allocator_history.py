#!/usr/bin/env python3
"""Reverse actual published allocator stores plus independent primitive history."""
from pathlib import Path
import os
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'allocator-history';TMP.mkdir(parents=True,exist_ok=True)
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin')
    ir=subprocess.check_output([llvm/'llvm-dis',ROOT/f'dist/llvm{major}/musl-sysroot/lib/bennett-allocator.bc','-o','-'],text=True)
    for name in ('malloc','calloc','free','realloc','memset'):
        ir=re.sub(r'@'+name+r'(?=\()', '@test_'+name,ir)
    lines=[]; stores=0
    for line in ir.splitlines():
        if line.lstrip().startswith('store '):
            match=re.match(r'\s*store (?:volatile )?(i8|i32|i64|ptr) .*?, ptr ([^, ]+)',line)
            assert match,'unhandled allocator store '+line
            n={'i8':1,'i32':4,'i64':8,'ptr':8}[match[1]]
            lines.append(f'  call void @test_record(ptr {match[2]}, i64 {n})');stores+=1
        lines.append(line)
    assert stores>=10,stores
    lines.append('declare void @test_record(ptr, i64)')
    instrumented=TMP/f'allocator-{major}.ll';instrumented.write_text('\n'.join(lines)+'\n')
    exe=TMP/f'allocator-{major}'
    # O0 preserves the published instruction stream and added journal calls;
    # this is a hosted test interpreter, not the O2 comparison backend.
    subprocess.run([llvm/'clang','-O0','-ffreestanding','-fno-builtin',instrumented,
                    ROOT/'tests/allocator_history.c','-o',exe],check=True)
    subprocess.run([exe],check=True,timeout=10)
    print(f'LLVM {major}: allocator payload/metadata/list stores + primitive history fully reversed; identical re-execution PASS')
