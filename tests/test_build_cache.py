#!/usr/bin/env python3
"""A cache hit preserves bytes; source/header changes invalidate it."""
import importlib.util
import os
from pathlib import Path
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('build_cache', ROOT/'scripts/build_cache.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as directory:
    root = Path(directory)
    source, output = root/'input.c', root/'output.ll'
    source.write_text('int f(void) { return 1; }\n')
    cache = module.BuildCache(root/'cache', 'headers-one')
    command = [llvm_dir(18)/'clang', '-O2', '-S', '-emit-llvm', source, '-o', output]
    cache.execute(command, root)
    original = output.read_bytes()
    output.unlink()
    cache.execute(command, root)
    assert output.read_bytes() == original and (cache.hits, cache.misses) == (1, 1)
    source.write_text('int f(void) { return 2; }\n')
    cache.execute(command, root)
    assert output.read_bytes() != original and cache.misses == 2
    changed_headers = module.BuildCache(root/'cache', 'headers-two')
    changed_headers.execute(command, root)
    assert changed_headers.misses == 1
print('cache restores identical bytes; changed source and headers miss PASS')
