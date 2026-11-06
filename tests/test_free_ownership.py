#!/usr/bin/env python3
"""Reject C free of an unrecorded primitive allocation before any mutation."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'free-ownership'
TMP.mkdir(parents=True, exist_ok=True)
for major in (18, 23):
    exe = TMP/f'free-{major}'
    subprocess.run([str(llvm_dir(major)/'clang'), '-O2', '-ffreestanding', '-fno-builtin',
                    '-Dmalloc=test_malloc', '-Dcalloc=test_calloc', '-Dfree=test_free',
                    '-Drealloc=test_realloc', ROOT/'tests/free_ownership.c',
                    ROOT/'src/allocator/bump.c', ROOT/'src/libc/memory.c', '-o', exe], check=True)
    result = subprocess.run([exe], timeout=5)
    assert result.returncode == 107, (major, result.returncode)
    print(f'LLVM {major}: unowned free faults 7 before primitive state changes; free(NULL) has no effect')
