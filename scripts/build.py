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
SOURCES = ['src/allocator/bump.c', 'src/libc/memory.c', 'src/device/console.c', 'src/musl/syscall.c']
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
    objects = []
    for source in SOURCES:
        ll = scratch/(Path(source).stem+'.ll')
        run(llvm/'clang', *FLAGS, '-S', '-emit-llvm', source, '-o', ll)
        text = ll.read_text()
        text = re.sub(r'^; ModuleID = .*\n', '', text, flags=re.M)
        text = re.sub(r'^source_filename = .*', f'source_filename = "{source}"', text, flags=re.M)
        # Clang C has no source attribute for LLVM allockind. §5.1 requires it;
        # add only alloc (NOT uninitialized) and allocsize to the two definitions.
        for name, indices in [('malloc', '0'), ('calloc', '0,1')]:
            text = re.sub(rf'^(define .*@{name}\([^\n]*?) #',
                          rf'\1 allockind("alloc") allocsize({indices}) #',
                          text, flags=re.M)
        ll.write_text(text)
        bc = ll.with_suffix('.bc')
        run(llvm/'llvm-as', ll, '-o', bc)
        objects.append(bc)
    core = scratch/'allocator.bc'
    run(llvm/'llvm-link', *objects[:2], '-o', core)
    files, musl_flags = musl.prepare(ROOT, out, scratch)
    for i, source in enumerate(files):
        ll = scratch/f'musl-{i}.ll'
        run(llvm/'clang', *musl_flags, '-S', source.relative_to(ROOT), '-o', ll)
        text = ll.read_text()
        text = re.sub(r'^; ModuleID = .*\n', '', text, flags=re.M)
        logical = str(source.relative_to(scratch)) if source.is_relative_to(scratch) else str(source.relative_to(ROOT))
        text = re.sub(r'^source_filename = .*', f'source_filename = "{logical}"', text, flags=re.M)
        ll.write_text(text)
        bc=ll.with_suffix('.bc')
        run(llvm/'llvm-as', ll, '-o', bc)
        objects.append(bc)
    (out/'musl-sysroot/lib/bennett-allocator.bc').write_bytes(core.read_bytes())
    start = scratch/'start.ll'
    run(llvm/'clang', *musl_flags, '-S', 'src/native/start.c', '-o', start)
    text = re.sub(r'^; ModuleID = .*\n', '', start.read_text(), flags=re.M)
    text = re.sub(r'^source_filename = .*', 'source_filename = "src/native/start.c"', text, flags=re.M)
    start.write_text(text)
    run(llvm/'llvm-as', start, '-o', out/'musl-sysroot/lib/start.bc')
    artifact = scratch/'publish.bc'
    run(llvm/'llvm-link', *objects, '-o', artifact)
    run(llvm/'opt', '-passes=verify', '-disable-output', artifact)
    ir = run(llvm/'llvm-dis', artifact, '-o', '-')
    exports = sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--defined-only', '--extern-only', artifact).splitlines() if line.split())
    assert set(EXPORTS + ['printf','puts','write','exit']) <= set(exports), exports
    assert not re.search(r'^define .*@(?:__bennett_nz_|__bennett_effect_)', ir, re.M)
    manifest = {
        'implementation': 'musl-console-v1', 'llvm_major': int(major),
        'target': 'x86_64-unknown-linux-musl', 'dialect': '0.2.10',
        'dialect_commit': '023f5a771a814270634db3be9cfde88ded69f975',
        'isa': '0.8', 'sha256': sha(artifact), 'exports': exports,
        'effect_imports': sorted(set(re.findall(r'^declare .*?@(__bennett_effect_[^ (]+)\(', ir, re.M))),
        'undefined_imports': sorted(line.split()[-1] for line in run(llvm/'llvm-nm', '--undefined-only', artifact).splitlines() if line.split()),
        'primitive_imports': ['__bennett_nz_alloc', '__bennett_nz_fault', '__bennett_nz_free'],
        'flags': FLAGS,
        'musl': {'version': musl.VERSION, 'url': musl.URL, 'source_sha256': musl.SHA, 'files': sorted(set(musl.FILES)), 'flags': [re.sub(str(scratch), '<build>', re.sub(str(out), '<dist>', f)) for f in musl_flags]},
        'sysroot': {str(p.relative_to(out)): sha(p) for p in sorted((out/'musl-sysroot').rglob('*')) if p.is_file()},
        'tools': {name: run(llvm/name, '--version').splitlines()[0]
                  for name in ('clang', 'llvm-as', 'llvm-link', 'opt')},
        'sources': {s: sha(ROOT/s) for s in SOURCES +
                    ['scripts/build', 'scripts/build.py', 'scripts/musl.py', 'scripts/native-link', 'src/musl/context.c', 'src/native/start.S', 'src/native/start.c', 'src/native/syscall.c', 'src/native/heap.c', 'SPEC_VERSIONS.toml']},
    }
    # Atomic file replacement prevents consumers observing partial bitcode.
    temporary = out/'.bennett-runtimes.bc.new'
    temporary.write_bytes(artifact.read_bytes())
    temporary.replace(out/'bennett-runtimes.bc')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    (out/'SHA256SUMS').write_text(''.join(f'{sha(out/name)}  {name}\n'
                                        for name in ('bennett-runtimes.bc', 'manifest.json')))
    print(f'LLVM {major}: {manifest["sha256"]}  {out/"bennett-runtimes.bc"}')

if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else '')
