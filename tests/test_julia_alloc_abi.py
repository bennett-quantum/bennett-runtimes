#!/usr/bin/env python3
"""Execute Julia 1.13's emitted allocation body with the LLVM23 publication.

This checks the allocation boundary, not a complete Julia process or collector.
The installed stock Julia is a frontend oracle; no sibling implementation is used.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'julia-allocation'; TMP.mkdir(parents=True,exist_ok=True)
julia=Path(os.environ.get('BENNETT_JULIA',str(Path.home()/'.julia/juliaup/julia-1.13.3+0.x64.linux.gnu/bin/julia')))
llvm=llvm_dir(23)
from functools import partial
run = partial(run, timeout=60)
raw=TMP/'frontend.ll'
code='using InteractiveUtils; mutable struct Box; x::Int; end; f(x)=Box(x); open(ARGS[1],"w") do io; code_llvm(io,f,Tuple{Int};raw=true,dump_module=true,optimize=true); end'
run(julia,'--startup-file=no','-t1','-e',code,raw)
text=raw.read_text()
expected={'julia.gc_alloc_obj':['ptr','i64','ptr'],
          'ijl_gc_small_alloc':['ptr','i32','i32','i64'],
          'ijl_gc_big_alloc':['ptr','i64','i64'],
          'ijl_gc_alloc_typed':['ptr','i64','i64']}
def signature(ir,kind,name):
    line=next(l for l in ir.splitlines() if l.startswith(kind+' ') and '@'+name+'(' in l)
    start=line.index('@'+name+'(')+len(name)+2
    depth=0; end=start
    for end in range(start,len(line)):
        if line[end]=='(' : depth+=1
        if line[end]==')':
            if depth==0: break
            depth-=1
    args=[]; at=start; depth=0
    for i in range(start,end):
        if line[i]=='(': depth+=1
        if line[i]==')': depth-=1
        if line[i]==',' and depth==0:
            args.append(line[at:i].strip().split()[0]); at=i+1
    args.append(line[at:end].strip().split()[0])
    return args
for major in (18,23):
    ir=run(str(llvm_dir(major)/'llvm-dis'),ROOT/f'dist/llvm{major}/bennett-runtimes.bc','-o','-').decode()
    for name,args in expected.items():
        assert signature(text,'declare',name)==args,(major,name,'frontend')
        assert signature(ir,'define',name)==args,(major,name,'runtime')
# Take the actual emitted swiftcc method, retaining the real allocation call,
# header store and payload store. Debug/JIT wrapper metadata is not executable.
match=re.search(r'^(define swiftcc .*?@julia_f_\d+\([^\n]+\).*?\n\})',text,re.M|re.S)
assert match,'stock Julia did not emit the expected boxed allocation method'
body=match[1]
body=re.sub(r'@julia_f_\d+','@julia_allocation_body',body)
body=re.sub(r' #\d+','',body)
body=re.sub(r', ![A-Za-z_.]+ !\d+','',body)
body=re.sub(r' !dbg !\d+','',body)
body='\n'.join(l for l in body.splitlines() if not l.lstrip().startswith('#dbg_'))
tag=re.search(r'store atomic i64 (\d+),',body)
assert tag,'missing actual Julia type header store'
probe=TMP/'probe.ll'
probe.write_text('''@safe = global i64 0, align 8
@tls = global {i64,i64,ptr} {i64 0,i64 0,ptr @safe}, align 8
@gcstack = global {i64,i64,ptr} {i64 0,i64 0,ptr @tls}, align 8
@observed = global ptr null, align 8
declare ptr @ijl_gc_small_alloc(ptr,i32,i32,i64)
'''+body+'''
define i32 @main() {
  %p = call swiftcc ptr @julia_allocation_body(ptr swiftself @gcstack, i64 123456789)
  store volatile ptr %p, ptr @observed, align 8
  %x = load i64, ptr %p, align 8
  %h = getelementptr i8, ptr %p, i64 -8
  %t = load i64, ptr %h, align 8
  %a = icmp eq i64 %x, 123456789
  %b = icmp eq i64 %t, '''+tag[1]+'''
  %ok = and i1 %a, %b
  %r = select i1 %ok, i32 0, i32 91
  ret i32 %r
}
''')
layout=TMP/'layout.ll'
write_layout(layout, 536875008, 65536)
linked=TMP/'linked.bc'; postlink=TMP/'postlink.bc'
run(llvm/'llvm-link',probe,ROOT/'dist/llvm23/bennett-runtimes.bc','--only-needed','-o',linked)
run(llvm/'opt','-passes=lto<O3>',linked,'-o',postlink)
for name,source in [('ordinary',probe),('lto',postlink)]:
    exe=TMP/name
    run(ROOT/'scripts/native-check-link',23,source,exe,layout)
    subprocess.run([exe],check=True,timeout=10)
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
print('Julia 1.13 frontend allocation signatures LLVM18/23; actual boxed method before/after LTO native checked LLVM23 PASS')
