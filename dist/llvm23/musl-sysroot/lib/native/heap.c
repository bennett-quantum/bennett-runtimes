/* Comparison-only lowering: never link into production. Dialect §§17.1,
 * 17.5,19.5; ISA §7.1 byte bump max(n,1), no reclaim, preflight bounds.
 * The driver supplies layout.h's required resolved layout object. */
#include <stddef.h>
#include <stdint.h>
#include "layout.h"
_Static_assert(sizeof(void *) == 8, "native backend requires x86_64 LP64");
struct allocation { uintptr_t base; uint64_t size, live; };
static struct allocation *history;
static uint64_t cursor, entries;
static int ready;

/* Preserve arbitrary fault codes in a stable decimal diagnostic, then trap.
 * Neither musl nor the console model is called on a failing path. */
_Noreturn void __bennett_nz_fault(uint64_t code) {
    char text[64] = "bennett runtime fault ";
    char digits[20]; size_t n=0, len=22;
    do { digits[n++]=(char)('0'+code%10); code/=10; } while(code);
    /* prefix is 22 bytes, excluding its terminating NUL. */
    while(n) text[len++]=digits[--n];
    text[len++]='\n';
    __bennett_rt_native_os(1,2,(long)text,len,0,0,0);
    __builtin_trap();
}

static void initialise(void) {
    const struct bennett_native_layout *l=&__bennett_rt_native_layout;
    if (ready) return;
    if (l->abi_version!=1 || l->heap_base<4096 || l->heap_base%4096 ||
        l->heap_limit_max>UINTPTR_MAX-l->heap_base ||
        l->heap_limit_max>SIZE_MAX/sizeof(*history)) __bennett_nz_fault(7);
    if (l->heap_limit_max) {
        /* MAP_FIXED_NOREPLACE: never overwrite globals/text, never relocate.
         * Linux may reject low addresses (mmap_min_addr); that is a loud fault.
         * Anonymous private mapping guarantees initial zero fill. */
        long p=__bennett_rt_native_os(9,l->heap_base,l->heap_limit_max,3,0x100022,-1,0);
        if ((uint64_t)p!=l->heap_base) __bennett_nz_fault(7);
        /* max(n,1) consumes at least one byte. One record per heap byte covers
         * EVERY possible history, including zero-sized/dead allocations. This
         * private native bookkeeping is never a device-visible allocation. */
        p=__bennett_rt_native_os(9,0,l->heap_limit_max*sizeof(*history),3,0x22,-1,0);
        if ((unsigned long)p>=(unsigned long)-4095) __bennett_nz_fault(2);
        history=(void *)p;
    }
    ready=1;
}
void *__bennett_nz_alloc(uint64_t n) {
    initialise();
    uint64_t extent=n?n:1, limit=__bennett_rt_native_layout.heap_limit_max;
    if (extent>limit-cursor) __bennett_nz_fault(2);
    unsigned char *p=(void *)(uintptr_t)(__bennett_rt_native_layout.heap_base+cursor);
    history[entries].base=(uintptr_t)p;
    history[entries].size=n;
    history[entries].live=1;
    ++entries; cursor+=extent;
    for (uint64_t i=0;i<n;++i) ((volatile unsigned char *)p)[i]=0;
    return p;
}
void __bennett_nz_free(void *p) {
    if (!p) return;
    for (uint64_t i=0;i<entries;++i) {
        if (history[i].base==(uintptr_t)p && history[i].live) {
            history[i].live=0; return;
        }
    }
    __bennett_nz_fault(7);
}
