"""Offline, pinned musl slice builder; upstream files remain unmodified."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile

VERSION='1.2.5'
URL='https://musl.libc.org/releases/musl-1.2.5.tar.gz'
SHA='a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'
# Explicit closure, including real vfprintf (not just the optimised puts route),
# wide conversions, stdio flushing and exit handlers. No musl allocator objects.
FILES='''stdio/printf stdio/vfprintf stdio/puts stdio/fputs stdio/fwrite
stdio/__towrite stdio/__overflow stdio/__stdio_write
stdio/__stdout_write stdio/__stdio_close stdio/__stdio_seek stdio/stdout
stdio/stderr stdio/__stdio_exit stdio/__lockfile stdio/ofl stdio/fflush
stdio/putc stdio/fputc stdio/putchar stdio/__lockfile
exit/exit exit/_Exit exit/atexit unistd/write unistd/writev unistd/lseek
unistd/close internal/syscall_ret thread/__syscall_cp thread/__lock
errno/__errno_location string/strlen string/strnlen string/memcpy
string/memmove string/memchr string/strchrnul string/strchr string/memcmp string/strcmp
multibyte/wctomb multibyte/wcrtomb ctype/iswprint math/frexpl math/frexp math/__fpclassifyl math/__signbitl errno/strerror locale/__lctrans locale/__mo_lookup
'''.split()

def prepare(root, out, scratch):
    archive=root/f'vendor/musl-{VERSION}.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==SHA, 'musl source digest mismatch'
    with tarfile.open(archive) as tar:
        tar.extractall(scratch, filter='data')
    src=scratch/f'musl-{VERSION}'
    inc=out/'musl-sysroot/include'
    if inc.exists(): shutil.rmtree(inc)
    shutil.copytree(src/'include',inc)
    for arch in ('generic','x86_64'):
        shutil.copytree(src/f'arch/{arch}/bits',inc/'bits',dirs_exist_ok=True)
    alltypes=subprocess.check_output(['sed','-f',src/'tools/mkalltypes.sed',src/'arch/x86_64/bits/alltypes.h.in',src/'include/alltypes.h.in'])
    (inc/'bits/alltypes.h').write_bytes(alltypes)
    syscalls=(src/'arch/x86_64/bits/syscall.h.in').read_text()
    (inc/'bits/syscall.h').write_text(syscalls+syscalls.replace('__NR_','SYS_'))
    for p in inc.rglob('*.in'): p.unlink()
    overlays=scratch/'overlays'; overlays.mkdir(exist_ok=True)
    (overlays/'pthread_arch.h').write_text('extern void *__bennett_rt_thread_pointer(void);\nstatic inline uintptr_t __get_tp(void) { return (uintptr_t)__bennett_rt_thread_pointer(); }\n#define MC_PC gregs[REG_RIP]\n')
    syscall='extern long __bennett_rt_syscall(long,long,long,long,long,long,long);\n#define __SYSCALL_LL_E(x) (x)\n#define __SYSCALL_LL_O(x) (x)\n'
    for n in range(7):
        args=['long nr']+[f'long a{i}' for i in range(n)]
        vals=['nr']+[f'a{i}' for i in range(n)]+['0']*(6-n)
        syscall+=f'static inline long __syscall{n}({",".join(args)}) {{ return __bennett_rt_syscall({",".join(vals)}); }}\n'
    (overlays/'syscall_arch.h').write_text(syscall)
    lib=out/'musl-sysroot/lib'; lib.mkdir(parents=True,exist_ok=True)
    for name in ('start.S','start.c','syscall.c','heap.c'): shutil.copyfile(root/'src/native'/name,lib/name)
    shutil.copyfile(src/'COPYRIGHT',out/'musl-sysroot/COPYRIGHT')
    flags=['--target=x86_64-unknown-linux-musl','-std=c99','-O2','-flto','-emit-llvm','-ffreestanding','-fno-builtin','-fno-vectorize','-fno-slp-vectorize','-fno-ident','-Wno-deprecated-non-prototype','-Wno-shift-op-parentheses','-fno-stack-protector','-nostdinc','-D_XOPEN_SOURCE=700']
    for path in (overlays,src/'arch/x86_64',src/'arch/generic',src/'src/include',src/'src/internal',inc):
        flags+=['-I',str(path)]
    files=[src/f'src/{f}.c' for f in sorted(set(FILES))]+[root/'src/musl/context.c']
    return files,flags
