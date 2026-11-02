#!/usr/bin/env python3
"""Exact implicit byval objects, caller reads and callee lifetimes at O0/O2."""
import hashlib
import os
from pathlib import Path
import resource
import signal
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'native-byval'; TMP.mkdir(parents=True,exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(*args):
    try: return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace')); raise
CASES={
 'copy-and-caller-live':('''%r = call i64 @copy(ptr byval(%V) align 8 %local)
 %old = load i64, ptr %local, align 8
 %a = icmp eq i64 %r, 87
 %b = icmp eq i64 %old, 17
 %ok = and i1 %a, %b''',0),
 'nested':('''%r = call i64 @nested(ptr byval(%V) align 8 %local)
 %ok = icmp eq i64 %r, 87''',0),
 'mixed-alloca':('''%r = call i64 @mixed(ptr byval(%V) align 8 %local)
 %ok = icmp eq i64 %r, 96''',0),
 'padding':('''%r = call i8 @padding(ptr byval(%Pad) align 1 @bytes)
 %old = load i8, ptr getelementptr ([16 x i8],ptr @bytes,i64 0,i64 15), align 1
 %a = icmp eq i8 %r, 42
 %b = icmp eq i8 %old, 7
 %ok = and i1 %a, %b''',0),
 'two-objects':('''%r = call i64 @two(ptr byval(%V) align 8 %local,ptr byval(i8) align 1 @tiny)
 %ok = icmp eq i64 %r, 26''',0),
 'unaligned-source':('''%heap = call ptr @__bennett_nz_alloc(i64 25)
 %p = getelementptr i8, ptr %heap, i64 1
 store i64 17, ptr %p, align 1
 %p1 = getelementptr i8, ptr %p, i64 8
 store i64 29, ptr %p1, align 1
 %p2 = getelementptr i8, ptr %p, i64 16
 store i64 41, ptr %p2, align 1
 %r = call i64 @copy(ptr byval(%V) align 1 %p)
 %ok = icmp eq i64 %r, 87''',0),
 'empty':('''call void @empty(ptr byval(%Empty) null)
 %ok = icmp eq i1 true, true''',0),
 'callee-oob':('''call void @overrun(ptr byval(%Pad) align 1 @bytes)
 %ok = icmp eq i1 true, true''',7),
 'separate-object-bounds':('''call void @small_overrun(ptr byval(%V) align 8 %local,ptr byval(i8) align 1 @tiny)
 %ok = icmp eq i1 true, true''',7),
 'expired-early-return':('''call void @escape(ptr byval(%V) align 8 %local,i1 true)
 %p = load ptr, ptr @escaped, align 8
 %r = load i8, ptr %p, align 1
 %ok = icmp eq i1 true, true''',7),
 'expired-other-return':('''call void @escape(ptr byval(%V) align 8 %local,i1 false)
 %p = load ptr, ptr @escaped, align 8
 %r = load i8, ptr %p, align 1
 %ok = icmp eq i1 true, true''',7),
 'source-null':('''%r = call i64 @copy(ptr byval(%V) null)
 %ok = icmp eq i1 true, true''',7),
 'source-short-global':('''%r = call i64 @copy(ptr byval(%V) @tiny)
 %ok = icmp eq i1 true, true''',7),
 'source-short-heap':('''%p = call ptr @__bennett_nz_alloc(i64 23)
 %r = call i64 @copy(ptr byval(%V) %p)
 %ok = icmp eq i1 true, true''',7),
 'source-dead':('''%p = call ptr @__bennett_nz_alloc(i64 24)
 call void @__bennett_nz_free(ptr %p)
 %r = call i64 @copy(ptr byval(%V) %p)
 %ok = icmp eq i1 true, true''',7),
 'empty-oob':('''call void @empty_overrun(ptr byval(%Empty) null)
 %ok = icmp eq i1 true, true''',7),
}
body='''target triple = "x86_64-unknown-linux-musl"
target datalayout = "e-p:64:64-i64:64-i128:128-n8:16:32:64-S128"
%V = type {i64,i64,i64}
%Pad = type {i64,i8}
%Empty = type {}
@tiny = global i8 9, align 1
@bytes = global [16 x i8] c"\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07\\07", align 1
@escaped = global ptr null, align 8
declare ptr @__bennett_nz_alloc(i64)
declare void @__bennett_nz_free(ptr)
define i64 @copy(ptr byval(%V) %arg) noinline {
 %a = load i64, ptr %arg, align 1
 %p1 = getelementptr i8, ptr %arg, i64 8
 %b = load i64, ptr %p1, align 1
 %p2 = getelementptr i8, ptr %arg, i64 16
 %c = load i64, ptr %p2, align 1
 store i64 99, ptr %arg, align 1
 %ab = add i64 %a,%b
 %r = add i64 %ab,%c
 ret i64 %r
}
define i64 @nested(ptr byval(%V) %arg) noinline {
 %r = call i64 @copy(ptr byval(%V) %arg)
 %a = load i64, ptr %arg, align 1
 %same = icmp eq i64 %a,17
 %result = select i1 %same,i64 %r,i64 1234
 ret i64 %result
}
define i64 @mixed(ptr byval(%V) %arg) noinline {
 %slot = alloca i8
 store i8 9, ptr %slot
 %r = call i64 @copy(ptr byval(%V) %arg)
 %s = load i8, ptr %slot
 %s64 = zext i8 %s to i64
 %result = add i64 %r,%s64
 ret i64 %result
}
define i8 @padding(ptr byval(%Pad) %arg) noinline {
 %p = getelementptr i8, ptr %arg,i64 15
 store i8 42, ptr %p,align 1
 %r = load i8, ptr %p,align 1
 ret i8 %r
}
define i64 @two(ptr byval(%V) %large,ptr byval(i8) %small) noinline {
 %a = load i64,ptr %large,align 1
 %b = load i8,ptr %small,align 1
 %bb = zext i8 %b to i64
 %r = add i64 %a,%bb
 ret i64 %r
}
define void @overrun(ptr byval(%Pad) %arg) noinline {
 %p = getelementptr i8,ptr %arg,i64 16
 store i8 1,ptr %p,align 1
 ret void
}
define void @small_overrun(ptr byval(%V) %large,ptr byval(i8) %small) noinline {
 %r = load i16,ptr %small,align 1
 ret void
}
define void @escape(ptr byval(%V) %arg,i1 %early) noinline {
 store ptr %arg,ptr @escaped,align 8
 br i1 %early,label %first,label %second
first:
 ret void
second:
 ret void
}
define void @empty(ptr byval(%Empty) %arg) noinline { ret void }
define void @empty_overrun(ptr byval(%Empty) %arg) noinline {
 store i8 1,ptr %arg,align 1
 ret void
}
define i32 @main(i32 %argc,ptr %argv) {
 %local = alloca %V,align 8
 store %V {i64 17,i64 29,i64 41},ptr %local,align 8
 %ap = getelementptr ptr,ptr %argv,i64 1
 %arg = load ptr,ptr %ap,align 8
 %which = load i8,ptr %arg,align 1
 switch i8 %which,label %bad [ CASES ]
'''
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'
    text=body.replace('CASES',' '.join(f'i8 {33+i},label %case{i}' for i in range(len(CASES))))
    for i,(label,(instructions,code)) in enumerate(CASES.items()):
        text+=f'case{i}:\n'+instructions+'\n %status = select i1 %ok,i32 0,i32 93\n ret i32 %status\n'
        # SSA names are function-wide; keep every independently derived case distinct.
        start=text.index(f'case{i}:\n')
        import re
        text=text[:start]+re.sub(r'%([A-Za-z][A-Za-z0-9]*)',
            lambda m:m[0] if m[1] in {'local','V','Pad','Empty'} else '%'+m[1]+str(i),text[start:])
    text+='bad:\n ret i32 94\n}\n'
    source=TMP/f'input-{major}.ll'; source.write_text(text)
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    layout=TMP/f'layout-{major}.ll'
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 536875008,i64 16384},align 8\n')
    closed=TMP/f'closed-{major}.bc'; checked=TMP/f'checked-{major}.bc'; native=TMP/f'native-{major}.bc'
    run(llvm/'llvm-link',dist/'bennett-runtimes.bc','--override='+str(source),layout,'-o',closed)
    run(ROOT/'scripts/native-check',major,closed,checked)
    run(llvm/'llvm-link',checked,'--override='+str(dist/'musl-sysroot/lib/native-support.bc'),'-o',native)
    for level in (0,2):
        exe=TMP/f'byval-{major}-O{level}'
        run(llvm/'clang','--target=x86_64-unknown-linux-musl',f'-O{level}','-nostdlib','-nostdinc',
            '-static','-no-pie','-ffreestanding','-fno-builtin','-Wl,-e,_start','-Wl,--no-undefined',native,'-o',exe)
        for i,(name,(_,code)) in enumerate(CASES.items()):
            r=subprocess.run([exe,chr(33+i)],capture_output=True,timeout=10)
            if code: assert r.returncode==-signal.SIGILL and r.stderr==b'bennett runtime fault 7\n',(major,level,name,r)
            else: assert r.returncode==0 and not r.stderr,(major,level,name,r)
            assert not r.stdout,(major,level,name,r)
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        assert b'NEEDED' not in run('readelf','-d',exe)
        print(f'LLVM{major} O{level}: {len(CASES)} exact byval copy/lifetime/source-range cases PASS',flush=True)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
