"""Shared command, toolchain and layout support for component tests."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('pinned_sibling', ROOT/'scripts/pinned_sibling.py')
_pins = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_pins)
pinned_sibling = _pins.resolve

def llvm_dir(major):
    return Path(os.environ.get(f'BENNETT_LLVM{major}_BIN', f'/usr/lib/llvm-{major}/bin'))

def run(*args, timeout=180, stderr=subprocess.PIPE):
    command = list(map(str, args))
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=stderr, start_new_session=True) as process:
        try:
            output, errors = process.communicate(timeout=timeout)
        except BaseException:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        if process.returncode:
            if errors:
                print(errors.decode(errors='replace'))
            raise subprocess.CalledProcessError(process.returncode, command, output, errors)
        return output

def write_layout(path, base, limit):
    path.write_text('@__bennett_rt_native_layout = constant { i64, i64, i64 } '
                    f'{{ i64 1, i64 {base}, i64 {limit} }}, align 8\n')
