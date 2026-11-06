#!/usr/bin/env python3
"""Real upstream localeconv/getpagesize, prepared process and checked O0/O2."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, write_layout
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'process-apis'
TMP.mkdir(parents=True, exist_ok=True)


for major in (18, 23):
    llvm = llvm_dir(major)
    dist = ROOT/f'dist/llvm{major}'
    publication = dist/'bennett-runtimes.bc'
    original = hashlib.sha256(publication.read_bytes()).digest()
    ir = run(llvm/'llvm-dis', publication, '-o', '-').decode()
    for name in ('localeconv', 'getpagesize'):
        assert re.search(r'^define .*@'+name+r'\(', ir, re.M), name+' missing'
    manifest = json.loads((dist/'manifest.json').read_text())
    assert {'locale/localeconv', 'legacy/getpagesize'} <= set(manifest['musl']['files'])
    assert {'localeconv', 'getpagesize'} <= set(manifest['exports'])
    for level in (0, 2):
        app = TMP/f'app-{major}-{level}.bc'
        flags = ['--target=x86_64-unknown-linux-musl', f'-O{level}', '-ffreestanding', '-fno-builtin',
                 '-fno-vectorize', '-fno-slp-vectorize', '-nostdinc', '-isystem', dist/'musl-sysroot/include']
        run(llvm/'clang', *flags, '-emit-llvm', '-c', ROOT/'tests/process_apis.c', '-o', app)
        layout = TMP/f'layout-{major}.ll'
        write_layout(layout, 570429440, 1048576)
        closure = TMP/f'closure-{major}-{level}.bc'
        checked = TMP/f'checked-{major}-{level}.bc'
        native = TMP/f'native-{major}-{level}.bc'
        run(llvm/'llvm-link', publication, '--override='+str(app), layout, '-o', closure)
        run(ROOT/'scripts/native-check', major, closure, checked)
        run(llvm/'llvm-link', checked, '--override='+str(dist/'musl-sysroot/lib/native-support.bc'), '-o', native)
        exe = TMP/f'apis-{major}-{level}'
        run(llvm/'clang', '--target=x86_64-unknown-linux-musl', f'-O{level}', '-Xclang', '-disable-llvm-passes',
            '-nostdlib', '-nostdinc', '-static', '-no-pie', '-ffreestanding', '-fno-builtin',
            '-Wl,-e,_start', '-Wl,--no-undefined', native, '-o', exe)
        assert not run(llvm/'llvm-nm', '--undefined-only', exe).strip()
        assert b'NEEDED' not in run('readelf', '-d', exe)
        assert run(exe) == b''
    assert hashlib.sha256(publication.read_bytes()).digest() == original
    print(f'LLVM {major}: upstream localeconv all C-locale fields and getpagesize, initial/prepared process, strict checked O0/O2 no-host closure PASS', flush=True)
