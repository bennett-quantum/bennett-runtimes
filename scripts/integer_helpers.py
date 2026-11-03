"""Producer-internal body emission; consumer delivery remains a spec contract."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
UNARY={'ctpop','bitreverse','bswap','ctlz','cttz'}
FLAGS={'ctlz','cttz'}
FIXED={'smul_fix','umul_fix','smul_fix_sat','umul_fix_sat','sdiv_fix','udiv_fix','sdiv_fix_sat','udiv_fix_sat'}
OVERFLOW={'smul_with_overflow','umul_with_overflow'}
WIDE_ONLY={'mul','udiv','sdiv','urem','srem','shl','lshr','ashr'}
def operations():
    text=(ROOT/'src/runtime/integer.h').read_text()
    return {name.lower():int(number) for name,number in re.findall(r'^#define BRT_INT_(\w+) (\d+)$',text,re.M)}
def generate(widths,triple,layout):
    lines=['source_filename = "bennett-runtimes/integer-helpers"',
           f'target triple = "{triple}"',f'target datalayout = "{layout}"',
           'declare i32 @__bennett_rt_integer_op(ptr,ptr,ptr,ptr,i32,i32,i32)']
    for width in sorted(set(widths)):
        if not isinstance(width,int) or not 1<=width<=8388608:
            raise ValueError('width must be an admitted LLVM integer width')
        limbs=(width+63)//64
        for name,op in operations().items():
            if name=='bswap' and width%16: continue
            if name in WIDE_ONLY and width<=128: continue
            if name in OVERFLOW and not 64<width<=128: continue
            nargs=1 if name in UNARY else 3 if name in ('fshl','fshr') else 2
            extra=['i1 %flag'] if name in FLAGS else ['i32 %flag'] if name in FIXED else []
            if width>128:
                args=['ptr %dst']+['ptr %'+a for a in 'abc'[:nargs]]+extra
                symbol=f'__bennett_rt_i{width}_{name}'
                lines.append(f'define void @{symbol}({", ".join(args)}) {{')
                pointers=['%'+a for a in 'abc'[:nargs]]+['null']*(3-nargs)
                destination='%dst'
            else:
                args=[f'i{width} %{a}' for a in 'abc'[:nargs]]+extra
                symbol=f'__bennett_rt_{name}_i{width}'
                result_type=f'{{ i{width}, i1 }}' if name in OVERFLOW else f'i{width}'
                lines.append(f'define {result_type} @{symbol}({", ".join(args)}) {{')
                lines.append(f'  %dst = alloca [{limbs} x i64], align 8')
                pointers=[]; destination='%dst'
                for a in 'abc'[:nargs]:
                    lines.extend([f'  %{a}.slot = alloca [{limbs} x i64], align 8',
                                  f'  store [{limbs} x i64] zeroinitializer, ptr %{a}.slot, align 8',
                                  f'  store i{width} %{a}, ptr %{a}.slot, align 8'])
                    pointers.append('%'+a+'.slot')
                pointers+=['null']*(3-nargs)
            flag='0'
            if name in FLAGS:
                lines.append('  %flags = zext i1 %flag to i32'); flag='%flags'
            elif name in FIXED: flag='%flag'
            lines.append(f'  %status = call i32 @__bennett_rt_integer_op(ptr {destination},'+
                         ','.join('ptr '+p for p in pointers)+f',i32 {width},i32 {op},i32 {flag})')
            if width>128: lines.append('  ret void')
            else:
                lines.append(f'  %result = load i{width}, ptr %dst, align 8')
                if name in OVERFLOW:
                    lines.extend(['  %overflow = icmp ne i32 %status,0',
                        f'  %r0 = insertvalue {result_type} zeroinitializer,i{width} %result,0',
                        f'  %r1 = insertvalue {result_type} %r0,i1 %overflow,1',f'  ret {result_type} %r1'])
                else: lines.append(f'  ret i{width} %result')
            lines.append('}')
    return '\n'.join(lines)+'\n'
