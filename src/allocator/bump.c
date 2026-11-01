/* Dialect §§3.1,16.9,17.1: allocation is an ISA primitive, not
 * a private C arena. Its live table and nonreclaiming history belong to ISA.
 * The native comparison link supplies the same primitive semantics separately.
 */
#include <stddef.h>
#include <stdint.h>

extern void *__bennett_nz_alloc(uint64_t);
extern void __bennett_nz_free(void *);
extern void __bennett_nz_fault(uint64_t);
extern void __bennett_rt_memset(void *, unsigned char, uint64_t);

/* All runtime primitive allocation sites use this entry so the wrapper knows
 * the exact next byte address. Initially the published heap base is page
 * aligned. This state is ordinary device memory and reverses with the caller.
 * Direct primitive users retain the exact byte-bump ABI. */
static uintptr_t next_byte;
void *__bennett_rt_alloc(uint64_t n) {
    void *p = __bennett_nz_alloc(n);
    next_byte = (uintptr_t)p + (n ? n : 1);
    return p;
}

void *malloc(size_t n) {
    const size_t a = _Alignof(max_align_t);
    size_t extent = n ? n : 1;
    if (extent > SIZE_MAX - (a - 1)) {
        __bennett_nz_fault(2);
        __builtin_unreachable();
    }
    extent = (extent + a - 1) / a * a;
    size_t padding = (-(uintptr_t)next_byte) % a;
    if (padding) __bennett_rt_alloc(padding);
    void *p = __bennett_rt_alloc(extent);
    /* Explicit even when the primitive already zeroes: no host libc. */
    __bennett_rt_memset(p, 0, extent);
    return p;
}

void *calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        __bennett_nz_fault(2); /* FAULT_BUDGET_HEAP; no mutation on overflow */
        __builtin_unreachable();
    }
    return malloc(count * size);
}

void free(void *p) {
    if (p) __bennett_nz_free(p); /* primitive rejects interior/dead/nonheap */
}
