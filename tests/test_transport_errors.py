#!/usr/bin/env python3
"""Host write errors are infrastructure failures, never dialect fault 8."""
from pathlib import Path
import os
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ['TMPDIR'])/'transport-errors'
TMP.mkdir(parents=True, exist_ok=True)
for major in (18, 23):
    exe = TMP/f'transport-{major}'
    subprocess.run([f'/usr/lib/llvm-{major}/bin/clang', '-O2', '-ffunction-sections',
                    '-Wl,--gc-sections', ROOT/'tests/transport_errors.c',
                    ROOT/'src/native/syscall.c', ROOT/'src/native/heap.c', '-o', exe], check=True)
    result = subprocess.run([exe, '3'], capture_output=True, timeout=5)
    assert (result.returncode, result.stdout, result.stderr) == (0, b'hello', b''), result
    with open('/dev/full', 'wb') as full:
        result = subprocess.run([exe], stdout=full, stderr=subprocess.PIPE, timeout=5)
    failures = [result]
    failures.append(subprocess.run([exe], preexec_fn=lambda: os.close(1), stderr=subprocess.PIPE, timeout=5))
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        failures.append(subprocess.run([exe], stdout=write_fd, stderr=subprocess.PIPE,
                                       restore_signals=False, timeout=5))
    finally:
        os.close(write_fd)
    for mode in ('1', '2'):
        failures.append(subprocess.run([exe, mode], capture_output=True, timeout=5))
    for result in failures:
        assert result.returncode == 125, result
        assert result.stderr == b'bennett infrastructure failure: console write\n', result
    print(f'LLVM {major}: ENOSPC/EBADF/EPIPE/zero/oversized writes exit 125; EINTR/short writes complete')
