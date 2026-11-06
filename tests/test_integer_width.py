#!/usr/bin/env python3
"""LLVM's admitted-width bound is checked before any width-sized VLA or read."""
from pathlib import Path
import os
import resource
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'integer-width'
TMP.mkdir(parents=True, exist_ok=True)
def small_stack():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_STACK, (65536, 65536))
for major in (18, 23):
    exe = TMP/f'width-{major}'
    subprocess.run([str(llvm_dir(major)/'clang'), '-O2',
                    ROOT/'tests/integer_width.c', ROOT/'src/runtime/integer_core.c',
                    ROOT/'src/runtime/integer_arithmetic.c', '-o', exe], check=True)
    for width in (0, 8388609, 0xffffffff):
        for op in (0, 15, 16):
            result = subprocess.run([exe, str(width), str(op)], preexec_fn=small_stack, timeout=5)
            assert result.returncode == 104, (major, width, op, result.returncode)
    print(f'LLVM {major}: invalid widths fault 4 before access/allocation on a 64 KiB stack')
