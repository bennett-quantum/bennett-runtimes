#!/usr/bin/env python3
"""Hand-derived integer printf acceptance, independent host libc and native musl."""
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir, pinned_sibling, run, write_layout

ROOT = Path(__file__).resolve().parents[1]
ARITH = pinned_sibling('bennett-arith')
TMP = Path(os.environ['TMPDIR'])/'printf-integer'
TMP.mkdir(parents=True, exist_ok=True)

# Independent literal tables, derived from the C formats, never captured from musl:
# int is 32 bits: -2^31, 2^31-1, 2^32-1; hex UINT_MAX is eight f/F digits.
# LP64 long and long long use -2^63, 2^63-1, 2^64-1, sixteen f/F hex digits.
# Width counts sign+digits; precision counts digits only. '-' pads on the right;
# '0' pads after the sign, but integer precision overrides '0'. String precision
# caps bytes, %.0 zero integers emit no digits, and '*' consumes an int each time.
# wide-fields: 22-(1+19)=2 zeros; 24-(1+20)=3 spaces; 24-20=4 spaces.
# RETURN is a direct write AFTER the final printf, which remains buffered until
# main returns. The earlier fflush is before FINAL and cannot flush it.
CASES = {
    'conversions': (0, (
        b'basic:musl|-42|-17|42|abcdef|ABCDEF|Z|%\n',
        b'limits:-2147483648|2147483647|4294967295|ffffffff|FFFFFFFF\n',
        b'zero:0|0|0|0|0\n',
        b'RETURN\n',
        b'FINAL conversions:buffered|-7|!|%',
    )),
    'fields': (7, (
        b'width:[    42][42    ][000042][-00042]\n',
        b'precision:[-00042][  -00042][-00042  ][  -00042]\n',
        b'unsigned:[00000042][00002a][    002A][42      ]\n',
        b'text:[     abc][abc     ][][   A][B   ][%]\n',
        b'empty:[][][][][][     ]\n',
        b'dynamic:[   -0012][abc    ][-00012]\n',
        b'RETURN\n',
        b'FINAL fields:[-00009][][do   ]',
    )),
    'lengths': (23, (
        b'long:-9223372036854775808|9223372036854775807|18446744073709551615|ffffffffffffffff|FFFFFFFFFFFFFFFF\n',
        b'long-long:-9223372036854775808|9223372036854775807|18446744073709551615|ffffffffffffffff|FFFFFFFFFFFFFFFF\n',
        b'wide-fields:[-009223372036854775808][   -09223372036854775808][18446744073709551615    ]\n',
        b'walk:-1|two|-3000000000|4|-5000000000|66|G|8000000000|-9|10000000000|AB|%\n',
        b'RETURN\n',
        b'FINAL lengths:-3000000000|-5000000000|18446744073709551615',
    )),
}
CONSOLE_TABLES = (ROOT/'device-models/printf-integer.md').read_text()
for name, (_, rows) in CASES.items():
    block = CONSOLE_TABLES.split(f'<!-- {name} -->\n```python\n', 1)[1].split('\n```', 1)[0]
    assert ast.literal_eval(block) == rows, ('console expectation drift', name)


def check_output(executable, label, expected_status, expected_bytes):
    result = subprocess.run([executable], capture_output=True, timeout=10)
    executable.with_suffix('.stdout').write_bytes(result.stdout)
    executable.with_suffix('.stderr').write_bytes(result.stderr)
    executable.with_suffix('.json').write_text(json.dumps({
        'exit_status': result.returncode,
        'stdout_bytes': len(result.stdout),
        'stdout_sha256': hashlib.sha256(result.stdout).hexdigest(),
    }, indent=2)+'\n')
    assert (result.returncode, result.stdout, result.stderr) == (
        expected_status, expected_bytes, b''), (label, result)
    return result


def build_native(major, source, output, layout, printf_calls):
    llvm = llvm_dir(major)
    dist = ROOT/f'dist/llvm{major}'
    sysroot = dist/'musl-sysroot'
    original = output.with_suffix('.bc')
    linked = output.with_suffix('.linked.bc')
    run(llvm/'clang', '--target=x86_64-unknown-linux-musl',
        '--sysroot='+str(sysroot), '-nostdinc', '-isystem', sysroot/'include',
        '-std=c11', '-O2', '-ffreestanding', '-fno-builtin', '-flto',
        '-emit-llvm', '-c', source, '-o', original)
    ir = run(llvm/'llvm-dis', original, '-o', '-')
    assert len(re.findall(rb'\bcall\b[^\n]*@printf\(', ir)) == printf_calls, (major, source, ir)
    run(llvm/'llvm-link', original, dist/'bennett-runtimes.bc',
        ARITH/f'dist/llvm{major}/bennett-arith.bc', '--only-needed', '-o', linked)
    run(llvm/'opt', '-passes=lto<O3>', linked, '-o', linked)
    run(ROOT/'scripts/native-link', major, linked, output, layout)
    assert not run(llvm/'llvm-nm', '--undefined-only', output).strip(), output
    assert b'NEEDED' not in run('readelf', '-d', output), output


for major in (18, 23):
    llvm = llvm_dir(major)
    layout = TMP/f'layout-{major}.ll'
    write_layout(layout, 0x1a000000, 1048576)
    for name, (status, rows) in CASES.items():
        expected = b''.join(rows)
        source = ROOT/'tests/printf_integer'/f'{name}.c'
        host = TMP/f'host-{name}-{major}'
        # Independent host glibc; all formats here have defined matching semantics.
        run(llvm/'clang', '-std=c11', '-O2', '-fno-builtin', source, '-o', host)
        check_output(host, ('host', major, name), status, expected)
        golden = TMP/f'native-{name}-{major}'
        build_native(major, source, golden, layout, len(rows)-1)
        check_output(golden, ('native', major, name), status, expected)
    print(f'LLVM {major}: integer printf host/native goldens equal hand-derived bytes/status PASS', flush=True)
