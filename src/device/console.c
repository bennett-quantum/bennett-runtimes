/* Dialect §§11.3, TDL-12. All mutable state is ordinary memory: the
 * compiler lifts/reverses it. It is never a host output buffer in R. */
#include <stdint.h>
extern void __bennett_nz_fault(uint64_t);
unsigned char *__bennett_dev_console_buffer;
uint64_t __bennett_dev_console_L;
uint64_t __bennett_dev_console_n;
uint32_t __bennett_dev_console_bound;
/* 0 = private oracle (discard excess); 1 = action (capacity is a bound). */
uint32_t __bennett_dev_console_action;

void __bennett_dev_console_write(const unsigned char *p, uint64_t len) {
    if (!__bennett_dev_console_bound) {
        __bennett_nz_fault(8);
        return;
    }
    uint64_t n=__bennett_dev_console_n, limit=__bennett_dev_console_L;
    if (n>limit || (limit && !__bennett_dev_console_buffer)) {
        __bennett_nz_fault(7);
        return;
    }
    uint64_t count=limit-n;
    if (__bennett_dev_console_action && len>count) {
        __bennett_nz_fault(2); /* capacity exhausted, before any mutation */
        return;
    }
    if (count>len) count=len;
    if (count && !p) {
        __bennett_nz_fault(7);
        return;
    }
    for (uint64_t i=0; i<count; ++i)
        __bennett_dev_console_buffer[n+i]=p[i];
    __bennett_dev_console_n=n+count;
}
