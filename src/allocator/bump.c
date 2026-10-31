/* Dialect 0.2.10 §§3.1,16.9,17.1: allocation is an ISA primitive, not
 * a private C arena. Its live table and nonreclaiming history belong to ISA.
 * The native comparison link supplies the same primitive semantics separately.
 */
#include <stddef.h>
#include <stdint.h>

extern void *__bennett_nz_alloc(uint64_t);
extern void __bennett_nz_free(void *);
extern void __bennett_nz_fault(uint64_t);
extern void __bennett_rt_memset(void *, unsigned char, uint64_t);

void *malloc(size_t n) {
    void *p = __bennett_nz_alloc(n);
    /* Explicit even when the primitive already zeroes: no host libc. */
    __bennett_rt_memset(p, 0, n);
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
