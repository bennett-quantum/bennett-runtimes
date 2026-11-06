#!/usr/bin/env python3
"""Same defined C program against independent host glibc and runtime musl."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'host-libc'
TMP.mkdir(parents=True, exist_ok=True)
for major in (18, 23):
    llvm = llvm_dir(major)
    host, native = TMP/f'host-{major}', TMP/f'native-{major}'
    source = ROOT/'tests/libc_differential.c'
    run(llvm/'clang', '-O2', '-fno-builtin', source, '-o', host)
    oracle = subprocess.run([host], capture_output=True, timeout=10)
    assert oracle.returncode == 0, oracle
    bc, layout = TMP/f'input-{major}.bc', TMP/f'layout-{major}.ll'
    write_layout(layout, 0x28000000, 1048576)
    run(llvm/'clang', '--target=x86_64-unknown-linux-musl', '-O2', '-ffreestanding', '-fno-builtin',
        '-nostdinc', '-isystem', ROOT/f'dist/llvm{major}/musl-sysroot/include',
        '-emit-llvm', '-c', source, '-o', bc)
    run(ROOT/'scripts/native-link', major, bc, native, layout)
    actual = subprocess.run([native], capture_output=True, timeout=10)
    assert (actual.returncode, actual.stdout, actual.stderr) == (oracle.returncode, oracle.stdout, oracle.stderr), (major, oracle, actual)
    print(f'LLVM {major}: host glibc differential: 9 allocator boundaries and 35 printf/stdio records PASS')
