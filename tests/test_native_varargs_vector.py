#!/usr/bin/env python3
"""Repeated wide-vector arguments use bounded comparison-private stack storage."""
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from common import run,llvm_dir,write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'native-varargs-vector';TMP.mkdir(parents=True,exist_ok=True)
# This AVX native ABI fixture uses a host with AVX, as does its original control.
# An 8-lane vector sums to 36; 4096 calls sum to 147456. A leaked 32-byte
# probe slot per iteration would require 131072 bytes, exceeding this 64 KiB
# resource fixture. Both original and checked execution must still succeed.
def stack_fixture():
    resource.setrlimit(resource.RLIMIT_STACK,(65536,65536))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
for major in (18,23):
    llvm=llvm_dir(major);dist=ROOT/f'dist/llvm{major}'
    callee=TMP/f'callee-{major}.bc';caller=TMP/f'caller-{major}.ll'
    run(llvm/'clang','--target=x86_64-unknown-linux-musl','-mavx','-O0','-ffreestanding',
        '-nostdinc','-isystem',dist/'musl-sysroot/include','-emit-llvm','-c',
        ROOT/'tests/native_varargs_vector.c','-o',callee)
    caller.write_text('''target triple = "x86_64-unknown-linux-musl"
declare i32 @read_vector(i32,...)
define i32 @main() #0 {
entry:
 br label %loop
loop:
 %n = phi i32 [0,%entry],[%next,%loop]
 %sum = phi i32 [0,%entry],[%total,%loop]
 %value = call i32 (i32,...) @read_vector(i32 0,<8 x i32> <i32 1,i32 2,i32 3,i32 4,i32 5,i32 6,i32 7,i32 8>)
 %total = add i32 %sum,%value
 %next = add i32 %n,1
 %done = icmp eq i32 %next,4096
 br i1 %done,label %exit,label %loop
exit:
 %bad = icmp ne i32 %total,147456
 %status = zext i1 %bad to i32
 ret i32 %status
}
attributes #0 = { \"target-features\"=\"+avx\" }
''')
    layout=TMP/f'layout-{major}.ll';write_layout(layout,536875008,65536)
    closed=TMP/f'closed-{major}.bc';checked=TMP/f'checked-{major}.bc'
    run(llvm/'llvm-link',callee,caller,dist/'bennett-runtimes.bc',layout,'-o',closed)
    run(ROOT/'scripts/native-check',major,closed,checked)
    for label,module in (('original',closed),('checked',checked)):
        native=TMP/f'{label}-{major}.bc'
        run(llvm/'llvm-link',module,'--override='+str(dist/'musl-sysroot/lib/native-support.bc'),'-o',native)
        for level in (0,2):
            exe=TMP/f'{label}-{major}-O{level}'
            run(llvm/'clang',f'-O{level}','-nostdlib','-static','-no-pie','-ffreestanding',
                '-fno-builtin','-Wl,-e,_start',native,'-o',exe)
            # Hang guard only; see docs/quality.md. Assertions count work/bytes.
            result=subprocess.run([exe],capture_output=True,timeout=600,preexec_fn=stack_fixture)
            assert (result.returncode,result.stdout,result.stderr)==(0,b'',b''),(major,label,level,result)
            print(f'LLVM{major} {label} O{level}: 4096 wide-vector calls return 147456 with 64 KiB stack PASS',flush=True)

    # Without AVX the backend splits this raw vector into register arguments.
    # The C callee's attempted stack read has no incoming overflow object;
    # a probe must not invent one by treating the vector as a stack byval.
    register=TMP/f'register-{major}.ll'
    register.write_text(caller.read_text().replace('+avx','+sse2,-avx').replace('4096','1').replace('147456','36'))
    raw=TMP/f'register-closed-{major}.bc';adapted=TMP/f'register-checked-{major}.bc'
    native=TMP/f'register-native-{major}.bc';exe=TMP/f'register-{major}'
    run(llvm/'llvm-link',callee,register,dist/'bennett-runtimes.bc',layout,'-o',raw)
    run(ROOT/'scripts/native-check',major,raw,adapted)
    run(llvm/'llvm-link',adapted,'--override='+str(dist/'musl-sysroot/lib/native-support.bc'),'-o',native)
    for level in (0,2):
        exe=TMP/f'register-{major}-O{level}'
        run(llvm/'clang',f'-O{level}','-nostdlib','-static','-no-pie','-ffreestanding','-fno-builtin','-Wl,-e,_start',native,'-o',exe)
        result=subprocess.run([exe],capture_output=True,timeout=600,preexec_fn=stack_fixture)
        assert (result.returncode,result.stdout,result.stderr)==(-signal.SIGILL,b'',b'bennett runtime fault 7\n'),(major,level,result)
        print(f'LLVM{major} O{level}: register-split vector rejects missing overflow storage PASS',flush=True)
