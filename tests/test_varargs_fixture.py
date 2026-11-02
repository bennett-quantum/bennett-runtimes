#!/usr/bin/env python3
"""Independent scalar/byval/va-copy checks for N23 component preparation."""
import importlib.util
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'varargs-fixture'; TMP.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('varargs_support',ROOT/'tests/varargs_support.py')
support=importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
def run(*args):
    try: return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace')); raise
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    bc=TMP/f'original-{major}.bc'; normal=TMP/f'n23-{major}.bc'; exe=TMP/f'fixture-{major}'; layout=TMP/f'layout-{major}.ll'
    # O0/noinline preserves the frontend ABI boundaries rather than folding
    # the known sums before the test transformation can exercise them.
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O0','-nostdinc','-isystem',dist/'musl-sysroot/include',
        '-emit-llvm','-c',ROOT/'tests/varargs.c','-o',bc)
    support.materialise(major,bc,normal)
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 536875008,i64 65536},align 8\n')
    run(ROOT/'scripts/native-check-link',major,normal,exe,layout)
    subprocess.run([exe],check=True,timeout=10)
    print(f'LLVM{major}: N23 fixture scalar/pointer/FP/large-byval arguments, empty list and va_copy PASS',flush=True)
