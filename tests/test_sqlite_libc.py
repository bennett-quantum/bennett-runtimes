#!/usr/bin/env python3
"""Musl-derived string/UTC slice; realloc has a separate mandatory fixture."""
from pathlib import Path
from datetime import datetime,timezone
import os
import resource
import signal
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'sqlite-libc'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
epoch=datetime(1970,1,1,tzinfo=timezone.utc)
dates=[(1,1,1),(1600,2,29),(1900,2,28),(1900,3,1),(1969,12,31),(1970,1,1),
       (2000,2,29),(2000,3,1),(2024,2,29),(2038,1,19),(2100,3,1),(2400,2,29),(9999,12,31)]
rows=[]
for year,month,day in dates:
    d=datetime(year,month,day,23,59,59,tzinfo=timezone.utc)
    t=int((d-epoch).total_seconds())
    yday=(d-datetime(year,1,1,tzinfo=timezone.utc)).days
    rows.append(f'{{{t}LL,{year},{month},{day},23,59,59,{(d.weekday()+1)%7},{yday}}}')
(TMP/'calendar_vectors.h').write_text('static const struct calendar_case calendars[]={'+','.join(rows)+'};\n')
for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    names={line.split()[-1] for line in run(llvm/'llvm-nm','--defined-only',dist/'bennett-runtimes.bc').decode().splitlines() if line.split()}
    assert {'bcmp','strncmp','strspn','strcspn','strrchr','localtime'}<=names,'missing SQLite string/UTC definitions'
    bc=TMP/f'libc-{major}.bc'; layout=TMP/f'layout-{major}.ll'; exe=TMP/f'libc-{major}'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
        '-nostdinc','-isystem',dist/'musl-sysroot/include','-I',TMP,'-emit-llvm','-c',ROOT/'tests/sqlite_libc.c','-o',bc)
    # Keep frontend vectorisation enabled. Scalarise its fixed vectors at the
    # N21 boundary before the comparison-only memory/fault adapter.
    scalar=TMP/f'scalar-{major}.bc'
    options=['-passes=scalarizer','-scalarize-load-store'] if major==18 else ['-passes=scalarizer<load-store>']
    run(llvm/'opt',*options,bc,'-o',scalar)
    write_layout(layout, 520093696, 16384)
    run(ROOT/'scripts/native-check-link',major,scalar,exe,layout)
    subprocess.run([exe],check=True,timeout=20)
    r=subprocess.run([exe,'fault'],capture_output=True,timeout=10)
    assert r.returncode==-signal.SIGILL and r.stderr==b'bennett runtime fault 7\n',r
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    assert b'NEEDED' not in run('readelf','-d',exe)
    print(f'LLVM {major}: 256 exact-sized string/alignment cases, real unterminated-string fault, UTC calendar/overflow vectors, no-host closure PASS')
