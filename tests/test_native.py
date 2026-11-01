#!/usr/bin/env python3
"""Published native closure: strict corpus links and resolved-layout primitives."""
from pathlib import Path
import hashlib
import json
import os
import resource
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=ROOT/'.tmp/native-tests'; TMP.mkdir(parents=True,exist_ok=True)
CORPUS=Path(os.environ.get('BENNETT_CONFORMANCE_ROOT',ROOT.parent/'bennett-conformance'))/'ladder'
ARITH=Path(os.environ.get('BENNETT_ARITH_ROOT',ROOT.parent/'bennett-arith'))
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
GOLDEN=b"basic 0 0\nbasic 1 23131069394\nbasic 7 138063414195\nbasic 64 1083288249908\nbasic 1000 54380828142828\ngrow 0 4\ngrow 1 23131069404\ngrow 7 138063414247\ngrow 64 1083288250354\ngrow 1000 54380828149874\n"
def run(*args):
    return subprocess.check_output(list(map(str,args)))
for m in (18,23):
    llvm=Path(f'/usr/lib/llvm-{m}/bin'); dist=ROOT/f'dist/llvm{m}'
    native=dist/'bennett-runtimes-native.bc'
    assert native.is_file(), f'LLVM {m}: missing published native closure'
    manifest=json.loads((dist/'manifest.json').read_text())['native']
    assert manifest['sha256']==hashlib.sha256(native.read_bytes()).hexdigest()
    assert {'_start','printf','puts','write','__bennett_nz_alloc','__bennett_nz_free','__bennett_nz_fault','__bennett_rt_native_console_write'} <= set(manifest['exports'])
    assert set(manifest['undefined_imports']) <= {'main','__bennett_rt_native_layout','__fini_array_start','__fini_array_end',
                                               '__bennett_rt_native_objects','__bennett_rt_native_object_count'}
    assert '__bennett_rt_native_layout' in manifest['undefined_imports'], 'layout must never default'
    ir=run(llvm/'llvm-dis',native,'-o','-').decode()
    assert 'define' in ir and '__bennett_effect_' not in ir, 'native must not define or import markers'
    flags=['--target=x86_64-unknown-linux-musl','-nostdinc','-isystem',dist/'musl-sysroot/include','-O2','-ffreestanding','-fno-builtin','-fno-pie']
    for label,sources,expected in [
        ('hashtable',[CORPUS/'rung-2/fixtures/hashtable.c',CORPUS/'rung-2/fixtures/main_golden.c'],GOLDEN),
        ('hello',[CORPUS/'rung-3/fixtures/hello.c'],b'hello, world\n')]:
        bcs=[]
        for i,s in enumerate(sources):
            bc=TMP/f'{label}-{i}-{m}.bc'; run(llvm/'clang',*flags,'-emit-llvm','-c',s,'-o',bc); bcs.append(bc)
        source=TMP/f'{label}-{m}.bc'; run(llvm/'llvm-link',*bcs,'-o',source)
        # Two distinct resolved layouts prove the closure does not bake one in.
        for base in (0x18000000,0x28000000):
            config=TMP/f'layout-{m}.c'
            config.write_text('extern char heap_base; const unsigned long __bennett_rt_native_layout[3]={1,(unsigned long)&heap_base,1048576};\n')
            script=TMP/f'comparison-{m}.ld'
            script.write_text(f"""PHDRS {{ data PT_LOAD FLAGS(6); code PT_LOAD FLAGS(5); }}
SECTIONS {{
 . = {base};
 .rodata : {{ *(.rodata .rodata.*) }} :data
 .data : {{ *(.data .data.*) *(.init_array*) *(.fini_array*) }} :data
 .bss : {{ *(.bss .bss.*) *(COMMON) }} :data
 heap_base = ALIGN(.,4096);
 . = ALIGN(heap_base + 1048576,2097152);
 .text : {{ *(.text .text.*) }} :code
 /DISCARD/ : {{ *(.eh_frame*) *(.comment) *(.note*) }}
}}
""")
            layout=TMP/f'layout-{m}.bc'; run(llvm/'clang',*flags,'-emit-llvm','-c',config,'-o',layout)
            exe=TMP/f'{label}-{m}-{base}'
            run(ROOT/'scripts/native-link',m,source,exe,layout,script)
            result=subprocess.run([exe],capture_output=True,timeout=10)
            assert result.returncode==0 and result.stdout==expected,(m,label,base,result)
            assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
            assert b'NEEDED' not in run('readelf','-d',exe)
        # The driver already linked materialised runtime functions. Override
        # only the native support, never the materialised main/device variants.
        merged=TMP/f'materialised-shape-{label}-{m}.bc'
        supported=TMP/f'adapted-{label}-{m}.bc'
        closed=TMP/f'support-{label}-{m}.bc'
        run(llvm/'llvm-link',source,dist/'bennett-runtimes.bc',ARITH/f'dist/llvm{m}/bennett-arith.bc','--only-needed','-o',merged)
        run(llvm/'llvm-link',merged,layout,'--override='+str(dist/'musl-sysroot/lib/native-support.bc'),'-o',supported)
        # Startup introduces exit/environ/stdio-flush imports. Complete the
        # closure AFTER introducing support, as the consumer must do.
        run(llvm/'llvm-link',supported,dist/'bennett-runtimes.bc',ARITH/f'dist/llvm{m}/bennett-arith.bc','--only-needed','-o',closed)
        exe=TMP/f'support-{label}-{m}'
        run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-nostdlib','-nostdinc','-static','-no-pie','-Wl,-e,_start','-Wl,--build-id=none','-Wl,-T,'+str(script),closed,'-o',exe)
        assert run(exe)==expected
        assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
        # Missing layout is a hard undefined-symbol link error, never a default.
        fail=subprocess.run([llvm/'clang','-nostdlib','-nostdinc','-static','-no-pie',source,native,ARITH/f'dist/llvm{m}/bennett-arith.bc','-o',TMP/f'missing-{m}'],capture_output=True)
        assert fail.returncode and b'__bennett_rt_native_layout' in fail.stderr, fail
    source=TMP/f'primitives-{m}.bc'
    run(llvm/'clang',*flags,'-emit-llvm','-c',ROOT/'tests/native_contract.c','-o',source)
    for version,base,limit,code in [(1,0x1c000000,0,2),(1,0x1c000000,65537,2),
                                   (2,0x1c000000,16,7),(1,0x1c000001,16,7),
                                   (1,0,16,7),(1,0xfffffffffffff000,8192,7),
                                   (1,0x1c000000,0xffffffffffffffff,7),
                                   (1,0x400000,16,7)]:
        config=TMP/f'primitive-layout-{m}.c'
        config.write_text(f'const unsigned long __bennett_rt_native_layout[3]={{{version},{base}UL,{limit}UL}};\n')
        layout=TMP/f'primitive-layout-{m}.bc'
        run(llvm/'clang',*flags,'-emit-llvm','-c',config,'-o',layout)
        exe=TMP/f'primitives-{m}'
        run(ROOT/'scripts/native-link',m,source,exe,layout)
        result=subprocess.run([exe],capture_output=True,timeout=10)
        if code==7: # invalid layout or occupied address is host setup failure
            assert result.returncode==125 and result.stderr.startswith(b'bennett infrastructure failure: '),(m,version,base,limit,result)
        else:
            assert result.returncode==-4 and result.stderr==f'bennett runtime fault {code}\n'.encode(),(m,version,base,limit,result)
    # The heap fits but its required all-history bookkeeping cannot be mapped.
    # This is not a dialect budget-heap outcome.
    config.write_text('const unsigned long __bennett_rt_native_layout[3]={1,0x1c000000,67108864};\n')
    run(llvm/'clang',*flags,'-emit-llvm','-c',config,'-o',layout)
    run(ROOT/'scripts/native-link',m,source,exe,layout)
    def constrained(): resource.setrlimit(resource.RLIMIT_AS,(128*1024*1024,128*1024*1024))
    result=subprocess.run([exe],capture_output=True,timeout=10,preexec_fn=constrained)
    assert result.returncode==125 and result.stderr==b'bennett infrastructure failure: allocation history mapping\n',result
    for arg,code in [('f',18446744073709551615),('s',8)]:
        result=subprocess.run([exe,arg],capture_output=True,timeout=10)
        assert result.returncode==-4 and result.stderr==f'bennett runtime fault {code}\n'.encode(),result
    print(f'LLVM {m}: strict native hashtable ten cases + hello, two resolved layouts, required configuration, exports/imports PASS')
