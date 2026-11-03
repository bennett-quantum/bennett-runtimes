#!/usr/bin/env python3
"""Independent mandatory published-helper link and value witnesses."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'helper-closure'; TMP.mkdir(parents=True,exist_ok=True)
def run(*args):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120)
    assert r.returncode==0,(args,r)
    return r.stdout
modules={
    'shadow-stack':'''declare void @__bennett_rt_ss_init(i64)
declare ptr @__bennett_rt_ss_push(i64,i64)
declare void @__bennett_rt_ss_pop(ptr)
define i32 @main() {
 call void @__bennett_rt_ss_init(i64 64)
 %p=call ptr @__bennett_rt_ss_push(i64 8,i64 8)
 store i64 42,ptr %p,align 8
 %v=load i64,ptr %p,align 8
 call void @__bennett_rt_ss_pop(ptr %p)
 %bad=icmp ne i64 %v,42
 %r=zext i1 %bad to i32
 ret i32 %r
}
''',
    'popcount':'''declare i32 @__bennett_rt_ctpop_i32(i32)
define i32 @main() {
 %v=call i32 @__bennett_rt_ctpop_i32(i32 257)
 %bad=icmp ne i32 %v,2
 %r=zext i1 %bad to i32
 ret i32 %r
}
'''}
failures=[]
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin')
    layout=TMP/f'layout-{major}.ll'
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 620761088,i64 1048576}, align 8\n')
    for name,text in modules.items():
        source=TMP/f'{name}-{major}.ll'; source.write_text(text)
        run(llvm/'opt','-passes=verify','-disable-output',source)
        exe=TMP/f'{name}-{major}'
        r=subprocess.run([ROOT/'scripts/native-check-link',str(major),source,exe,layout],capture_output=True,timeout=120)
        if r.returncode:
            failures.append((major,name,r.stderr.decode())); continue
        assert run(exe)==b''
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    print(f'LLVM{major}: helper witness IR valid; link/value failures so far {len(failures)}',flush=True)
assert not failures,failures
