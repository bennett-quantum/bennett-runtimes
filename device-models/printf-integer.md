# Integer printf console expectations

These three ordinary `main(void)` programs are in
[`tests/printf_integer/`](../tests/printf_integer/). They use the real musl
`printf`/`vfprintf` path with integer/string/character arguments; floating point
is outside this acceptance family. They require 32-bit int and LP64 long,
with 64-bit long long, checked by C static assertions.

Each tuple below is an ordered list of **exact ASCII byte strings**. Concatenate
its entries, interpret `\n` as byte 0x0a, and retain every space inside the
brackets. Quotes, tuple punctuation and the Markdown code fence are not output.
The last entry has no newline, carriage return or NUL terminator. Expected
stderr is empty. These tables mirror the independently hand-derived tables in
[`test_printf_integer.py`](../tests/test_printf_integer.py); that test checks
that the documentation and literal oracle stay equal.

## Conversions — exit status 0

Seven differently typed arguments in the first call exercise `%s %d %i %u %x
%X %c %%`, with negative numbers, zero and the signed/unsigned 32-bit limits.

<!-- conversions -->
```python
(
    b'basic:musl|-42|-17|42|abcdef|ABCDEF|Z|%\n',
    b'limits:-2147483648|2147483647|4294967295|ffffffff|FFFFFFFF\n',
    b'zero:0|0|0|0|0\n',
    b'RETURN\n',
    b'FINAL conversions:buffered|-7|!|%',
)
```

## Fields — exit status 7

Width includes the sign; integer precision counts only digits. Zero padding
follows the sign, `-` pads to the right, and explicit integer precision ignores
the `0` flag. Precision zero suppresses a zero integer's digits. String precision
truncates bytes. Dynamic width/precision consume extra int arguments, including
a negative width for left alignment.

<!-- fields -->
```python
(
    b'width:[    42][42    ][000042][-00042]\n',
    b'precision:[-00042][  -00042][-00042  ][  -00042]\n',
    b'unsigned:[00000042][00002a][    002A][42      ]\n',
    b'text:[     abc][abc     ][][   A][B   ][%]\n',
    b'empty:[][][][][][     ]\n',
    b'dynamic:[   -0012][abc    ][-00012]\n',
    b'RETURN\n',
    b'FINAL fields:[-00009][][do   ]',
)
```

## Lengths — exit status 23

`l` and `ll` cover signed minimum/maximum, unsigned maximum, both hex cases
and fields wider than 64-bit decimal numbers. The `walk` call interleaves eleven
integer/pointer arguments, passing the x86_64 six-GP-register boundary and
requiring the full variadic argument walk.

<!-- lengths -->
```python
(
    b'long:-9223372036854775808|9223372036854775807|18446744073709551615|ffffffffffffffff|FFFFFFFFFFFFFFFF\n',
    b'long-long:-9223372036854775808|9223372036854775807|18446744073709551615|ffffffffffffffff|FFFFFFFFFFFFFFFF\n',
    b'wide-fields:[-009223372036854775808][   -09223372036854775808][18446744073709551615    ]\n',
    b'walk:-1|two|-3000000000|4|-5000000000|66|G|8000000000|-9|10000000000|AB|%\n',
    b'RETURN\n',
    b'FINAL lengths:-3000000000|-5000000000|18446744073709551615',
)
```

## Ordering, normal-return flush and executor acceptance

Each fixture first prints its complete newline-terminated rows and explicitly
flushes **only that prefix**. It then calls formatted `printf` for `FINAL`
without `\n`, writes `RETURN\n` directly on stdout with `write(1, ...)`, and
returns from main with the stated status. There is no explicit flush/close/exit
after `FINAL`, no unbuffered mode and no `atexit` handler in the fixture.
Consequently the console must already contain the prefix and `RETURN\n`
before normal-return cleanup appends `FINAL`. An omitted cleanup loses `FINAL`;
an early flush puts `FINAL` before `RETURN` and also fails these tables.

The R console must concatenate all console-write payloads in this order on
each executor and both LLVM majors, reaching the stated exit status. Payload
chunking is not fixed: musl may split a flush into writev vectors. An executor's
normal-return lifecycle must run the ordinary musl stdio cleanup before
completion. Whole-run reversal must restore console and process state to their
pre-run values; saved-tape replay must reproduce the same bytes and status.
For a private L console, bind capacity at least the full expected byte length
to compare these tables without the separate §11.3 truncation behavior.

The native golden test establishes original-bitcode musl output and link
closure. It does **not** establish R materialisation, executor startup/flush,
reset or replay. The conformance rung-3 gate owns those obligations and can
cite these fixtures and byte/status tables without substituting constant hello.
