#!/usr/bin/env python3
"""Red-by-design capability inventory; enable with scripts/gate --promised."""
from pathlib import Path
import subprocess
import sys
import tomllib
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]
manifest = tomllib.loads((ROOT/'tests/promised-helpers.toml').read_text())
missing = []
for major in (18, 23):
    output = subprocess.check_output([f'/usr/lib/llvm-{major}/bin/llvm-nm', '--defined-only',
                                      ROOT/f'dist/llvm{major}/bennett-runtimes.bc'], text=True)
    names = {line.split()[-1] for line in output.splitlines() if line.split()}
    missing.extend(f'LLVM {major}: {name}' for name in manifest['symbols'] if name not in names)
if missing:
    message = f"{manifest['bead']} (blocked by {manifest['blocked_by']}): " + '; '.join(missing)
    if '--report' in sys.argv:
        print('PROMISED / BLOCKED: '+message)
    else:
        raise AssertionError(message)
else:
    print('All promised helper definitions present; integrated contract tests still required')
