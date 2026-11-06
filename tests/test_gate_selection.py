#!/usr/bin/env python3
"""Every test belongs to an explicit fast, slow or promised suite."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]
listed = {}
for suite in ('fast', 'slow', 'promised'):
    result = subprocess.run([ROOT/'scripts/gate', '--list', suite], capture_output=True, text=True, check=True)
    listed[suite] = result.stdout.splitlines()
    assert len(listed[suite]) == len(set(listed[suite])), (suite, listed[suite])
all_tests = [test for tests in listed.values() for test in tests]
assert len(all_tests) == len(set(all_tests)), all_tests
assert set(all_tests) == {p.name for p in (ROOT/'tests').glob('test_*.py')}
assert {'test_native.py', 'test_sqlite_memdb.py', 'test_reproducibility.py'} <= set(listed['slow'])
assert 'test_promised_helpers.py' in listed['promised']
print('Every test is assigned exactly once; expensive and promised tests remain visible')
