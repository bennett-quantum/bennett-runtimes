/* Native comparison only: real Linux SIGABRT termination for compiler-rt.
 * No signal API or sanitizer handler is simulated. No host libc dependency. */
#include "layout.h"
_Noreturn void abort(void) {
    const unsigned long mask = 1UL << (6-1);
    const unsigned long action[4] = {0,0,0,0}; /* SIG_DFL, flags, restorer, mask */
    if (__bennett_rt_native_os(13,6,(long)action,0,8,0,0) < 0 ||
        __bennett_rt_native_os(14,1,(long)&mask,0,8,0,0) < 0)
        __bennett_nz_fault(8);
    long pid=__bennett_rt_native_os(39,0,0,0,0,0,0);
    long tid=__bennett_rt_native_os(186,0,0,0,0,0,0);
    __bennett_rt_native_os(234,pid,tid,6,0,0,0);
    __bennett_nz_fault(8); /* Linux failed to terminate; never return. */
    __builtin_unreachable();
}
