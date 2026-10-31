/* N26 helpers. Volatile byte accesses keep stock LTO from re-forming an
 * intrinsic inside the helper that implements it (normalisation drops volatile
 * under N9). Device/native bounds instrumentation remains the driver's job.
 */
#include <stddef.h>
#include <stdint.h>

void __bennett_rt_memset(void *dst, unsigned char value, uint64_t n) {
    volatile unsigned char *d = dst;
    for (uint64_t i = 0; i < n; ++i) d[i] = value;
}

void __bennett_rt_memmove(void *dst, const void *src, uint64_t n) {
    volatile unsigned char *d = dst;
    const volatile unsigned char *s = src;
    if ((uintptr_t)dst <= (uintptr_t)src) {
        for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        while (n) { --n; d[n] = s[n]; }
    }
}

/* Native code generation of corpus llvm.memset may call this spelling. */
void *memset(void *dst, int value, size_t n) {
    __bennett_rt_memset(dst, (unsigned char)value, n);
    return dst;
}
