/* §11.2 console syscall bridge. musl's syscall_ret translates negative
 * errno results; genuine unmodelled operations remain fault 8. Other devices
 * and the VFS remain tracked by brt-zct, not silently passed to Linux. */
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#ifdef BENNETT_NATIVE_COMPARISON
#include "../native/layout.h"
extern void __bennett_rt_native_console_write(const void *, uint64_t);
#define CONSOLE_WRITE __bennett_rt_native_console_write
#else
extern void __bennett_effect_console_write(const void *, uint64_t);
#define CONSOLE_WRITE __bennett_effect_console_write
#endif
extern void __bennett_nz_fault(uint64_t);
struct iovec { void *base; size_t len; };
#ifndef BENNETT_NATIVE_COMPARISON
struct { int active, selector; void *object; } __bennett_rt_eh;
/* Dedicated returning §14.4/14.7 replacement. N28–N30 must remove noreturn,
 * rewrite _Exit/exit_group to this symbol, and propagate/catch the flag. */
void __bennett_rt_exit(int status) {
    __bennett_rt_eh.active=4;
    __bennett_rt_eh.selector=status;
    __bennett_rt_eh.object=0;
}
#endif
long __bennett_rt_syscall(long nr,long a,long b,long c,long d,long e,long f) {
    (void)d; (void)e; (void)f;
    if (nr==60 || nr==231) {
#ifdef BENNETT_NATIVE_COMPARISON
        __bennett_rt_native_os(231,a,0,0,0,0,0);
        __builtin_trap();
#else
        __bennett_rt_exit((int)a); return 0;
#endif
    }
    if (a==1 || a==2) {
        if (nr==1) { /* write */
            if ((unsigned long)c>LONG_MAX) return -22;
            #ifdef BENNETT_NATIVE_COMPARISON
            /* Preserve diagnostic stderr (compiler-rt uses musl write(2)). */
            if (a==2) {
                long r;
                do { r=__bennett_rt_native_os(1,a,b,c,0,0,0); } while (r==-4);
                return r;
            }
#endif
            if (c) CONSOLE_WRITE((void *)(uintptr_t)b,c);
            return c;
        }
        if (nr==20) { /* writev: validate aggregate before first effect */
            if ((unsigned long)c>1024) return -22;
            if (c && !b) return -14;
            const struct iovec *v=(const void *)(uintptr_t)b;
            size_t total=0;
            for (long i=0;i<c;++i) {
                if (v[i].len>LONG_MAX-total) return -22;
                total+=v[i].len;
            }
            for (long i=0;i<c;++i)
                if (v[i].len) CONSOLE_WRITE(v[i].base,v[i].len);
            return total;
        }
        if (nr==16 && b==0x5413) return -25; /* ioctl(TIOCGWINSZ): ENOTTY */
        if (nr==8) return -29; /* lseek: ESPIPE */
        if (nr==3) return 0; /* virtual close, no host resource */
    }
    __bennett_nz_fault(8);
    return -38; /* unreachable after the tier's terminal fault */
}
