/* Same musl/console framing and unmodelled-effect policy as the device.
 * This file is exclusively the native comparison build of that bridge. */
#define BENNETT_NATIVE_COMPARISON 1
#include "../musl/syscall.c"

/* Native twin of top-level console output. L/private-oracle calls continue
 * using the unchanged ordinary __bennett_dev_console_write buffer model.
 * Do not define an effect marker or route arbitrary syscalls to Linux. */
void __bennett_rt_native_console_write(const void *p, uint64_t len) {
    if (__bennett_rt_native_objects) __bennett_rt_native_check(p,len);
    if (len && !p) __bennett_nz_fault(7);
    const unsigned char *bytes=p;
    while (len) {
        long n=__bennett_rt_native_os(1,1,(long)bytes,len,0,0,0);
        if (n==-4) continue; /* EINTR */
        if (n<=0 || (uint64_t)n>len) __bennett_nz_fault(8);
        bytes+=n; len-=n;
    }
}
