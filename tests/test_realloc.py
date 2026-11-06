#!/usr/bin/env python3
"""Real allocator growth/shrink/zero/live-prefix semantics and invalid inputs."""
from pathlib import Path
import os
import resource
import signal
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'realloc'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    names={line.split()[-1] for line in run(llvm/'llvm-nm','--defined-only',dist/'bennett-runtimes.bc').decode().splitlines() if line.split()}
    assert 'realloc' in names,'missing production realloc definition'
    bc=TMP/f'realloc-{major}.bc'; scalar=TMP/f'scalar-{major}.bc'
    layout=TMP/f'layout-{major}.ll'; exe=TMP/f'realloc-{major}'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
        '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',ROOT/'tests/realloc.c','-o',bc)
    options=['-passes=scalarizer','-scalarize-load-store'] if major==18 else ['-passes=scalarizer<load-store>']
    run(llvm/'opt',*options,bc,'-o',scalar)
    write_layout(layout, 536870912, 16384)
    run(ROOT/'scripts/native-check-link',major,scalar,exe,layout)
    subprocess.run([exe],check=True,timeout=10)
    for arg,code in [('u',7),('i',7),('o',2)]:
        r=subprocess.run([exe,arg],capture_output=True,timeout=10)
        assert r.returncode==-signal.SIGILL and r.stderr==f'bennett runtime fault {code}\n'.encode(),r
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    print(f'LLVM {major}: realloc growth/shrink/zero/null, live-prefix/zero-fill and invalid/dead/overflow faults PASS')
