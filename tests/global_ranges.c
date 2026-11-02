/* Independent byte-set oracle for private global-range indexing. No fake
 * address is dereferenced; the actual checker only validates the interval. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include "../src/native/layout.h"
const struct bennett_native_layout __bennett_rt_native_layout={1,0x30000000,0};
const struct bennett_native_object __bennett_rt_native_objects[]={
    {(void *)0x4020,1},{(void *)0x4012,1},{(void *)0x4003,3},
    {(void *)0x4010,7},{(void *)0x4000,3},{(void *)0x4020,4},
    {(void *)0x4011,2},{(void *)0x4030,0},
    {(void *)(UINTPTR_MAX-2),8}
};
const uint64_t __bennett_rt_native_object_count=sizeof __bennett_rt_native_objects/sizeof *__bennett_rt_native_objects;
static jmp_buf fault;
_Noreturn void __bennett_nz_fault(uint64_t code) { assert(code==7); longjmp(fault,1); }
static int reference(uintptr_t p,uint64_t n) {
    if (!n) return 1;
    if (n>UINTPTR_MAX-p) return 0;
    for (uint64_t i=0;i<__bennett_rt_native_object_count;i++) {
        const struct bennett_native_object *o=__bennett_rt_native_objects+i;
        int all=1;
        for (uint64_t byte=0;byte<n;byte++) {
            uintptr_t a=p+byte,base=(uintptr_t)o->base;
            if (a<base || a-base>=o->size) { all=0; break; }
        }
        if (all) return 1;
    }
    return 0;
}
static void probe(uintptr_t p,uint64_t n) {
    int expected=reference(p,n);
    volatile int valid=0;
    if (!setjmp(fault)) { __bennett_rt_native_check((void *)p,n); valid=1; }
    assert(valid==expected);
}
int main(void) {
    for (uintptr_t p=0x3ff0;p<=0x4050;p++) for (uint64_t n=0;n<=16;n++) probe(p,n);
    for (uintptr_t d=0;d<12;d++) for (uint64_t n=0;n<=16;n++) probe(UINTPTR_MAX-d,n);
    probe(0,0); probe(0,1);
    return 0;
}
