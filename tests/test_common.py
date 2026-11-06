#!/usr/bin/env python3
"""Command failures retain evidence; pin mismatches fail instead of using siblings."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import run, llvm_dir, pinned_sibling, write_layout
assert run(sys.executable, '-c', 'print("ok")') == b'ok\n'
try:
    run(sys.executable, '-c', 'import sys; print("out"); sys.stderr.write("err"); sys.exit(9)')
except subprocess.CalledProcessError as error:
    assert (error.returncode, error.output, error.stderr) == (9, b'out\n', b'err')
else:
    raise AssertionError('failed command accepted')
try:
    run(sys.executable, '-c', 'import time; time.sleep(30)', timeout=0.1)
except subprocess.TimeoutExpired:
    pass
else:
    raise AssertionError('timeout accepted')
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as directory:
    root = Path(directory)
    (root/'gate.toml').write_text('[siblings.example]\nsha = "'+'a'*40+'"\n')
    os.environ['EXAMPLE_ROOT'] = str(root/'missing')
    try:
        pinned_sibling('example', root)
    except RuntimeError as error:
        assert 'pin '+'a'*40 in str(error) and 'actual HEAD unavailable' in str(error)
    else:
        raise AssertionError('missing pin accepted')
    checkout = root/'other'
    subprocess.run(['git', 'init', '-q', checkout], check=True)
    subprocess.run(['git', '-C', checkout, '-c', 'user.name=Test', '-c',
                    'user.email=test@example.invalid', 'commit', '-q', '--allow-empty', '-m', 'fixture'], check=True)
    actual = subprocess.check_output(['git', '-C', checkout, 'rev-parse', 'HEAD'], text=True).strip()
    os.environ['EXAMPLE_ROOT'] = str(checkout)
    try:
        pinned_sibling('example', root)
    except RuntimeError as error:
        assert 'actual HEAD '+actual in str(error)
    else:
        raise AssertionError('mismatching HEAD accepted')
    path = root/'layout.ll'
    write_layout(path, 4096, 512)
    assert '{ i64 1, i64 4096, i64 512 }' in path.read_text()
os.environ['BENNETT_LLVM18_BIN'] = '/custom/tools'
assert llvm_dir(18) == Path('/custom/tools')
print('commands preserve bytes/errors, timeouts terminate, missing pins fail, layout/tool overrides PASS')
