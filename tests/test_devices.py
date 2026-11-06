#!/usr/bin/env python3
"""Published lifted models: independent vectors, undo, and checked comparison."""
import os
from pathlib import Path
import re
import resource
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'devices'
TMP.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


for major in (18, 23):
    llvm = llvm_dir(major)
    dist = ROOT/f'dist/llvm{major}'
    ir = run(llvm/'llvm-dis', dist/'bennett-runtimes.bc', '-o', '-').decode()
    names = ['__bennett_dev_clock_read', '__bennett_dev_entropy_read',
             '__bennett_dev_block_read', '__bennett_dev_block_write']
    for name in names:
        assert re.search(r'^define .*@'+name+r'\(', ir, re.M), name+' missing'
    for suffix, initial, align in (
            ('clock_c', 'i64 0', 8), ('clock_bound', 'i32 0', 4),
            ('entropy_base', 'ptr null', 8), ('entropy_size', 'i64 0', 8),
            ('entropy_pos', 'i64 0', 8), ('entropy_bound', 'i32 0', 4),
            ('block_base', 'ptr null', 8), ('block_nsect', 'i64 0', 8),
            ('block_bound', 'i32 0', 4), ('block_overlay', 'ptr null', 8)):
        assert re.search(r'^@__bennett_dev_'+suffix+r' = .*global '+initial+
                         r', align '+str(align)+r'(?:,|$)', ir, re.M), suffix
    assert not re.search(r'^define .*@__bennett_effect_', ir, re.M)
    # Journal stores in the actual publication, including its allocator. The
    # independent primitive below journals exact allocation/zero-fill history.
    allocator = run(llvm/'llvm-dis', dist/'musl-sysroot/lib/bennett-allocator.bc', '-o', '-').decode()
    tracked = set(re.findall(r'^define .*?@([^ (]+)\(', allocator, re.M)) | set(names)
    for name in ('malloc', 'calloc', 'realloc', 'free', 'memset'):
        ir = re.sub(r'@'+name+r'(?=[(\s},])', '@test_'+name, ir)
        if name in tracked:
            tracked.remove(name)
            tracked.add('test_'+name)
    active = False
    lines = []
    stores = 0
    for line in ir.splitlines():
        if line.startswith('define '):
            active = re.search(r'@([^ (]+)\(', line)[1] in tracked
        if active and line.lstrip().startswith('store '):
            match = re.match(r'\s*store (?:volatile )?(i8|i16|i32|i64|ptr) .*?, ptr ([^, ]+)', line)
            assert match, line
            size = {'i8': 1, 'i16': 2, 'i32': 4, 'i64': 8, 'ptr': 8}[match[1]]
            lines.append(f'  call void @test_record(ptr {match[2]}, i64 {size})')
            stores += 1
        lines.append(line)
    assert stores >= 20, stores
    lines.append('declare void @test_record(ptr, i64)')
    instrumented = TMP/f'history-{major}.ll'
    instrumented.write_text('\n'.join(lines)+'\n')
    harness = TMP/f'harness-{major}.bc'
    linked = TMP/f'history-{major}.bc'
    run(llvm/'clang', '-O0', '-ffreestanding', '-fno-builtin', '-emit-llvm', '-c',
        ROOT/'tests/devices_history.c', '-o', harness)
    run(llvm/'llvm-link', harness, instrumented, '--only-needed', '-o', linked)
    exe = TMP/f'history-{major}'
    run(llvm/'clang', '-O0', '-ffreestanding', '-fno-builtin', linked, '-o', exe)
    run(exe)
    flags = ['--target=x86_64-unknown-linux-musl', '-O2', '-ffreestanding', '-fno-builtin',
             '-nostdinc', '-isystem', dist/'musl-sysroot/include', '-emit-llvm', '-c']
    checked = TMP/f'checked-{major}.bc'
    run(llvm/'clang', *flags, ROOT/'tests/devices_checked.c', '-o', checked)
    layout = TMP/f'layout-{major}.ll'
    write_layout(layout, 553652224, 1048576)
    exe = TMP/f'checked-{major}'
    run(ROOT/'scripts/native-check-link', major, checked, exe, layout)
    assert not run(llvm/'llvm-nm', '--undefined-only', exe).strip()
    assert b'NEEDED' not in run('readelf', '-d', exe)
    run(exe)
    for case in ('clock-short', 'entropy-short', 'block-short', 'read-short', 'image-short', 'tape-short'):
        r = subprocess.run([exe, case], capture_output=True, timeout=10)
        assert r.returncode == -4 and r.stderr == b'bennett runtime fault 7\n' and not r.stdout, (major, case, r)
    print(f'LLVM {major}: clock/entropy/block vectors, pre-mutation faults, exact store+allocation undo/re-execution, static checked comparison and short-object faults PASS', flush=True)
