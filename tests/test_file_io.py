#!/usr/bin/env python3
"""Real musl FILE operations over memory images, checked with no host libc."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'file-io'; TMP.mkdir(parents=True,exist_ok=True)
def run(*args):
    try: return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace')); raise
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    names={l.split()[-1] for l in run(llvm/'llvm-nm','--defined-only',dist/'bennett-runtimes.bc').decode().splitlines() if l.split()}
    assert {'fmemopen','open_memstream','fread','fclose','fseek','ftell','fgetpos','fsetpos'}<=names,'missing production FILE closure'
    bc=TMP/f'file-io-{major}.bc'; scalar=TMP/f'scalar-{major}.bc'; exe=TMP/f'file-io-{major}'; layout=TMP/f'layout-{major}.ll'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
        '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',ROOT/'tests/file_io.c','-o',bc)
    options=['-passes=scalarizer','-scalarize-load-store'] if major==18 else ['-passes=scalarizer<load-store>']
    run(llvm/'opt',*options,bc,'-o',scalar)
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 536875008,i64 1048576}, align 8\n')
    run(ROOT/'scripts/native-check-link',major,scalar,exe,layout)
    subprocess.run([exe],check=True,timeout=15)
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    print(f'LLVM {major}: musl memory-file read/write/seek/EOF/error/append/growth and exact zero-size buffer PASS')
