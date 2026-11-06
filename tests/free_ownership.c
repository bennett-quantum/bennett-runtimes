/* A C free must reject a live primitive allocation with no C record. */
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
static _Alignas(4096) unsigned char arena[4096];
static size_t cursor;
static unsigned freed;
void *__bennett_nz_alloc(uint64_t n) {
    void *p=arena+cursor; cursor+=n?n:1; return p;
}
void __bennett_nz_free(void *p) { (void)p; ++freed; }
_Noreturn void __bennett_nz_fault(uint64_t code) { _exit(freed?99:100+code); }
extern void *__bennett_rt_alloc(uint64_t);
extern void test_free(void *);
int main(void) {
    test_free(0);
    if(freed) return 1;
    test_free(__bennett_rt_alloc(8));
    return 2;
}
