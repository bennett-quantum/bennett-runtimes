#!/usr/bin/env python3
"""Rebuild in another source path: LLVM 23 local-symbol GUIDs must be stable."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='relocated-',dir=ROOT/'.tmp') as tmp:
    source=Path(tmp)/'source'
    source.mkdir()
    for name in ('scripts','src','vendor','julia-shim'):
        shutil.copytree(ROOT/name,source/name,ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copyfile(ROOT/'SPEC_VERSIONS.toml',source/'SPEC_VERSIONS.toml')
    shutil.copyfile(ROOT/'gate.toml',source/'gate.toml')
    for major in (18,23):
        out=Path(tmp)/f'publish-{major}'
        subprocess.run([source/'scripts/build',str(major),out],check=True,timeout=240)
        for _ in range(2):
            cached = Path(tmp)/f'cached-{major}'
            result = subprocess.run([source/'scripts/build',str(major),cached,'--incremental'],
                                    check=True,capture_output=True,text=True,timeout=240)
            if _ == 1:
                assert '0 misses' in result.stdout, result.stdout
            for path in out.rglob('*'):
                if path.is_file():
                    subprocess.run(['cmp', path, cached/path.relative_to(out)],check=True)
        baseline=ROOT/f'dist/llvm{major}'
        expected={str(p.relative_to(baseline)) for p in baseline.rglob('*') if p.is_file()}
        actual={str(p.relative_to(out)) for p in out.rglob('*') if p.is_file()}
        assert actual==expected, (major,actual^expected)
        for name in sorted(expected):
            assert (out/name).read_bytes()==(baseline/name).read_bytes(), (major,name,'checkout-path-dependent publication')
print('LLVM 18/23: relocated-source full publication byte comparison PASS')
