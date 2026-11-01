/* Hosted fault-catching harness for the actual native support, not a second
 * checker. The materialised binaries are separately audited as freestanding. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
extern void *__bennett_nz_alloc(uint64_t);
extern void __bennett_nz_free(void *);
extern void __bennett_rt_native_check(const void *,uint64_t);
const uint64_t __bennett_rt_native_layout[3]={1,0x1d000000,32};
static unsigned char global[3];
const struct { const void *base; uint64_t size; } __bennett_rt_native_objects[]={{global,3}};
const uint64_t __bennett_rt_native_object_count=1;
static jmp_buf target;
static uint64_t code;
_Noreturn void __bennett_nz_fault(uint64_t c) { code=c; longjmp(target,1); }
static uint64_t check(uintptr_t p,uint64_t n) {
    if (!setjmp(target)) { __bennett_rt_native_check((void *)p,n); return 0; }
    return code;
}
int main(void) {
    uintptr_t base=(uintptr_t)__bennett_nz_alloc(3);
    assert(base==__bennett_rt_native_layout[1]);
    assert((uintptr_t)__bennett_nz_alloc(0)==base+3);
    assert((uintptr_t)__bennett_nz_alloc(5)==base+4);
    for (unsigned dead=0;dead<2;dead++) {
        for (int offset=-1;offset<=16;offset++) for (unsigned n=0;n<=17;n++) {
            int first=!dead && offset>=0 && offset<=3 && n<=3-(unsigned)offset;
            int second=offset>=4 && offset<=9 && n<=9-(unsigned)offset;
            uint64_t expected=(!n || first || second)?0:7;
            assert(check(base+offset,n)==expected);
        }
        if (!dead) __bennett_nz_free((void *)base);
    }
    for (int offset=-1;offset<=4;offset++) for (unsigned n=0;n<=5;n++) {
        int valid=offset>=0 && offset<=3 && n<=3-(unsigned)offset;
        assert(check((uintptr_t)global+offset,n)==((!n || valid)?0:7));
    }
    assert(check(UINTPTR_MAX,16)==7);
    assert(check(0,0)==0 && check(0,1)==7);
    return 0;
}
