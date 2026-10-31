/* Comparison-only x86_64 Linux lowering of the §3.1 primitives. NEVER linked
 * into production bitcode: the plugin owns their device lowering. ISA 0.8
 * §7.1: exact byte bump, max(n,1), dead-mark free, no reclaim, preflight limits.
 * Link non-PIE with --section-start=.bennett_heap=0x10000000. The driver must
 * match that base and both configured bounds to its comparison layout.
 */
#include <stddef.h>
#include <stdint.h>

#ifndef BENNETT_NATIVE_HEAP_SIZE
#define BENNETT_NATIVE_HEAP_SIZE 16777216u
#endif
#ifndef BENNETT_NATIVE_ALLOCATIONS
#define BENNETT_NATIVE_ALLOCATIONS 65536u
#endif
_Static_assert(sizeof(void *) == 8, "native backend requires x86_64 LP64");
static unsigned char heap[BENNETT_NATIVE_HEAP_SIZE]
    __attribute__((section(".bennett_heap"), aligned(16)));
static struct { uintptr_t base; size_t size; unsigned live; }
    history[BENNETT_NATIVE_ALLOCATIONS];
static size_t cursor, entries;

/* A diagnostic without libc (including without stdio's own malloc). */
static void diagnostic(const char *s, size_t n) {
    register long nr __asm__("rax") = 1; /* Linux write(2, s, n) */
    register long fd __asm__("rdi") = 2;
    register const char *buf __asm__("rsi") = s;
    register size_t len __asm__("rdx") = n;
    __asm__ volatile("syscall" : "+a"(nr) : "D"(fd), "S"(buf), "d"(len)
                     : "rcx", "r11", "memory");
}
#define DIE(text) do { diagnostic(text "\n", sizeof(text "\n") - 1); __builtin_trap(); } while (0)

void __bennett_nz_fault(uint64_t code) {
    if (code == 2) DIE("bennett runtime fault 2: heap exhausted");
    if (code == 7) DIE("bennett runtime fault 7: invalid or double free");
    DIE("bennett runtime: native fault");
}

void *__bennett_nz_alloc(uint64_t n) {
    size_t extent = n ? n : 1;
    if ((uintptr_t)heap != 0x10000000u)
        DIE("bennett runtime: comparison heap linked at wrong base");
    if (extent > sizeof(heap) - cursor) {
        __bennett_nz_fault(2);
        __builtin_unreachable();
    }
    if (entries == BENNETT_NATIVE_ALLOCATIONS)
        DIE("bennett runtime fault 2: allocation table exhausted");
    unsigned char *p = heap + cursor;
    history[entries].base = (uintptr_t)p;
    history[entries].size = n;
    history[entries].live = 1;
    ++entries;
    cursor += extent;
    for (size_t i = 0; i < n; ++i) ((volatile unsigned char *)p)[i] = 0;
    return p;
}

void __bennett_nz_free(void *p) {
    for (size_t i = 0; i < entries; ++i) {
        if (history[i].base == (uintptr_t)p && history[i].live) {
            history[i].live = 0;
            return;
        }
    }
    __bennett_nz_fault(7);
    __builtin_unreachable();
}
