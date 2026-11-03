#!/usr/bin/env python3
"""Independent mandatory published-helper link and value witnesses."""
import os
import resource
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'helper-closure'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
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
''',
    'divide-zero':'''@a = global [3 x i64] [i64 7,i64 0,i64 0]
@b = global [3 x i64] zeroinitializer
@d = global [3 x i64] zeroinitializer
declare void @__bennett_rt_i129_udiv(ptr,ptr,ptr)
define i32 @main() {
 call void @__bennett_rt_i129_udiv(ptr @d,ptr @a,ptr @b)
 ret i32 99
}
''',
    'divide-overflow':'''@a = global [3 x i64] [i64 0,i64 0,i64 1]
@b = global [3 x i64] [i64 -1,i64 -1,i64 1]
@d = global [3 x i64] zeroinitializer
declare void @__bennett_rt_i129_sdiv(ptr,ptr,ptr)
define i32 @main() {
 call void @__bennett_rt_i129_sdiv(ptr @d,ptr @a,ptr @b)
 ret i32 99
}
'''}
failures=[]
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin')
    layout=TMP/f'layout-{major}.ll'
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 620761088,i64 1048576}, align 8\n')
    for name,text in modules.items():
        if name.startswith('divide-'):
            # Private emitter tests a non-cache width; public on-demand
            # delivery remains the separate pending specification decision.
            import importlib.util,re
            spec=importlib.util.spec_from_file_location('integer_helpers',ROOT/'scripts/integer_helpers.py')
            helper=importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
            ir=run(llvm/'llvm-dis',ROOT/f'dist/llvm{major}/bennett-runtimes.bc','-o','-').decode()
            text=helper.generate([129],re.search(r'target triple = "([^"]+)"',ir)[1],re.search(r'target datalayout = "([^"]+)"',ir)[1])+text
            # The emitted instance already defines this operation.
            text=re.sub(r'^declare void @__bennett_rt_i129_[us]div\(ptr,ptr,ptr\)\n','',text,flags=re.M)
        source=TMP/f'{name}-{major}.ll'; source.write_text(text)
        run(llvm/'opt','-passes=verify','-disable-output',source)
        exe=TMP/f'{name}-{major}'
        r=subprocess.run([ROOT/'scripts/native-check-link',str(major),source,exe,layout],capture_output=True,timeout=120)
        if r.returncode:
            failures.append((major,name,r.stderr.decode())); continue
        if name.startswith('divide-'):
            fault=5 if name=='divide-zero' else 6
            r=subprocess.run([exe],capture_output=True,timeout=10)
            assert r.returncode==-4 and not r.stdout and r.stderr==f'bennett runtime fault {fault}\n'.encode(),(major,name,r)
        else: assert run(exe)==b''
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    print(f'LLVM{major}: helper witness IR valid; link/value failures so far {len(failures)}',flush=True)
assert not failures,failures
