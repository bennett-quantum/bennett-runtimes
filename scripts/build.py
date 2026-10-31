#!/usr/bin/env python3
"""Deterministic major-matched publication; no timestamps or checkout paths."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/allocator/bump.c', 'src/libc/memory.c']
EXPORTS = ['__bennett_rt_memmove', '__bennett_rt_memset', 'calloc', 'free', 'malloc', 'memset']
FLAGS = ['-target', 'x86_64-unknown-linux-gnu', '-std=c11', '-O2', '-flto',
         '-ffreestanding', '-fno-builtin', '-fno-vectorize', '-fno-slp-vectorize',
         '-fno-ident', '-Wall', '-Wextra', '-Werror']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True)

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
        run(llvm/'clang', *FLAGS, '-S', '-emit-llvm', ROOT/source, '-o', ll)
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
    artifact = scratch/'publish.bc'
    run(llvm/'llvm-link', *objects, '-o', artifact)
    run(llvm/'opt', '-passes=verify', '-disable-output', artifact)
    ir = run(llvm/'llvm-dis', artifact, '-o', '-')
    exports = sorted(re.findall(r'^define (?!internal |private ).*?@([^ (]+)\(', ir, re.M))
    assert exports == EXPORTS, exports
    assert not re.search(r'^define .*@__bennett_nz_', ir, re.M)
    manifest = {
        'implementation': 'rung-2-allocation-v1', 'llvm_major': int(major),
        'target': 'x86_64-unknown-linux-gnu', 'dialect': '0.2.10',
        'dialect_commit': '023f5a771a814270634db3be9cfde88ded69f975',
        'isa': '0.8', 'sha256': sha(artifact), 'exports': exports,
        'primitive_imports': ['__bennett_nz_alloc', '__bennett_nz_fault', '__bennett_nz_free'],
        'flags': FLAGS,
        'tools': {name: run(llvm/name, '--version').splitlines()[0]
                  for name in ('clang', 'llvm-as', 'llvm-link', 'opt')},
        'sources': {s: sha(ROOT/s) for s in SOURCES +
                    ['scripts/build', 'scripts/build.py', 'SPEC_VERSIONS.toml']},
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
