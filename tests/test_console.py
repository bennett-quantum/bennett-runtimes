#!/usr/bin/env python3
"""Published console IR under a test-only store-history interpreter."""
from pathlib import Path
import re
import resource
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=ROOT/'.tmp/console-tests'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(*args):
    return subprocess.check_output(list(map(str,args)),stderr=None)
for m in (18,23):
    llvm=Path(f'/usr/lib/llvm-{m}/bin')
    dist=ROOT/f'dist/llvm{m}'
    ir=run(llvm/'llvm-dis',dist/'bennett-runtimes.bc','-o','-').decode()
    assert re.search(r'^define .*@__bennett_dev_console_write\(',ir,re.M), 'console model missing'
    assert not re.search(r'^define .*@__bennett_effect_',ir,re.M), 'production defines marker'
    # Instrument stores in the actual published model, not a handwritten twin.
    # The external journal is the native-model analogue of §17 execution history.
    active=False; lines=[]; stores=0
    for line in ir.splitlines():
        if line.startswith('define '): active='@__bennett_dev_console_write(' in line
        if active and line.lstrip().startswith('store '):
            match=re.match(r'\s*store (?:volatile )?(i8|i32|i64|ptr) .*?, ptr ([^, ]+)',line)
            assert match, 'unhandled model store: '+line
            size={'i8':1,'i32':4,'i64':8,'ptr':8}[match[1]]
            lines.append(f'  call void @test_record(ptr {match[2]}, i64 {size})')
            stores+=1
        lines.append(line)
    assert stores>=2
    lines.append('declare void @test_record(ptr, i64)')
    instrumented=TMP/f'model-{m}.ll'; instrumented.write_text('\n'.join(lines)+'\n')
    exe=TMP/f'model-{m}'
    heap=TMP/f'heap-{m}.o'
    run(llvm/'clang','-O2','-ffreestanding','-fno-builtin','-D__bennett_nz_fault=test_heap_fault','-c',ROOT/'src/native/heap.c','-o',heap)
    run(llvm/'clang','-O2','-ffreestanding','-fno-builtin','-no-pie',instrumented,ROOT/'tests/console.c',heap,'-Wl,--section-start=.bennett_heap=0x10000000','-o',exe)
    assert run(exe)==b'hello, world\n'
    for flag,code in [('u',8),('a',2),('m',8)]:
        r=subprocess.run([exe,flag],capture_output=True)
        assert r.returncode==100+code, (m,flag,r)
    print(f'LLVM {m}: musl writes -> marker -> console; store-history reverse restores all bytes; saved tape replay and tamper rejection; bindings/capacity faults PASS')
