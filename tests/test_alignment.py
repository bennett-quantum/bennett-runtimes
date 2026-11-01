#!/usr/bin/env python3
"""Wrapper tests against independent primitives, including pre-mutation faults."""
from pathlib import Path
import os
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'alignment'; TMP.mkdir(parents=True,exist_ok=True)
for major in (18,23):
    clang=f'/usr/lib/llvm-{major}/bin/clang'
    obj=TMP/f'bump-{major}.o'
    subprocess.run([clang,'-O2','-ffreestanding','-fno-builtin','-Dmalloc=runtime_malloc',
                    '-Dcalloc=runtime_calloc','-Dfree=runtime_free','-c',ROOT/'src/allocator/bump.c','-o',obj],check=True)
    exe=TMP/f'alignment-{major}'
    subprocess.run([clang,'-O2',ROOT/'tests/alignment.c',obj,'-o',exe],check=True)
    for case in ('history','overflow','padding-budget','extent-budget'):
        subprocess.run([exe,case],check=True,timeout=5)
    print(f'LLVM {major}: aligned/odd/zero requests, padding history/reversal, overflow and budget boundaries PASS')
