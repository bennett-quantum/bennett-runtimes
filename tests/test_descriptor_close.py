#!/usr/bin/env python3
"""Independent same-source host, production bridge and checked descriptor cases."""
import os
import importlib.util
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'descriptor-close'
TMP.mkdir(parents=True, exist_ok=True)
spec=importlib.util.spec_from_file_location('production_support',ROOT/'tests/production_support.py')
production=importlib.util.module_from_spec(spec); spec.loader.exec_module(production)
for major in (18, 23):
    llvm = llvm_dir(major); dist = ROOT/f'dist/llvm{major}'
    for level in (0, 2):
        exe = TMP/f'host-{major}-{level}'
        run(llvm/'clang', f'-O{level}', ROOT/'tests/descriptor_close.c', '-o', exe)
        assert run(exe) == b''
        flags = ['--target=x86_64-unknown-linux-musl', f'-O{level}', '-ffreestanding', '-fno-builtin',
                 '-nostdinc', '-isystem', dist/'musl-sysroot/include', '-emit-llvm', '-c']
        app = TMP/f'app-{major}-{level}.bc'; layout = TMP/f'layout-{major}.ll'
        run(llvm/'clang', *flags, ROOT/'tests/descriptor_close.c', '-o', app)
        write_layout(layout, 587206656, 1048576)
        exe = TMP/f'checked-{major}-{level}'
        run(ROOT/'scripts/native-check-link', major, app, exe, layout)
        assert run(exe) == b''
        app = TMP/f'production-{major}-{level}.bc'; linked = TMP/f'linked-{major}-{level}.bc'
        run(llvm/'clang', f'-O{level}', '-ffreestanding', '-fno-builtin', '-DTEST_PRODUCTION',
            '-emit-llvm', '-c', ROOT/'tests/descriptor_close.c', '-o', app)
        run(llvm/'llvm-link', app, dist/'bennett-runtimes.bc', '--only-needed', '-o', linked)
        exe = TMP/f'production-{major}-{level}'
        production.link(major,linked,exe,layout,fault_override=True)
        assert run(exe) == b''
    print(f'LLVM {major}: same-source host/checked O0/O2 close/write/writev/seek/repeated-close, no output or production effect; process reset PASS', flush=True)
