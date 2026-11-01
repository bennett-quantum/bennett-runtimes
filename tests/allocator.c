/* Linked with the published bitcode, never the host allocator. */
#include <stddef.h>
#include <stdint.h>
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void free(void *);
extern void *__bennett_rt_alloc(uint64_t);
extern void __bennett_rt_memset(void *, unsigned char, uint64_t);
extern void __bennett_rt_memmove(void *, const void *, uint64_t);
extern void *memset(void *, int, size_t);

static void *volatile observed;
extern const uint64_t __bennett_rt_native_layout[3];
int main(int argc, char **argv) {
    unsigned char *p = malloc(32);
    if ((uintptr_t)p != __bennett_rt_native_layout[1]) return 10;
    for (size_t i = 0; i < 32; ++i) if (p[i]) return 11;
    __bennett_rt_memset(p, 0xa5, 32);
    if (argc > 1) {
        switch (argv[1][0]) {
        case 'd': free(p); free(p); return 20;
        case 'i': free(p + 1); return 21;
        case 'g': free((void *)(uintptr_t)0x1234); return 22;
        case 'o': observed=malloc(SIZE_MAX); return 23;
        case 'c': observed=calloc(SIZE_MAX / 2 + 1, 2); return 24;
        case 'h': for (;;) { void *q = malloc(128); free(q); } /* history exhausts */
        case 't': for (;;) observed=malloc(0); /* allocation table exhausts */
        }
        return 25;
    }
    unsigned char *q = calloc(3, 7);
    if ((uintptr_t)q != (uintptr_t)p + 64) return 30;
    for (size_t i = 0; i < 21; ++i) if (q[i]) return 31;
    free(NULL);
    free(p);
    unsigned char *r = malloc(5);
    if ((uintptr_t)r != (uintptr_t)q + 64) return 32;
    for (size_t i = 0; i < 5; ++i) if (r[i]) return 33;
    unsigned char *z = malloc(0), *z2 = calloc(0, SIZE_MAX);
    if ((uintptr_t)z2 != (uintptr_t)z + _Alignof(max_align_t) + 32) return 34;
    free(z); free(z2); free(q);
    for (size_t i = 0; i < 5; ++i) r[i] = (unsigned char)i;
    __bennett_rt_memmove(r + 1, r, 4);
    if (r[0] != 0 || r[1] != 0 || r[2] != 1 || r[3] != 2 || r[4] != 3) return 35;
    __bennett_rt_memmove(r, r + 1, 4);
    if (r[0] != 0 || r[1] != 1 || r[2] != 2 || r[3] != 3) return 36;
    if (memset(r, 0x1ff, 5) != r) return 37;
    for (size_t i = 0; i < 5; ++i) if (r[i] != 255) return 38;
    __bennett_rt_memmove(NULL, NULL, 0);
    __bennett_rt_memset(NULL, 0, 0);
    free(r);
    unsigned char *odd = __bennett_rt_alloc(3);
    unsigned char *aligned = malloc(1);
    if ((uintptr_t)aligned != (uintptr_t)odd + _Alignof(max_align_t)) return 39;
    if ((uintptr_t)aligned % _Alignof(max_align_t)) return 40;
    free(odd); free(aligned);
    return 0;
}
