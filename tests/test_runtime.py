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
    dist=ROOT/f'dist/llvm{major}'
    flags=['--target=x86_64-unknown-linux-musl', '-O2', '-ffreestanding', '-fno-builtin',
           '-nostdinc', '-isystem', str(dist/'musl-sysroot/include'), '-emit-llvm', '-c']
    source=TMP/f'allocator-{major}.bc'
    subprocess.run([clang,*flags,ROOT/'tests/allocator.c','-o',source],check=True)
    config=TMP/f'layout-{major}.c'
    config.write_text('const unsigned long __bennett_rt_native_layout[3]={1,0x19000000,512};\n')
    layout=TMP/f'layout-{major}.bc'
    subprocess.run([clang,*flags,config,'-o',layout],check=True)
    subprocess.run([ROOT/'scripts/native-link',str(major),source,exe,layout],check=True)
    subprocess.run([str(exe)], check=True)
    for case, message in [('d', b'fault 7'), ('i', b'fault 7'),
                          ('g', b'fault 7'), ('o', b'fault 2'),
                          ('c', b'fault 2'), ('h', b'fault 2'),
                          ('t', b'fault 2')]:
        result = subprocess.run([str(exe), case], capture_output=True, timeout=5)
        assert result.returncode == -signal.SIGILL, (major, case, result)
        assert message in result.stderr, (major, case, result.stderr)
    print(f'LLVM {major}: allocation addresses/zero-fill/history, memory helpers, seven traps PASS')
