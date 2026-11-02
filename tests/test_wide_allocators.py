#!/usr/bin/env python3
"""Allocator contract vectors through actual published bitcode, no host libc."""
import os
from pathlib import Path
import resource
import signal
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'wide-allocators'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(*args):
    try: return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace')); raise
required={'aligned_alloc','memalign','posix_memalign','strdup','strndup',
          '_Znwm','_Znam','_ZnwmRKSt9nothrow_t','_ZnamRKSt9nothrow_t',
          '_ZnwmSt11align_val_t','_ZnamSt11align_val_t',
          '__rust_alloc','__rust_alloc_zeroed','__rust_realloc','__rust_dealloc',
          'jl_gc_alloc','ijl_gc_alloc_typed','ijl_gc_small_alloc','ijl_gc_big_alloc','julia.gc_alloc_obj'}
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    names={l.split()[-1] for l in run(llvm/'llvm-nm','--defined-only',dist/'bennett-runtimes.bc').decode().splitlines() if l.split()}
    assert required<=names,('missing production allocators',sorted(required-names))
    ir=run(llvm/'llvm-dis',dist/'bennett-runtimes.bc','-o','-').decode()
    import re
    fresh=required-{'posix_memalign','__rust_realloc','__rust_dealloc'}
    for name in fresh:
        definition=next(l for l in ir.splitlines() if l.startswith('define ') and '@'+name+'(' in l)
        group=re.search(r'#(\d+)',definition)[1]
        attributes=next(l for l in ir.splitlines() if l.startswith('attributes #'+group+' ='))
        assert 'allockind("alloc")' in attributes,(name,attributes)
    layout=TMP/f'layout-{major}.ll'
    # Page aligned but deliberately not 8192 aligned: the initial heap base
    # is not known to ordinary C until a primitive allocation returns it.
    layout.write_text('@__bennett_rt_native_layout = constant { i64, i64, i64 } { i64 1, i64 536875008, i64 1048576 }, align 8\n')
    for source in ('wide_allocators.c','new.cpp'):
        name=Path(source).stem; bc=TMP/f'{name}-{major}.bc'; scalar=TMP/f'scalar-{name}-{major}.bc'; exe=TMP/f'{name}-{major}'
        flags=['-std=c++17','-fno-exceptions','-fno-rtti','-fsized-deallocation'] if source.endswith('.cpp') else []
        run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',*flags,
            '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',ROOT/'tests'/source,'-o',bc)
        options=['-passes=scalarizer','-scalarize-load-store'] if major==18 else ['-passes=scalarizer<load-store>']
        run(llvm/'opt',*options,bc,'-o',scalar)
        run(ROOT/'scripts/native-check-link',major,scalar,exe,layout)
        subprocess.run([exe],check=True,timeout=20)
        if source.endswith('.c'):
            subprocess.run([exe,'s'],check=True,timeout=10)
            for arg,code in [('o',2),('r',4),('j',2)]:
                r=subprocess.run([exe,arg],capture_output=True,timeout=10)
                assert r.returncode==-signal.SIGILL and r.stderr==f'bennett runtime fault {code}\n'.encode(),r
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    print(f'LLVM {major}: aligned C, duplicates, C++ new/delete, Rust allocation and Julia tagged allocation vectors PASS')
