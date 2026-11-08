#!/usr/bin/env python3
"""Real native variadic save/overflow objects, exact bounds and callee lifetime."""
import hashlib
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'native-varargs'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
# Hang guards, measured and classified in docs/quality.md.
EXECUTION_HANG_GUARD=600
for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    source=TMP/f'input-{major}.bc'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O0','-nostdinc',
        '-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',
        ROOT/'tests/native_varargs.c','-o',source)
    layout=TMP/f'layout-{major}.ll'; write_layout(layout,536875008,65536)
    closed=TMP/f'closed-{major}.bc'; checked=TMP/f'checked-{major}.bc'
    native=TMP/f'native-{major}.bc'
    run(llvm/'llvm-link',dist/'bennett-runtimes.bc','--override='+str(source),layout,'-o',closed)
    original=hashlib.sha256(closed.read_bytes()).hexdigest()
    run(ROOT/'scripts/native-check',major,closed,checked)
    assert hashlib.sha256(closed.read_bytes()).hexdigest()==original
    run(llvm/'llvm-link',checked,'--override='+str(dist/'musl-sysroot/lib/native-support.bc'),'-o',native)
    for level in (0,2):
        exe=TMP/f'varargs-{major}-O{level}'
        run(llvm/'clang','--target=x86_64-unknown-linux-musl',f'-O{level}',
            '-nostdlib','-nostdinc','-static','-no-pie','-ffreestanding','-fno-builtin',
            '-Wl,-e,_start','-Wl,--no-undefined',native,'-o',exe)
        for mode in range(7):
            result=subprocess.run([exe,str(mode)],capture_output=True,timeout=EXECUTION_HANG_GUARD)
            expected=(0,b'',b'') if not mode else (
                -signal.SIGILL,b'',b'bennett runtime fault 7\n')
            assert (result.returncode,result.stdout,result.stderr)==expected,(major,level,mode,result)
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        assert b'NEEDED' not in run('readelf','-d',exe)
        print(f'LLVM{major} O{level}: raw scalar/FP/byval/indirect/va_copy/restart, '
              'exact save/overflow bounds and returned storage lifetime PASS',flush=True)
