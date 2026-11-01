#!/usr/bin/env python3
"""Strict O0 compiler-rt subset: golden outputs, real reports, ASan refusal."""
from pathlib import Path
import hashlib
import json
import os
import resource
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TMP=ROOT/'.tmp/sanitizer-tests'; TMP.mkdir(parents=True,exist_ok=True)
CORPUS=Path(os.environ.get('BENNETT_CONFORMANCE_ROOT',ROOT.parent/'bennett-conformance'))/'ladder'
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
GOLDEN=b"basic 0 0\nbasic 1 23131069394\nbasic 7 138063414195\nbasic 64 1083288249908\nbasic 1000 54380828142828\ngrow 0 4\ngrow 1 23131069404\ngrow 7 138063414247\ngrow 64 1083288250354\ngrow 1000 54380828149874\n"
def run(*args):
    try:
        return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f'{e.cmd}: {e.stderr.decode(errors="replace")}') from e
for m in (18,23):
    llvm=Path(f'/usr/lib/llvm-{m}/bin'); dist=ROOT/f'dist/llvm{m}'
    pub=json.loads((dist/'manifest.json').read_text())
    assert 'sanitizers' in pub, f'LLVM {m}: missing sanitizer publication'
    san=pub['sanitizers']
    assert san['archive_undefined_imports']==['abort','strlen','write']
    for name,digest in san['artifacts'].items():
        assert hashlib.sha256((dist/name).read_bytes()).hexdigest()==digest
    archive=dist/'libclang_rt.ubsan_minimal-x86_64.a'
    imports=sorted(set(line.split()[-1] for line in run(llvm/'llvm-nm','--undefined-only',archive).decode().splitlines() if ' U ' in line))
    assert imports==san['archive_undefined_imports'],imports
    assert set(line.split()[-1] for line in run(llvm/'llvm-nm','--undefined-only',dist/'sanitizer-support.bc').decode().splitlines())=={'__bennett_nz_fault','__bennett_rt_native_os'}
    flags=['--target=x86_64-unknown-linux-musl','-nostdinc','-isystem',dist/'musl-sysroot/include','-O0','-ffreestanding','-fno-builtin','-fno-pie']
    layout_c=TMP/f'layout-{m}.c'; layout_c.write_text('extern char heap_base; const unsigned long __bennett_rt_native_layout[3]={1,(unsigned long)&heap_base,1048576};\n')
    layout=TMP/f'layout-{m}.bc'; run(llvm/'clang',*flags,'-emit-llvm','-c',layout_c,'-o',layout)
    script=TMP/f'comparison-{m}.ld'
    script.write_text('''PHDRS { data PT_LOAD FLAGS(6); code PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x18000000;
 .rodata : { *(.rodata .rodata.*) } :data
 .data : { *(.data .data.*) *(.init_array*) *(.fini_array*) } :data
 .bss : { *(.bss .bss.*) *(COMMON) } :data
 heap_base = ALIGN(.,4096);
 . = ALIGN(heap_base + 1048576,2097152);
 .text : { *(.text .text.*) } :code
 /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
''')
    cases=[('hashtable',[CORPUS/'rung-2/fixtures/hashtable.c',CORPUS/'rung-2/fixtures/main_golden.c'],GOLDEN),('hello',[CORPUS/'rung-3/fixtures/hello.c'],b'hello, world\n'),('bounds',[ROOT/'tests/sanitizer_bounds.c'],b'')]
    for label,sources,expected in cases:
        for variant in ('plain','ubsan-minimal'):
            bcs=[]
            for i,s in enumerate(sources):
                bc=TMP/f'{label}-{variant}-{i}-{m}.bc'
                instrumentation=[] if variant=='plain' else ['-fsanitize=undefined','-fsanitize-minimal-runtime','-fno-sanitize-recover=all']
                run(llvm/'clang',*flags,*instrumentation,'-emit-llvm','-c',s,'-o',bc); bcs.append(bc)
            source=TMP/f'{label}-{variant}-{m}.bc'; run(llvm/'llvm-link',*bcs,'-o',source)
            exe=TMP/f'{label}-{variant}-{m}'
            run(ROOT/'scripts/native-sanitizer-link',m,variant,source,exe,layout,script)
            result=subprocess.run([exe],capture_output=True,timeout=10)
            if label=='bounds' and variant=='ubsan-minimal':
                assert result.returncode==-6 and b'ubsan: out-of-bounds' in result.stderr,result
            else:
                assert result.returncode==0 and result.stdout==expected and result.stderr==b'',(m,label,variant,result)
            assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
            assert b'NEEDED' not in run('readelf','-d',exe)
            assert b'EXEC (Executable file)' in run('readelf','-h',exe)
    # No accepting/quietly downgrading a request for ASan.
    fail=subprocess.run([ROOT/'scripts/native-sanitizer-link',str(m),'asan',source,exe,layout],capture_output=True)
    assert fail.returncode and b'unsupported' in fail.stderr,fail
    # Capture the actual strict compiler-rt ASan link failure as reproducible evidence.
    obj=TMP/f'asan-{m}.o'
    run(llvm/'clang',*flags,'-fsanitize=address','-c',ROOT/'tests/sanitizer_bounds.c','-o',obj)
    archive=Path(f'/usr/lib/llvm-{m}/lib/clang/{m}/lib/linux/libclang_rt.asan-x86_64.a')
    fail=subprocess.run(list(map(str,[llvm/'clang','--target=x86_64-unknown-linux-musl','-O0','-nostdlib','-nostdinc','-static','-no-pie','-Wl,-e,_start','-Wl,--no-undefined','-Wl,-T,'+str(script),obj,layout,dist/'bennett-runtimes-native.bc','-Wl,--whole-archive',archive,'-Wl,--no-whole-archive','-o',TMP/f'unsupported-asan-{m}'])),capture_output=True)
    (TMP/f'asan-link-{m}.log').write_bytes(fail.stderr)
    assert fail.returncode and b'undefined reference' in fail.stderr and b'_DYNAMIC' in fail.stderr and b'dlsym' in fail.stderr,fail
    print(f'LLVM {m}: O0 plain/UBSan minimal hashtable ten golden cases + hello PASS; bounds plain silent, UBSan SIGABRT/report PASS; ASan strict link unavailable (recorded)')
