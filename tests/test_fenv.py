#!/usr/bin/env python3
"""One literal shared object, C fenv mapping and actual arith linkage."""
from pathlib import Path
import os
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout, pinned_sibling
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'fenv'; TMP.mkdir(parents=True,exist_ok=True)
for major in (18,23):
    llvm=llvm_dir(major); dist=ROOT/f'dist/llvm{major}'
    ir=run(llvm/'llvm-dis',dist/'bennett-runtimes.bc','-o','-').decode()
    definitions=re.findall(r'^@__bennett_rt_fenv = .*$',ir,re.M)
    assert len(definitions)==1 and re.search(r'global \{ i32, i32 \} zeroinitializer, section "\.data\.__bennett_rt_fenv", align 4(?:, !guid !\d+)?$',definitions[0]),definitions
    arith=pinned_sibling('bennett-arith')/f'dist/llvm{major}/bennett-arith.bc'
    a=run(llvm/'llvm-dis',arith,'-o','-').decode()
    assert re.search(r'^@__bennett_rt_fenv = external global \{ i32, i32 \}',a,re.M)
    flags=['--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
           '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c']
    bc=TMP/f'fenv-{major}.bc'; layout=TMP/f'layout-{major}.ll'
    run(llvm/'clang',*flags,ROOT/'tests/fenv.c','-o',bc)
    write_layout(layout, 486539264, 4096)
    exe=TMP/f'fenv-{major}'
    run(ROOT/'scripts/native-link',major,bc,exe,layout)
    subprocess.run([exe],check=True,timeout=10)
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    # Fault catching uses hosted setjmp; only the actual published fenv
    # definitions are imported. No second implementation of fenv is tested.
    harness=TMP/f'faults-{major}.bc'; linked=TMP/f'faults-linked-{major}.bc'
    run(llvm/'clang','-O2','-ffreestanding','-fno-builtin','-emit-llvm','-c',ROOT/'tests/fenv_faults.c','-o',harness)
    run(llvm/'llvm-link',harness,dist/'bennett-runtimes.bc','--only-needed','-o',linked)
    exe=TMP/f'faults-{major}'; run(llvm/'clang',linked,'-o',exe)
    subprocess.run([exe],check=True,timeout=10)
    print(f'LLVM {major}: one fenv object/type/initial bytes, 5 modes x 32 flags/masks, snapshots/reset, arith shared flags and pre-mutation faults PASS')
