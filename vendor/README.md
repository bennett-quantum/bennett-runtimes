# musl 1.2.5 (unmodified upstream source archive)

URL: https://musl.libc.org/releases/musl-1.2.5.tar.gz
SHA-256: a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4

Downloaded once on 2026-11-01. No build fetches anything. COPYRIGHT from the
archive is reproduced as MUSL-COPYRIGHT and in both published sysroots.
`scripts/musl.py` verifies the archive digest before extracting it locally.
The explicit source list is the first stdio/exit slice, not a replacement libc.
All upstream headers are published; other source families remain under brt-q1k
and brt-zct. Platform headers for syscalls and process context are generated
outside the upstream source tree. No upstream source is patched.
