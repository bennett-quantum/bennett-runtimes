#!/usr/bin/env python3
"""Actual typed helpers and internal body emitter vs independent Python integers.

The shared component fixture has no host imports. Exact LLVM adapter signatures
avoid imposing a C ABI on arbitrary-width integers. This does not define an
external instance-delivery protocol or claim full N22/N27 integration.
"""
import ctypes
import hashlib
import importlib.util
import os
from pathlib import Path
import random
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ['TMPDIR'])/'integer-helpers'; TMP.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('integer_helpers',ROOT/'scripts/integer_helpers.py')
helper=importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
def run(*args):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=180)
    assert r.returncode==0,(args,r)
    return r.stdout
def signed(x,w): return x-(1<<w) if x>>(w-1) else x
def expected(op,w,a,b,c,flag):
    mask=(1<<w)-1
    if op=='mul': return a*b&mask
    if op in ('udiv','sdiv','urem','srem'):
        if not b: return None
        aa,bb=(signed(a,w),signed(b,w)) if op[0]=='s' else (a,b)
        if op[0]=='s' and aa==-(1<<(w-1)) and bb==-1: return None
        q=(abs(aa)//abs(bb))*(-1 if (aa<0)!=(bb<0) else 1)
        return (aa-q*bb if op.endswith('rem') else q)&mask
    if op in helper.OVERFLOW:
        value=signed(a,w)*signed(b,w) if op[0]=='s' else a*b
        lo,hi=(-(1<<(w-1)),(1<<(w-1))-1) if op[0]=='s' else (0,mask)
        return value&mask,int(not lo<=value<=hi)
    if op in helper.FIXED:
        aa,bb=(signed(a,w),signed(b,w)) if op[0]=='s' else (a,b)
        if 'mul' in op: value=aa*bb>>flag
        else:
            if not bb: return None
            value=((abs(aa)<<flag)//abs(bb))*(-1 if (aa<0)!=(bb<0) else 1)
        lo,hi=(-(1<<(w-1)),(1<<(w-1))-1) if op[0]=='s' else (0,mask)
        if op.endswith('_sat'): value=max(lo,min(hi,value))
        elif 'div' in op and not lo<=value<=hi: return None
        return value&mask
    if op=='ctpop': return a.bit_count()
    if op=='ctlz': return 0 if not a and flag else w-a.bit_length()
    if op=='cttz': return 0 if not a and flag else (a&-a).bit_length()-1 if a else w
    if op=='bitreverse': return int(f'{a:0{w}b}'[::-1],2)
    if op=='bswap': return int.from_bytes(a.to_bytes(w//8,'little'),'big')
    if op in ('pdep','pext'):
        result=j=0
        for i in range(w):
            if b>>i&1:
                result|=((a>>j&1)<<i) if op=='pdep' else ((a>>i&1)<<j)
                j+=1
        return result
    if op=='clmul':
        result=0
        for i in range(w):
            if b>>i&1: result^=a<<i
        return result&mask
    if op=='fshl': return (((a<<w)|b)<<(c%w)>>w)&mask
    if op=='fshr': return (((a<<w)|b)>>(c%w))&mask
    if b>=w: return 0
    if op=='shl': return (a<<b)&mask
    if op=='lshr': return a>>b
    if op=='ashr': return (signed(a,w)>>b)&mask
    if op=='ushl_sat': return min(a<<b,mask)
    if op=='sshl_sat': return max(-(1<<(w-1)),min((1<<(w-1))-1,signed(a,w)<<b))&mask
    raise AssertionError(op)
widths=list(range(1,17))+[24,31,32,63,64,65,127,128,129,192,257,521]
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); publication=ROOT/f'dist/llvm{major}/bennett-runtimes.bc'
    before=hashlib.sha256(publication.read_bytes()).digest()
    ir=run(llvm/'llvm-dis',publication,'-o','-').decode()
    triple=re.search(r'target triple = "([^"]+)"',ir)[1]
    layout=re.search(r'target datalayout = "([^"]+)"',ir)[1]
    missing=[w for w in widths if f'@__bennett_rt_ctpop_i{w}(' not in ir and f'@__bennett_rt_i{w}_ctpop(' not in ir]
    emitted=TMP/f'instances-{major}.ll'; emitted.write_text(helper.generate(missing,triple,layout))
    adapters=[]; cases=[]
    for w in widths:
        for op in helper.operations():
            if op=='bswap' and w%16: continue
            if op in helper.WIDE_ONLY and w<=128: continue
            if op in helper.OVERFLOW and w<=64: continue
            n=1 if op in helper.UNARY else 3 if op in ('fshl','fshr') else 2
            args=['ptr %d','ptr %a','ptr %b','ptr %c','i32 %flag']
            adapters.append(f'define i32 @test_{op}_{w}({",".join(args)}) {{')
            if op in helper.OVERFLOW and w>128:
                # N27's wide arithmetic aggregate is legalised by N22. Check
                # its emitted typed definition separately, then test the same
                # arithmetic kernel/flag with a limb adapter before N22.
                assert re.search(r'define \{ i'+str(w)+r', i1 \} @__bennett_rt_'+op+'_i'+str(w)+r'\(',emitted.read_text())
                adapters.extend([f' %status=call i32 @__bennett_rt_integer_op(ptr %d,ptr %a,ptr %b,ptr null,i32 {w},i32 {helper.operations()[op]},i32 0)',
                                 ' ret i32 %status','}'])
                cases.append((op,w,n)); continue
            extra=[]
            if op in helper.FLAGS:
                adapters.append(' %z=icmp ne i32 %flag,0'); extra=['i1 %z']
            elif op in helper.FIXED: extra=['i32 %flag']
            if w<=128:
                for a in 'abc'[:n]: adapters.append(f' %{a}.v=load i{w},ptr %{a},align 1')
                params=[f'i{w} %{a}.v' for a in 'abc'[:n]]+extra
                result_type=f'{{ i{w}, i1 }}' if op in helper.OVERFLOW else f'i{w}'
                adapters.append(f' %r=call {result_type} @__bennett_rt_{op}_i{w}({",".join(params)})')
                if op in helper.OVERFLOW:
                    adapters.extend([f' %v=extractvalue {result_type} %r,0',f' %o=extractvalue {result_type} %r,1',' %status=zext i1 %o to i32'])
                adapters.append(f' store i{w} '+('%v' if op in helper.OVERFLOW else '%r')+',ptr %d,align 1')
                declaration=f'declare {result_type} @__bennett_rt_{op}_i{w}('+','.join([f'i{w}']*n+(['i1'] if op in helper.FLAGS else ['i32'] if op in helper.FIXED else []))+')'
            else:
                params=['ptr %d']+['ptr %'+a for a in 'abc'[:n]]+extra
                adapters.append(f' call void @__bennett_rt_i{w}_{op}({",".join(params)})')
                declaration=f'declare void @__bennett_rt_i{w}_{op}('+','.join(['ptr']*(n+1)+(['i1'] if op in helper.FLAGS else ['i32'] if op in helper.FIXED else []))+')'
            adapters+=[' ret i32 '+('%status' if op in helper.OVERFLOW else '0'),'}',declaration]; cases.append((op,w,n))
    adapter=TMP/f'adapters-{major}.ll'
    adapter.write_text(f'target triple = "{triple}"\ntarget datalayout = "{layout}"\n'+
        '\n'.join(adapters)+'\ndeclare i32 @__bennett_rt_integer_op(ptr,ptr,ptr,ptr,i32,i32,i32)\ndeclare void @llvm.trap()\ndefine void @__bennett_nz_fault(i64 %c) { call void @llvm.trap()\n unreachable }\n')
    linked=TMP/f'linked-{major}.bc'; library=TMP/f'helpers-{major}.so'
    run(llvm/'llvm-link',adapter,emitted,publication,'--only-needed','-o',linked)
    run(llvm/'clang','-O2','-fPIC','-nostdlib','-shared','-Wl,--no-undefined,-Bsymbolic',linked,'-o',library)
    assert not run(llvm/'llvm-nm','--undefined-only',library).strip()
    assert b'NEEDED' not in run('readelf','-d',library)
    lib=ctypes.CDLL(str(library)); rng=random.Random(0xB377E77); count=0
    for op,w,n in cases:
        f=getattr(lib,f'test_{op}_{w}'); f.argtypes=[ctypes.c_void_p]*4+[ctypes.c_uint32]; f.restype=ctypes.c_uint32
        mask=(1<<w)-1; limbs=(w+63)//64; array=ctypes.c_uint64*limbs
        values=list(range(1<<w)) if w<=4 else [0,1,2,mask,mask-1,1<<(w-1)]+[rng.getrandbits(w) for _ in range(25)]
        triples=[(a,0,0) for a in values] if n==1 else [(a,b,0) for a in values for b in values] if w<=4 and n==2 else [(a,rng.getrandbits(w),rng.getrandbits(w)) for a in values]
        if n>=2: triples += [(a,b,c) for a in values[:6] for b in (0,1,w-1,w,w+1,mask) if b<=mask for c in (0,1,w,mask)]
        for a,b,c in triples:
            aa=array(*[(a>>(64*k))&((1<<64)-1) for k in range(limbs)])
            bb=array(*[(b>>(64*k))&((1<<64)-1) for k in range(limbs)])
            cc=array(*[(c>>(64*k))&((1<<64)-1) for k in range(limbs)])
            if w%64:
                padding=((1<<64)-1)^((1<<(w%64))-1)
                aa[-1]|=padding; bb[-1]|=padding; cc[-1]|=padding
            original_inputs=(tuple(aa),tuple(bb),tuple(cc))
            for flag in (0,1) if op in helper.FLAGS else sorted({0,1,w//2,w-1,w}) if op in helper.FIXED else (0,):
                prediction=expected(op,w,a,b,c,flag)
                if prediction is None: continue
                out=(ctypes.c_uint64*(limbs+2))(*([0xa5a5a5a5a5a5a5a5]*(limbs+2)))
                status=f(ctypes.byref(out,8),aa,bb,cc,flag)
                actual=sum(out[k+1]<<(64*k) for k in range(limbs))&mask
                predicted_value,predicted_flag=prediction if op in helper.OVERFLOW else (prediction,0)
                assert (actual,status)==(predicted_value,predicted_flag),(major,op,w,a,b,c,flag,actual,status,prediction)
                assert (tuple(aa),tuple(bb),tuple(cc))==original_inputs
                assert out[0]==out[-1]==0xa5a5a5a5a5a5a5a5
                count+=1
    assert hashlib.sha256(publication.read_bytes()).digest()==before
    print(f'LLVM{major}: {count} exact typed scalar/limb-slot cases across {len(widths)} widths, independent integer oracle and no-host component closure PASS',flush=True)
    # The largest admitted LLVM width remains an ordinary generated body,
    # rather than a prebuilt-cache cap. Execute a linear-time operation there.
    maximum=8388608; maximum_limbs=maximum//64
    emitted.write_text(helper.generate([maximum],triple,layout))
    adapter.write_text(f'''target triple = "{triple}"
target datalayout = "{layout}"
declare void @__bennett_rt_i{maximum}_ctpop(ptr,ptr)
declare void @llvm.trap()
define void @__bennett_nz_fault(i64 %c) {{ call void @llvm.trap()
 unreachable }}
define void @test_maximum(ptr %d,ptr %a) {{
 call void @__bennett_rt_i{maximum}_ctpop(ptr %d,ptr %a)
 ret void
}}
''')
    maximum_library=TMP/f'maximum-{major}.so'
    run(llvm/'llvm-link',adapter,emitted,publication,'--only-needed','-o',linked)
    run(llvm/'clang','-O2','-fPIC','-nostdlib','-shared','-Wl,--no-undefined,-Bsymbolic',linked,'-o',maximum_library)
    assert not run(llvm/'llvm-nm','--undefined-only',maximum_library).strip()
    maximum_module=ctypes.CDLL(str(maximum_library)); f=maximum_module.test_maximum
    f.argtypes=[ctypes.c_void_p,ctypes.c_void_p]; f.restype=None
    values=(ctypes.c_uint64*maximum_limbs)(*([0x5555555555555555]*maximum_limbs))
    result=(ctypes.c_uint64*(maximum_limbs+2))()
    result[0]=result[-1]=0xa5a5a5a5a5a5a5a5
    f(ctypes.byref(result,8),values)
    assert result[1]==maximum//2 and not any(result[2:-1])
    assert result[0]==result[-1]==0xa5a5a5a5a5a5a5a5
    print(f'LLVM{major}: width{maximum} ctpop body executes with exact full-width result and bounds PASS',flush=True)
