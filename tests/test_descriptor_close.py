#!/usr/bin/env python3
"""Independent same-source host, production bridge and checked descriptor cases."""
import os
import importlib.util
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'descriptor-close'
TMP.mkdir(parents=True, exist_ok=True)
spec=importlib.util.spec_from_file_location('production_support',ROOT/'tests/production_support.py')
production=importlib.util.module_from_spec(spec); spec.loader.exec_module(production)
def run(*args):
    r = subprocess.run(list(map(str, args)), capture_output=True, timeout=120)
    assert r.returncode == 0, (args, r)
    return r.stdout
for major in (18, 23):
    llvm = Path(f'/usr/lib/llvm-{major}/bin'); dist = ROOT/f'dist/llvm{major}'
    for level in (0, 2):
        exe = TMP/f'host-{major}-{level}'
        run(llvm/'clang', f'-O{level}', ROOT/'tests/descriptor_close.c', '-o', exe)
        assert run(exe) == b''
        flags = ['--target=x86_64-unknown-linux-musl', f'-O{level}', '-ffreestanding', '-fno-builtin',
                 '-nostdinc', '-isystem', dist/'musl-sysroot/include', '-emit-llvm', '-c']
        app = TMP/f'app-{major}-{level}.bc'; layout = TMP/f'layout-{major}.ll'
        run(llvm/'clang', *flags, ROOT/'tests/descriptor_close.c', '-o', app)
        layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 587206656,i64 1048576}, align 8\n')
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
