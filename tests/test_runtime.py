#!/usr/bin/env python3
"""Test actual published bitcode and comparison-only primitive lowering."""
from pathlib import Path
import resource
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TMP = ROOT / '.tmp' / 'tests'
TMP.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for major in (18, 23):
    clang = f'/usr/lib/llvm-{major}/bin/clang'
    exe = TMP / f'allocator-{major}'
    subprocess.run([clang, '-target', 'x86_64-unknown-linux-gnu', '-O2', '-ffreestanding', '-fno-builtin', '-no-pie',
                    '-DBENNETT_NATIVE_HEAP_SIZE=256', '-DBENNETT_NATIVE_ALLOCATIONS=64',
                    '-Wl,--section-start=.bennett_heap=0x10000000',
                    str(ROOT/'tests/allocator.c'), str(ROOT/'src/native/heap.c'),
                    str(ROOT/f'dist/llvm{major}/bennett-runtimes.bc'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    for case, message in [('d', b'invalid or double free'), ('i', b'invalid or double free'),
                          ('g', b'invalid or double free'), ('o', b'heap exhausted'),
                          ('c', b'heap exhausted'), ('h', b'heap exhausted'),
                          ('t', b'allocation table exhausted')]:
        result = subprocess.run([str(exe), case], capture_output=True, timeout=5)
        assert result.returncode == -signal.SIGILL, (major, case, result)
        assert message in result.stderr, (major, case, result.stderr)
    print(f'LLVM {major}: allocation addresses/zero-fill/history, memory helpers, seven traps PASS')
