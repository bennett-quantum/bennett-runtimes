#!/usr/bin/env python3
"""Typed materialised state/record helpers, independent of host unwinding."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'control-helpers'; TMP.mkdir(parents=True,exist_ok=True)
def run(*args):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120)
    assert r.returncode==0,(args,r)
    return r.stdout
source='''@__bennett_rt_eh = external global {i32,i32,ptr}, align 8
@a = global i64 0
@b = global i64 0
@jump = global {ptr,i32} zeroinitializer
@trampoline = global [26 x i8] c"\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5\\A5"
declare i32 @__bennett_rt_eh_typeid(ptr)
declare void @__bennett_rt_eh_resume({ptr,i32})
declare void @__bennett_rt_setjmp(ptr,ptr,i32)
declare void @__bennett_rt_longjmp(ptr,i32)
declare void @__bennett_rt_tramp_init(ptr,ptr,ptr)
define i32 @main() {
 %ia=call i32 @__bennett_rt_eh_typeid(ptr @a)
 %again=call i32 @__bennett_rt_eh_typeid(ptr @a)
 %ib=call i32 @__bennett_rt_eh_typeid(ptr @b)
 %stable=icmp eq i32 %ia,%again
 %distinct=icmp ne i32 %ia,%ib
 %nonzero=icmp ne i32 %ia,0
 %good0=and i1 %stable,%distinct
 %good1=and i1 %good0,%nonzero
 call void @__bennett_rt_setjmp(ptr @jump,ptr @a,i32 9)
 %frame=load ptr,ptr @jump
 %sitep=getelementptr {ptr,i32},ptr @jump,i32 0,i32 1
 %site=load i32,ptr %sitep
 %frameok=icmp eq ptr %frame,@a
 %siteok=icmp eq i32 %site,9
 %good2=and i1 %frameok,%siteok
 %pair0=insertvalue {ptr,i32} zeroinitializer,ptr @b,0
 %pair1=insertvalue {ptr,i32} %pair0,i32 %ib,1
 call void @__bennett_rt_eh_resume({ptr,i32} %pair1)
 %active=load i32,ptr @__bennett_rt_eh
 %selectorp=getelementptr {i32,i32,ptr},ptr @__bennett_rt_eh,i32 0,i32 1
 %objectp=getelementptr {i32,i32,ptr},ptr @__bennett_rt_eh,i32 0,i32 2
 %selector=load i32,ptr %selectorp
 %object=load ptr,ptr %objectp
 %is1=icmp eq i32 %active,1
 %issel=icmp eq i32 %selector,%ib
 %isobj=icmp eq ptr %object,@b
 %good3=and i1 %is1,%issel
 %good4=and i1 %good3,%isobj
 call void @__bennett_rt_longjmp(ptr @jump,i32 0)
 %jumpactive=load i32,ptr @__bennett_rt_eh
 %jumpval=load i32,ptr %selectorp
 %jumpobject=load ptr,ptr %objectp
 %is2=icmp eq i32 %jumpactive,2
 %isone=icmp eq i32 %jumpval,1
 %isjump=icmp eq ptr %jumpobject,@jump
 %good5=and i1 %is2,%isone
 %good6=and i1 %good5,%isjump
 %t=getelementptr i8,ptr @trampoline,i64 1
 call void @__bennett_rt_tramp_init(ptr %t,ptr @main,ptr @a)
 %tag=load i32,ptr %t,align 1
 %entryp=getelementptr {i32,ptr,ptr},ptr %t,i32 0,i32 1
 %nestp=getelementptr {i32,ptr,ptr},ptr %t,i32 0,i32 2
 %entry=load ptr,ptr %entryp,align 1
 %nest=load ptr,ptr %nestp,align 1
 %istag=icmp eq i32 %tag,1111838292
 %isentry=icmp eq ptr %entry,@main
 %isnest=icmp eq ptr %nest,@a
 %guardp=getelementptr i8,ptr @trampoline,i64 25
 %guard1=load i8,ptr @trampoline
 %guard2=load i8,ptr %guardp
 %isguard1=icmp eq i8 %guard1,-91
 %isguard2=icmp eq i8 %guard2,-91
 %good7=and i1 %istag,%isentry
 %good8=and i1 %good7,%isnest
 %good9=and i1 %isguard1,%isguard2
 %ok0=and i1 %good1,%good2
 %ok1=and i1 %good4,%good6
 %ok2=and i1 %good8,%good9
 %ok3=and i1 %ok0,%ok1
 %ok=and i1 %ok3,%ok2
 %bad=xor i1 %ok,true
 %r=zext i1 %bad to i32
 ret i32 %r
}
'''
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin')
    module=TMP/f'control-{major}.ll'; module.write_text(source)
    run(llvm/'opt','-passes=verify','-disable-output',module)
    layout=TMP/f'layout-{major}.ll'; exe=TMP/f'control-{major}'
    layout.write_text('@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 654315520,i64 1048576}, align 8\n')
    run(ROOT/'scripts/native-check-link',major,module,exe,layout)
    assert run(exe)==b''
    print(f'LLVM{major}: typed EH state/type IDs/resume, jump-buffer/nonlocal state, unaligned trampoline record and guards PASS',flush=True)
