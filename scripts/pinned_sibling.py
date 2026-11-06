#!/usr/bin/env python3
"""Resolve a sibling exclusively through gate.toml and verify its actual HEAD."""
import os
from pathlib import Path
import subprocess
import sys
import tomllib
ROOT = Path(__file__).resolve().parents[1]

def resolve(name, root=ROOT):
    pins = tomllib.loads((root/'gate.toml').read_text())['siblings']
    pin = pins[name]['sha']
    variable = name.upper().replace('-', '_')+'_ROOT'
    checkout = Path(os.environ[variable]) if variable in os.environ else root/'.tmp/gate/cache'/f'{name}@{pin[:12]}'/name
    env = {key: value for key, value in os.environ.items()
           if key not in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE')}
    result = subprocess.run(['git', '-C', checkout, 'rev-parse', 'HEAD'], env=env,
                            capture_output=True, text=True)
    actual = result.stdout.strip() if result.returncode == 0 else 'unavailable'
    if actual != pin:
        raise RuntimeError(f'{name}: gate.toml pin {pin}, actual HEAD {actual} at {checkout}; materialise the gate pin')
    return checkout

if __name__ == '__main__':
    print(resolve(sys.argv[1]))
