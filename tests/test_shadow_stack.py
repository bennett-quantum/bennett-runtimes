#!/usr/bin/env python3
"""Actual publication: logical ranges, alignment, reset, restore and stale access."""
import os
from pathlib import Path
import resource
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'shadow-stack'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(*args):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120)
    assert r.returncode==0,(args,r)
    return r.stdout
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    for level in (0,2):
        app=TMP/f'app-{major}-{level}.bc'; layout=TMP/f'layout-{major}.ll'; exe=TMP/f'stack-{major}-{level}'
        run(llvm/'clang','--target=x86_64-unknown-linux-musl',f'-O{level}','-ffreestanding','-fno-builtin',
            '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',ROOT/'tests/shadow_stack.c','-o',app)
        layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 637538304,i64 1048576}, align 8\n')
        run(ROOT/'scripts/native-check-link',major,app,exe,layout)
        assert run(exe)==b''
        for case,code in [('g',7),('o',7),('d',7),('b',2),('a',4),('w',7),('t',7),('r',7)]:
            r=subprocess.run([exe,case],capture_output=True,timeout=10)
            assert r.returncode==-4 and r.stderr==f'bennett runtime fault {code}\n'.encode() and not r.stdout,(major,level,case,r)
    print(f'LLVM{major}: logical stack frames/dynamic allocations, idempotent init, saved restore/reuse/zeroing, O0/O2 full-range and stale-access faults PASS',flush=True)
