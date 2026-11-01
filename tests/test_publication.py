#!/usr/bin/env python3
"""Consumer edge: recorded/fresh closure and independent native golden cases."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CONFORMANCE = Path(os.environ.get('BENNETT_CONFORMANCE_ROOT', ROOT.parent/'bennett-conformance'))
ARITH = Path(os.environ.get('BENNETT_ARITH_ROOT', ROOT.parent/'bennett-arith'))
RUNG = CONFORMANCE/'ladder/rung-2'
TMP = ROOT/'.tmp'/'publication'
TMP.mkdir(parents=True, exist_ok=True)
EXPORTS = {'malloc', 'calloc', 'free', 'memset', '__bennett_rt_memset', '__bennett_rt_memmove', '__bennett_rt_alloc'}
PRIMITIVES = {'__bennett_nz_alloc', '__bennett_nz_free', '__bennett_nz_fault'}
MARKERS = {'__bennett_qalloc', '__bennett_qfree'}
GOLDEN = b'''basic 0 0
basic 1 23131069394
basic 7 138063414195
basic 64 1083288249908
basic 1000 54380828142828
grow 0 4
grow 1 23131069404
grow 7 138063414247
grow 64 1083288250354
grow 1000 54380828149874
'''

def run(*args):
    return subprocess.check_output([str(a) for a in args], stderr=subprocess.PIPE)

def names(ir, keyword):
    return set(re.findall(rf'^{keyword} .*?@([^ (]+)\(', ir, re.M))

def nonintrinsic_undefined(ir):
    return {n for n in names(ir, 'declare') if not n.startswith('llvm.')}

for major in (18, 23):
    llvm = Path(f'/usr/lib/llvm-{major}/bin')
    dist = ROOT/f'dist/llvm{major}'
    bc = dist/'musl-sysroot/lib/bennett-allocator.bc'
    arith = ARITH/f'dist/llvm{major}/bennett-arith.bc'
    manifest = json.loads((dist/'manifest.json').read_text())
    assert manifest['llvm_major'] == major
    assert manifest['sha256'] == hashlib.sha256((dist/'bennett-runtimes.bc').read_bytes()).hexdigest()
    assert manifest['dialect_commit'] == '023f5a771a814270634db3be9cfde88ded69f975'
    assert set(manifest['exports']) >= EXPORTS
    for source, expected in manifest['sources'].items():
        assert hashlib.sha256((ROOT/source).read_bytes()).hexdigest() == expected, source
    ir = run(llvm/'llvm-dis', bc, '-o', '-').decode()
    assert names(ir, 'define') == EXPORTS
    assert nonintrinsic_undefined(ir) == PRIMITIVES
    assert '__bennett_qalloc' not in ir and '__bennett_qfree' not in ir
    for name, indices in [('malloc', '0'), ('calloc', '0,1')]:
        definition = next(line for line in ir.splitlines() if line.startswith('define ') and f'@{name}(' in line)
        group = re.search(r'#(\d+)', definition)[1]
        attrs = next(line for line in ir.splitlines() if line.startswith(f'attributes #{group} ='))
        assert 'allockind("alloc")' in attrs and f'allocsize({indices})' in attrs, attrs
    recorded = RUNG/f'evidence/first-run/{major}/postlink.ll'
    before = nonintrinsic_undefined(recorded.read_text()) - MARKERS
    assert before == {'malloc', 'calloc', 'free'}, before
    assert before <= EXPORTS
    # Both the exact saved post-LTO evidence and fresh frontend output must close.
    fresh = TMP/f'marked-{major}.bc'
    run(llvm/'clang', '-O2', '-flto', '-emit-llvm', '-c',
        RUNG/'fixtures/hashtable_marked.c', '-o', fresh)
    for label, source in [('recorded', recorded), ('fresh', fresh)]:
        linked = TMP/f'{label}-{major}.bc'
        run(llvm/'llvm-link', source, bc, arith, '--only-needed', '-o', linked)
        for pipeline in ('verify', 'lto<O3>'):
            text = run(llvm/'opt', f'-passes={pipeline}', '-S', linked, '-o', '-').decode()
            undefined = nonintrinsic_undefined(text)
            assert undefined <= PRIMITIVES | MARKERS, (major, label, pipeline, undefined)
            assert '__bennett_nz_alloc' in undefined
    print(f'LLVM {major}: export/attribute audit, recorded+fresh link/LTO closure PASS')
