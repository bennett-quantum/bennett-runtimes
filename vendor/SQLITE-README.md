# SQLite test input

`sqlite-amalgamation-3540100.zip` is the unmodified SQLite 3.54.1
amalgamation archive named by the published dialect spike report at
`docs/spikes/sqlite/REPORT.md`. Its SHA3-256 is
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`.
It was copied from the already verified offline download cache; tests verify
the archive digest before extracting it. SQLite is public domain, as stated
in the source header. No SQLite source edits or private libc replacement are
applied. The archive is used by the libc/memdb tests and is not linked into
the production runtime publication.

The application-owned OS_OTHER VFS in `tests/sqlite_nofs.c` selects the spike's
memory-only profile with fixed random bytes and clock. SQLite's own memdb
reads the caller-provided image. This profile has no disk volume and is not
an implementation of the general filesystem/device scope retained by brt-zct.
