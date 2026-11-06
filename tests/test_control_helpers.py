#!/usr/bin/env python3
"""Typed materialised state/record helpers, independent of host unwinding."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'control-helpers'; TMP.mkdir(parents=True,exist_ok=True)
source='''@__bennett_rt_tls = external global i8
@__bennett_rt_reg_sp = external global i64
@__stack_chk_guard = external global i64
declare ptr @__bennett_rt_thread_pointer()
declare void @__bennett_rt_ss_init(i64)
declare ptr @__bennett_rt_ss_push(i64,i64)
declare void @__bennett_rt_ss_pop(ptr)
@__bennett_rt_eh = external global {i32,i32,ptr}, align 8
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
 call void @__bennett_rt_ss_init(i64 64)
 %fp=call ptr @__bennett_rt_ss_push(i64 8,i64 8)
 %baseint=ptrtoint ptr %fp to i64
 %end=add i64 %baseint,8
 %sp=load i64,ptr @__bennett_rt_reg_sp
 %spok=icmp eq i64 %sp,%end
 %tls=call ptr @__bennett_rt_thread_pointer()
 %tlsok=icmp eq ptr %tls,@__bennett_rt_tls
 %guard=load i64,ptr @__stack_chk_guard
 %guardok=icmp ne i64 %guard,0
 %env0=and i1 %spok,%tlsok
 %env=and i1 %env0,%guardok
 %ia=call i32 @__bennett_rt_eh_typeid(ptr @a)
 %again=call i32 @__bennett_rt_eh_typeid(ptr @a)
 %ib=call i32 @__bennett_rt_eh_typeid(ptr @b)
 %stable=icmp eq i32 %ia,%again
 %distinct=icmp ne i32 %ia,%ib
 %nonzero=icmp ne i32 %ia,0
 %good0=and i1 %stable,%distinct
 %good1=and i1 %good0,%nonzero
 call void @__bennett_rt_setjmp(ptr @jump,ptr %fp,i32 9)
 %frame=load ptr,ptr @jump
 %sitep=getelementptr {ptr,i32},ptr @jump,i32 0,i32 1
 %site=load i32,ptr %sitep
 %frameok=icmp eq ptr %frame,%fp
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
 %ok4=and i1 %ok3,%ok2
 %ok=and i1 %ok4,%env
 call void @__bennett_rt_ss_pop(ptr %fp)
 %bad=xor i1 %ok,true
 %r=zext i1 %bad to i32
 ret i32 %r
}
'''
for major in (18,23):
    llvm=llvm_dir(major)
    module=TMP/f'control-{major}.ll'; module.write_text(source)
    run(llvm/'opt','-passes=verify','-disable-output',module)
    layout=TMP/f'layout-{major}.ll'; exe=TMP/f'control-{major}'
    write_layout(layout, 654315520, 1048576)
    run(ROOT/'scripts/native-check-link',major,module,exe,layout)
    assert run(exe)==b''
    print(f'LLVM{major}: typed EH state/type IDs/resume, jump-buffer/nonlocal state, unaligned trampoline record and guards PASS',flush=True)
