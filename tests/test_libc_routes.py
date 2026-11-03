#!/usr/bin/env python3
"""Required real libc APIs through ordinary clock/entropy/data-image routes."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'libc-routes'; TMP.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('varargs_support',ROOT/'tests/varargs_support.py')
support=importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
spec=importlib.util.spec_from_file_location('production_support',ROOT/'tests/production_support.py')
production=importlib.util.module_from_spec(spec); spec.loader.exec_module(production)
def run(*args):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=180)
    assert r.returncode==0,(args,r)
    return r.stdout
apis=('clock_gettime','gettimeofday','time','getrandom','getentropy','fopen','open','read',
      'pread','pwrite','dup','fstat','stat','mkdir','rmdir','rename','unlink','ftruncate')
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    ir=run(llvm/'llvm-dis',dist/'bennett-runtimes.bc','-o','-').decode()
    defined=set(re.findall(r'^(?:define .*?@|@)([^ (]+)(?:\(| = .*alias)',ir,re.M))
    assert set(apis)<=defined, (major,sorted(set(apis)-defined))
    hostroot=TMP/f'host-root-{major}'
    hostroot.mkdir(exist_ok=True)
    host=TMP/f'host-vfs-{major}'
    run(llvm/'clang','-O2','-DVFS_PREFIX="'+str(hostroot)+'"',ROOT/'tests/vfs.c','-o',host)
    assert run(host)==b''
    hostroot.rmdir()
    for label in ('libc_devices','vfs'):
        app=TMP/f'{label}-{major}.bc'; linked=TMP/f'{label}-linked-{major}.bc'
        normal=TMP/f'{label}-n23-{major}.bc'; exe=TMP/f'{label}-{major}'
        run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
            '-fno-vectorize','-fno-slp-vectorize','-nostdinc','-isystem',dist/'musl-sysroot/include',
            '-emit-llvm','-c',ROOT/f'tests/{label}.c','-o',app)
        run(llvm/'llvm-link',dist/'bennett-runtimes.bc','--override='+str(app),'-o',linked)
        support.materialise(major,linked,normal)
        layout=TMP/f'layout-{major}.ll'
        layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 603983872,i64 2097152}, align 8\n')
        run(ROOT/'scripts/native-check-link',major,normal,exe,layout)
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        assert b'NEEDED' not in run('readelf','-d',exe)
        assert run(exe)==b''
        original=TMP/f'{label}-production-{major}'
        production.link(major,normal,original,layout)
        assert run(original)==b''
    print(f'LLVM{major}: clock/time/entropy/random-device markers; real file creation, sparse data, shared offsets, FILE I/O, append/truncate/rename/unlink and fd reuse PASS',flush=True)
