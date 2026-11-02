#!/usr/bin/env python3
"""Global index must match single-object byte membership, never coalescing."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'global-ranges'; TMP.mkdir(parents=True,exist_ok=True)
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); objects=[]
    for source in ('src/native/heap.c','src/native/os.c','tests/global_ranges.c'):
        out=TMP/f'{Path(source).stem}-{major}.bc'; objects.append(out)
        subprocess.run([llvm/'clang','-O2','-ffreestanding','-fno-builtin','-emit-llvm','-c',ROOT/source,'-o',out],check=True)
    linked=TMP/f'linked-{major}.bc'; exe=TMP/f'ranges-{major}'
    subprocess.run([llvm/'llvm-link',*objects[:2],'--override='+str(objects[2]),'-o',linked],check=True)
    subprocess.run([llvm/'clang',linked,'-o',exe],check=True)
    subprocess.run([exe],check=True,timeout=10)
    print(f'LLVM{major}: global interval index vs exhaustive byte-set reference, overlaps/duplicates/adjacency/zero/wrap PASS')
