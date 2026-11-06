#!/usr/bin/env python3
"""Real musl frontend, linked closure, static startup and formatted output."""
from pathlib import Path
import os
import subprocess
import json
import hashlib
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, pinned_sibling
ROOT = Path(__file__).resolve().parents[1]
ARITH = pinned_sibling('bennett-arith')
TMP = ROOT/'.tmp/musl-tests'
TMP.mkdir(parents=True, exist_ok=True)
for m in (18,23):
    llvm = llvm_dir(m)
    dist = ROOT/f'dist/llvm{m}'
    sysroot = dist/'musl-sysroot'
    assert (sysroot/'include/stdio.h').is_file(), 'real musl sysroot missing'
    manifest=json.loads((dist/'manifest.json').read_text())
    for name, digest in manifest['sysroot'].items():
        assert hashlib.sha256((dist/name).read_bytes()).hexdigest()==digest, name
    assert set(manifest['undefined_imports']) <= {'__bennett_nz_alloc','__bennett_nz_free','__bennett_nz_fault',
        '__bennett_effect_console_write','__bennett_effect_clock_read','__bennett_effect_entropy_read',
        '__bennett_effect_block_read','__bennett_effect_block_write','__fini_array_start','__fini_array_end'}
    assert {'printf','puts','write','exit','__bennett_dev_console_write'} <= set(manifest['exports'])
    fixtures = [('hello', '#include <stdio.h>\nint main(void) { printf("hello, world\\n"); return 0; }', b'hello, world\n'),
                ('formatted', '#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\nstatic void done(void){puts("handler");}\nint main(void){atexit(done); printf("%s %d %.2f", "musl",42,1.25); return 0;}', b'musl 42 1.25handler\n'),
                ('flush', '#include <stdio.h>\nint main(void){printf("%s %d","buffered",7); return 0;}', b'buffered 7'),
                ('putc', '#include <stdio.h>\n#include <unistd.h>\nint main(void){write(1,"direct ",7); putchar(65); return 0;}', b'direct A'),
                ('exit', '#include <stdio.h>\n#include <stdlib.h>\nint main(void){printf("exit %d",23); exit(23);}', b'exit 23')]
    config=TMP/f'layout-{m}.c'
    config.write_text('const unsigned long __bennett_rt_native_layout[3]={1,0x1a000000,1048576};\n')
    layout=TMP/f'layout-{m}.bc'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-ffreestanding','-emit-llvm','-c',config,'-o',layout)
    for label, code, expected in fixtures:
        source=TMP/f'{label}.c'; source.write_text(code)
        bc=TMP/f'{label}-{m}.bc'; linked=TMP/f'{label}-linked-{m}.bc'
        run(llvm/'clang', '--target=x86_64-unknown-linux-musl', '--sysroot='+str(sysroot), '-nostdinc', '-isystem', sysroot/'include', '-O2', '-flto', '-emit-llvm', '-c', source, '-o', bc)
        run(llvm/'llvm-link', bc, dist/'bennett-runtimes.bc', ARITH/f'dist/llvm{m}/bennett-arith.bc', '--only-needed', '-o', linked)
        run(llvm/'opt', '-passes=lto<O3>', linked, '-o', linked)
        exe=TMP/f'{label}-{m}'
        run(ROOT/'scripts/native-link', str(m), linked, exe, layout)
        result=subprocess.run([exe],capture_output=True,timeout=10)
        assert result.stdout==expected and result.returncode==(23 if label=='exit' else 0), (m,label,result)
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        assert b'NEEDED' not in run('readelf','-d',exe)
    print(f'LLVM {m}: ordinary musl hello + formatted printf/atexit/flush/explicit exit; static no-host-libc closure PASS')
