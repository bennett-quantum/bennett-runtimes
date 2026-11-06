#!/usr/bin/env python3
"""The quality ratchet detects each metric regression on independent examples."""
import importlib.util
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('quality', ROOT/'scripts/quality.py')
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)
assert quality.python_functions('def f(x):\n    return g(x)\n') == [(2, 0, 1)]
assert quality.python_functions('def f(x):\n    if x:\n        if x > 1:\n            return x\n')[0][1] == 3
assert quality.c_functions('int f(int x) {\n if(x) { if(x>1) return x; }\n return 0;\n}')[0][:2] == (4, 3)
values = dict(max_lines=4, over_20=0, over_50=0, over_100=0, over_300=0, complexity_mass=3, trivial_wrappers=0)
baseline = dict(files={'example.c': values}, litter=[], process_identifiers=[])
assert quality.regressions(baseline, baseline) == []
for metric in values:
    changed = dict(values)
    changed[metric] += 1
    assert quality.regressions(baseline, dict(files={'example.c': changed}, litter=[], process_identifiers=[]))
for metric in ('litter', 'process_identifiers'):
    changed = dict(baseline)
    changed[metric] = ['unexpected']
    assert quality.regressions(baseline, changed)
assert quality.LITTER.search('tests/__pycache__/test.pyc')
assert quality.PROCESS.search('brt-abc')
assert not quality.PROCESS.search('ordinary runtime error')
print('function parsing and length/complexity/wrapper/litter/identifier regressions PASS')
