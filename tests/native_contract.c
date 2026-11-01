/* Observable primitive boundary tests, no dependency on malloc recognition. */
#include <stdint.h>
#include <stdlib.h>
extern void *__bennett_nz_alloc(uint64_t);
extern void __bennett_nz_free(void *);
extern void __bennett_nz_fault(uint64_t);
extern long __bennett_rt_syscall(long,long,long,long,long,long,long);
extern const uint64_t __bennett_rt_native_layout[3];
int main(int argc, char **argv) {
    if (argc>1) {
        if (argv[1][0]=='f') __bennett_nz_fault(UINT64_MAX);
        if (argv[1][0]=='s') __bennett_rt_syscall(999,0,0,0,0,0,0);
    }
    uint64_t size=__bennett_rt_native_layout[2];
    if (!size) { __bennett_nz_alloc(0); return 1; }
    /* Saturate with zero-sized records, free each one. No hidden 65536 cap,
     * no reclamation, and exactly one byte consumed per zero allocation. */
    for (uint64_t i=0;i<size;++i) {
        void *p=__bennett_nz_alloc(0);
        if ((uintptr_t)p!=__bennett_rt_native_layout[1]+i) return 2;
        __bennett_nz_free(p);
    }
    __bennett_nz_alloc(1); /* must fault 2 */
    return 3;
}
