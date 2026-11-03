"""Test-only primitive support; retains actual production syscall/effect bodies."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def link(major, module, output, layout, fault_override=False):
    llvm=Path(f'/usr/lib/llvm-{major}/bin')
    flags=['--target=x86_64-unknown-linux-musl','-O2','-ffreestanding','-fno-builtin',
           '-nostdinc','-isystem',ROOT/f'dist/llvm{major}/musl-sysroot/include']
    def run(*args):
        r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120)
        assert r.returncode==0,(args,r)
    heap=output.with_suffix('.heap.bc'); os=output.with_suffix('.os.bc')
    extra=['-D__bennett_nz_fault=test_primitive_fault'] if fault_override else []
    run(llvm/'clang',*flags,*extra,'-emit-llvm','-c',ROOT/'src/native/heap.c','-o',heap)
    run(llvm/'clang',*flags,'-emit-llvm','-c',ROOT/'src/native/os.c','-o',os)
    closed=output.with_suffix('.closed.bc')
    run(llvm/'llvm-link',module,heap,os,layout,'-o',closed)
    start=output.with_suffix('.start.S')
    start.write_text('.text\n.global _start\n_start:\n and $-16,%rsp\n call main\n mov %eax,%edi\n mov $60,%eax\n syscall\n ud2\n.section .note.GNU-stack,"",@progbits\n')
    run(llvm/'clang',*flags,'-nostdlib','-static','-no-pie','-Wl,-e,_start','-Wl,--no-undefined',closed,start,'-o',output)
