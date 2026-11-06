#!/usr/bin/env python3
"""Published static context and one-time libc termination, without host CRT."""
from pathlib import Path
import os
import resource
import signal
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'process'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    bc=TMP/f'process-{major}.bc'; layout=TMP/f'layout-{major}.ll'
    flags=['--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
           '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c']
    run(llvm/'clang',*flags,ROOT/'tests/process.c','-o',bc)
    write_layout(layout, 503316480, 4096)
    exe=TMP/f'process-{major}'
    run(ROOT/'scripts/native-link',major,bc,exe,layout)
    for name in ('/opt/bin/context-probe','context-probe','/opt/bin/',''):
        for mode,code,stdout in [('return',17,b'21buffered'),('exit',23,b'21buffered'),
                                 ('x',29,b''),('prepare',17,b'21buffered'),('zero',0,b'')]:
            r=subprocess.run([name,mode],executable=exe,env={'BRT_CTX':'value'},capture_output=True,timeout=10)
            assert (r.returncode,r.stdout,r.stderr)==(code,stdout,b''),(major,name,mode,r)
    r=subprocess.run(['context-probe','bad'],executable=exe,env={'BRT_CTX':'value'},capture_output=True,timeout=10)
    assert r.returncode==-signal.SIGILL and r.stderr==b'bennett runtime fault 4\n' and not r.stdout,r
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    assert b'NEEDED' not in run('readelf','-d',exe)
    print(f'LLVM {major}: argc/argv/envp/program-name/errno/thread/fenv preparation, return/exit/_Exit handler+flush distinctions PASS')
