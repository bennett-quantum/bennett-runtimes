#!/usr/bin/env python3
"""Real musl frontend, linked closure, static startup and formatted output."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
TMP = ROOT/'.tmp/musl-tests'
TMP.mkdir(parents=True, exist_ok=True)
def run(*args):
    return subprocess.check_output(list(map(str,args)), stderr=subprocess.PIPE)
for m in (18,23):
    llvm = Path(f'/usr/lib/llvm-{m}/bin')
    dist = ROOT/f'dist/llvm{m}'
    sysroot = dist/'musl-sysroot'
    assert (sysroot/'include/stdio.h').is_file(), 'real musl sysroot missing'
    fixtures = [('hello', '#include <stdio.h>\nint main(void) { printf("hello, world\\n"); return 0; }', b'hello, world\n'),
                ('formatted', '#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\nstatic void done(void){puts("handler");}\nint main(void){atexit(done); printf("%s %d %.2f", "musl",42,1.25); return 0;}', b'musl 42 1.25handler\n')]
    for label, code, expected in fixtures:
        source=TMP/f'{label}.c'; source.write_text(code)
        bc=TMP/f'{label}-{m}.bc'; linked=TMP/f'{label}-linked-{m}.bc'
        run(llvm/'clang', '--target=x86_64-unknown-linux-musl', '--sysroot='+str(sysroot), '-nostdinc', '-isystem', sysroot/'include', '-O2', '-flto', '-emit-llvm', '-c', source, '-o', bc)
        run(llvm/'llvm-link', bc, dist/'bennett-runtimes.bc', '-o', linked)
        run(llvm/'opt', '-passes=lto<O3>', linked, '-o', linked)
        exe=TMP/f'{label}-{m}'
        run(ROOT/'scripts/native-link', str(m), linked, exe)
        assert run(exe)==expected, (m,label)
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        assert b'NEEDED' not in run('readelf','-d',exe)
    print(f'LLVM {m}: ordinary musl hello + formatted printf/atexit/return flush; static no-host-libc closure PASS')
