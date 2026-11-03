#!/usr/bin/env python3
"""Deterministic major-matched publication; no timestamps or checkout paths."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import importlib.util
_module = importlib.util.spec_from_file_location('musl', Path(__file__).with_name('musl.py'))
musl = importlib.util.module_from_spec(_module)
_module.loader.exec_module(musl)

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/allocator/bump.c', 'src/libc/memory.c', 'src/allocator/language.c', 'julia-shim/alloc.c', 'src/libc/utc.c', 'src/fenv/environment.c', 'src/device/console.c', 'src/device/models.c', 'src/device/transports.c', 'src/musl/vfs.c', 'src/musl/syscall.c', 'src/runtime/shadow.c', 'src/runtime/integer_core.c', 'src/runtime/integer_arithmetic.c']
EXPORTS = ['__bennett_rt_memmove', '__bennett_rt_memset', 'calloc', 'free', 'malloc', 'memset']
FLAGS = ['-target', 'x86_64-unknown-linux-musl', '-std=c11', '-O2', '-flto',
         '-ffreestanding', '-fno-builtin', '-fno-vectorize', '-fno-slp-vectorize',
         '-fno-ident', '-Wall', '-Wextra', '-Werror']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True, cwd=ROOT)

def build(major):
    if major not in ('18', '23'):
        raise SystemExit('usage: scripts/build 18|23 [output-directory]')
    llvm = Path(f'/usr/lib/llvm-{major}/bin')
    out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT/f'dist/llvm{major}'
    out.mkdir(parents=True, exist_ok=True)
    scratch = ROOT/'.tmp'/f'build-{major}'
    scratch.mkdir(parents=True, exist_ok=True)
    files, musl_flags = musl.prepare(ROOT, out, scratch)
    objects = []
    for source in SOURCES:
        ll = scratch/(Path(source).stem+'.ll')
        run(llvm/'clang', *FLAGS, '-nostdinc', '-isystem', out/'musl-sysroot/include',
            '-S', '-emit-llvm', source, '-o', ll)
        text = ll.read_text()
        text = re.sub(r'^; ModuleID = .*\n', '', text, flags=re.M)
        text = re.sub(r'^source_filename = .*', f'source_filename = "{source}"', text, flags=re.M)
        if source=='src/fenv/environment.c':
            # C names its record type. Publish the literal LLVM object type
            # §13.5 and arith declare, retaining the same checked C layout.
            text, count = re.subn(r'(@__bennett_rt_fenv = .*?global )%struct.bennett_fenv( zeroinitializer, section "\.data\.__bennett_rt_fenv", align 4)',
                                  r'\1{ i32, i32 }\2',text)
            assert count==1, 'missing canonical fenv definition'
        # Clang C has no source attribute for LLVM allockind. §5.1 requires it;
        # add only alloc (NOT uninitialized) and allocsize to the two definitions.
        allocators={'malloc':'0','calloc':'0,1',
                    '__bennett_rt_aligned_alloc':'1','memalign':'1','aligned_alloc':'1',
                    '__rust_alloc':'0','__rust_alloc_zeroed':'0',
                    '_Znwm':'0','_Znam':'0','_ZnwmRKSt9nothrow_t':'0','_ZnamRKSt9nothrow_t':'0',
                    '_ZnwmSt11align_val_t':'0','_ZnamSt11align_val_t':'0',
                    '_ZnwmSt11align_val_tRKSt9nothrow_t':'0','_ZnamSt11align_val_tRKSt9nothrow_t':'0',
                    'jl_gc_alloc':'1','ijl_gc_alloc_typed':'1','julia.gc_alloc_obj':'1',
                    'ijl_gc_small_alloc':'2','ijl_gc_big_alloc':'1'}
        for name, indices in allocators.items():
            text = re.sub(rf'^(define .*@{name}\([^\n]*?) #',
                          rf'\1 allockind("alloc") allocsize({indices}) #',
                          text, flags=re.M)
        ll.write_text(text)
        bc = ll.with_suffix('.bc')
        run(llvm/'llvm-as', ll, '-o', bc)
        objects.append(bc)
    helper_spec=importlib.util.spec_from_file_location('integer_helpers',ROOT/'scripts/integer_helpers.py')
    helper_module=importlib.util.module_from_spec(helper_spec); helper_spec.loader.exec_module(helper_module)
    template=(scratch/'integer_core.ll').read_text()
    assert not re.search(r'@llvm\.(?:ctpop|ctlz|cttz|bswap|bitreverse|fshl|fshr)',template), 'recursive integer intrinsic in helper body'
    helper_ir=scratch/'integer-helpers.ll'
    helper_ir.write_text(helper_module.generate((1,8,16,32,64,128),
        re.search(r'target triple = "([^"]+)"',template)[1],
        re.search(r'target datalayout = "([^"]+)"',template)[1]))
    helper_bc=helper_ir.with_suffix('.bc')
    run(llvm/'llvm-as',helper_ir,'-o',helper_bc); objects.append(helper_bc)
    core = scratch/'allocator.bc'
    run(llvm/'llvm-link', *objects[:2], '-o', core)
    for i, source in enumerate(files):
        ll = scratch/f'musl-{i}.ll'
        run(llvm/'clang', *musl_flags, '-S', source.relative_to(ROOT), '-o', ll)
        text = ll.read_text()
        text = re.sub(r'^; ModuleID = .*\n', '', text, flags=re.M)
        logical = str(source.relative_to(scratch)) if source.is_relative_to(scratch) else str(source.relative_to(ROOT))
        text = re.sub(r'^source_filename = .*', f'source_filename = "{logical}"', text, flags=re.M)
        # Duplicating strings allocates a fresh object of a runtime-computed
        # extent; neither bound argument is an exact allocsize promise.
        for name in ('strdup','strndup'):
            text = re.sub(rf'^(define .*@{name}\([^\n]*?) #',
                          r'\1 allockind("alloc") #', text, flags=re.M)
        ll.write_text(text)
        bc=ll.with_suffix('.bc')
        run(llvm/'llvm-as', ll, '-o', bc)
        objects.append(bc)
    (out/'musl-sysroot/lib/bennett-allocator.bc').write_bytes(core.read_bytes())
    native_objects = []
    native_sources = ['src/native/start.c', 'src/native/heap.c', 'src/native/os.c', 'src/native/syscall.c']
    for source in native_sources:
        ll = scratch/('native-'+Path(source).stem+'.ll')
        run(llvm/'clang', *FLAGS, '-nostdinc', '-isystem', out/'musl-sysroot/include',
            '-fno-stack-protector', '-fno-pie', '-S', '-emit-llvm', source, '-o', ll)
        text = re.sub(r'^; ModuleID = .*\n', '', ll.read_text(), flags=re.M)
        text = re.sub(r'^source_filename = .*', f'source_filename = "{source}"', text, flags=re.M)
        ll.write_text(text)
        bc = ll.with_suffix('.bc')
        run(llvm/'llvm-as', ll, '-o', bc)
        native_objects.append(bc)
    (out/'musl-sysroot/lib/start.bc').write_bytes(native_objects[0].read_bytes())
    support = out/'musl-sysroot/lib/native-support.bc'
    run(llvm/'llvm-link', *native_objects, '-o', support)
    native = out/'bennett-runtimes-native.bc'
    # Replace only primitive/startup/effect transports. Ordinary syscall and
    # VFS bodies remain identical; native definitions never enter production.
    run(llvm/'llvm-link', *objects, '--override='+str(support), '-o', native)
    run(llvm/'opt', '-passes=strip-dead-prototypes,verify', native, '-o', native)
    # Offline compiler-rt input is byte-pinned, never selected from host libraries.
    sanitizer_archive = 'libclang_rt.ubsan_minimal-x86_64.a'
    archive = ROOT/f'vendor/compiler-rt/llvm{major}'/sanitizer_archive
    expected = {'18': '66e08178e5231422dbe6717cbb0bd427ee4bb678175830ce1064d9d0b50781da',
                '23': '006cb02eeb8aeda11a20fa6c0d1bdcfadcb59b924be5755f7229dfcdcb8b82d6'}
    assert sha(archive) == expected[major], 'compiler-rt digest mismatch'
    (out/sanitizer_archive).write_bytes(archive.read_bytes())
    (out/'COMPILER-RT-COPYRIGHT').write_bytes(archive.with_name('COPYRIGHT').read_bytes())
    (out/'COMPILER-RT-LICENSE').write_bytes((ROOT/'vendor/compiler-rt/LICENSE').read_bytes())
    sanitizer_support = out/'sanitizer-support.bc'
    run(llvm/'clang', *FLAGS, '-nostdinc', '-isystem', out/'musl-sysroot/include',
        '-fno-stack-protector', '-fno-pie', '-emit-llvm', '-c', 'src/native/sanitizer.c', '-o', sanitizer_support)
    artifact = scratch/'publish.bc'
    run(llvm/'llvm-link', *objects, '-o', artifact)
    run(llvm/'opt', '-passes=verify', '-disable-output', artifact)
    ir = run(llvm/'llvm-dis', artifact, '-o', '-')
    exports = sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--defined-only', '--extern-only', artifact).splitlines() if line.split())
    assert set(EXPORTS + ['printf','puts','write','exit']) <= set(exports), exports
    assert not re.search(r'^define .*@(?:__bennett_nz_|__bennett_effect_)', ir, re.M)
    manifest = {
        'implementation': 'musl-device-vfs-native-v3', 'llvm_major': int(major),
        'target': 'x86_64-unknown-linux-musl', 'dialect': '0.2.10',
        'dialect_commit': '023f5a771a814270634db3be9cfde88ded69f975',
        'isa': '0.8', 'sha256': sha(artifact), 'exports': exports,
        'effect_imports': sorted(set(re.findall(r'^declare .*?@(__bennett_effect_[^ (]+)\(', ir, re.M))),
        'undefined_imports': sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--undefined-only', artifact).splitlines() if line.split()),
        'primitive_imports': ['__bennett_nz_alloc', '__bennett_nz_fault', '__bennett_nz_free'],
        'flags': FLAGS,
        'core_extra_flags': ['-nostdinc', '-isystem <dist>/musl-sysroot/include'],
        'fenv': {'symbol': '__bennett_rt_fenv', 'llvm_type': '{ i32, i32 }', 'alignment': 4,
                 'field_offsets': {'round': 0, 'flags': 4}, 'initial': [0,0],
                 'round_codes': [0,1,2,3,4], 'ieee_flag_bits': [1,2,4,8,16]},
        'process_context': {'prepare': '__bennett_rt_prepare_process',
                            'signature': 'void (i32 argc, ptr argv, ptr envp)',
                            'profile': 'fresh static images, single thread, C locale, page size 4096',
                            'native_start': 'register input objects; prepare; exit(main(argc, argv, environ))',
                            'device_entry': 'driver prepares vectors and invokes preparation before entry; no Linux CRT/TLS/auxv'},
        'c_allocator': {'payload': 'max_align_t-rounded, primitive zero fill',
                        'requested_alignment': 'charged zero-size address probe per allocation, then minimal padding; ABI-rounded payload extent',
                        'bookkeeping': 'explicit ordinary 32-byte allocation per peak simultaneous C allocation; live pool slots reused as data',
                        'realloc': 'always move; copy min(old requested size,new size); free old after copy; zero uses unique malloc(0) policy',
                        'history': 'payload, padding and bookkeeping charged identically; no payload reuse or record cap'},
        'language_allocators': {'cpp': 'Itanium scalar/array, nothrow, aligned, sized and unsized delete',
                               'rust': 'size/alignment ABI; realloc retains requested alignment',
                               'julia': '1.13 tagged nonmoving allocations; 8-byte preceding header, 16-byte aligned payload; full size-argument forward access window also retained'},
        'musl': {'version': musl.VERSION, 'url': musl.URL, 'source_sha256': musl.SHA,
                 'patches': {p: sha(ROOT/p) for p in musl.PATCHES},
                 'files': sorted(set(musl.FILES)), 'flags': [re.sub(str(scratch), '<build>', re.sub(str(out), '<dist>', f)) for f in musl_flags]},
        'native': {
            'artifact': 'bennett-runtimes-native.bc', 'sha256': sha(native),
            'support_artifact': str(support.relative_to(out)), 'support_sha256': sha(support),
            'exports': sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--defined-only', '--extern-only', native).splitlines() if line.split()),
            'undefined_imports': sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--undefined-only', native).splitlines() if line.split()),
            'layout': {'symbol': '__bennett_rt_native_layout', 'abi_version': 1,
                       'fields': ['abi_version:u64', 'heap_base:u64', 'heap_limit_max:u64'],
                       'allocation_records': 'heap_limit_max (max(n,1) consumes at least one byte)'},
            'materialised_checks': {
                'adapter': 'scripts/native-check', 'link_adapter': 'scripts/native-check-link',
                'coverage': ['live-object loads/stores', 'memory-function ranges', 'invalid/double free',
                             'exact callee-local byval copies and complete caller source ranges',
                             'division/remainder zero and signed overflow', 'explicit fault/unreachable'],
                'preservation': 'disable libcall/allocation inference; native-check-link skips repeat IR optimisation, retaining O2 machine code generation',
                'infrastructure_exit_status': 125,
                'source_definedness': 'separate original-input classifier, not inferred from materialised checks',
            },
            'extra_flags': ['-nostdinc', '-isystem <dist>/musl-sysroot/include', '-fno-stack-protector', '-fno-pie'],
        },
        'sanitizers': {
            'variant': 'ubsan-minimal', 'asan': 'unavailable: hosted compiler-rt ASan does not close',
            'compile_flags': ['-O0', '-fsanitize=undefined', '-fsanitize-minimal-runtime', '-fno-sanitize-recover=all'],
            'artifacts': {name: sha(out/name) for name in (sanitizer_archive, 'sanitizer-support.bc', 'COMPILER-RT-COPYRIGHT', 'COMPILER-RT-LICENSE')},
            'archive_undefined_imports': sorted(set(line.split()[-1] for line in run(llvm/'llvm-nm', '--undefined-only', archive).splitlines() if ' U ' in line)),
            'provenance': f'offline vendored installed LLVM {major} compiler-rt; Apache-2.0 WITH LLVM-exception; see COMPILER-RT-COPYRIGHT',
        },
        'sysroot': {str(p.relative_to(out)): sha(p) for p in sorted((out/'musl-sysroot').rglob('*')) if p.is_file()},
        'tools': {name: run(llvm/name, '--version').splitlines()[0]
                  for name in ('clang', 'llvm-as', 'llvm-link', 'opt')},
        'sources': {s: sha(ROOT/s) for s in SOURCES +
                    ['scripts/build', 'scripts/build.py', 'scripts/musl.py', 'scripts/integer_helpers.py', 'scripts/native-link', 'scripts/native-check', 'scripts/native-check.cpp', 'scripts/native-check-link', 'scripts/native-sanitizer-link', 'src/native/sanitizer.c', 'src/musl/context.c', 'src/musl/vfs.h', 'src/runtime/shadow.h', 'src/runtime/integer.h', 'src/native/start.S', 'src/native/layout.h', *native_sources, *musl.PATCHES, 'SPEC_VERSIONS.toml']},
    }
    # Atomic file replacement prevents consumers observing partial bitcode.
    temporary = out/'.bennett-runtimes.bc.new'
    temporary.write_bytes(artifact.read_bytes())
    temporary.replace(out/'bennett-runtimes.bc')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    (out/'SHA256SUMS').write_text(''.join(f'{sha(out/name)}  {name}\n'
                                        for name in ('bennett-runtimes.bc', 'bennett-runtimes-native.bc', 'musl-sysroot/lib/native-support.bc', 'sanitizer-support.bc', sanitizer_archive, 'COMPILER-RT-COPYRIGHT', 'COMPILER-RT-LICENSE', 'manifest.json')))
    print(f'LLVM {major}: {manifest["sha256"]}  {out/"bennett-runtimes.bc"}')

if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else '')
