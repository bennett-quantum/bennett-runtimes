/* Small independent arithmetic oracle for the instrumented host harness. */
#include <assert.h>
#include <stdint.h>
#include "../src/runtime/integer.h"
_Noreturn void __bennett_nz_fault(uint64_t code) { (void)code; assert(0); __builtin_trap(); }
int main(void) {
    for(uint32_t width=1;width<=8;++width) {
        uint64_t mask=(UINT64_C(1)<<width)-1;
        for(uint64_t a=0;a<=mask;++a) {
            uint64_t out=0, count=0, reverse=0;
            for(unsigned i=0;i<width;++i) { count+=(a>>i)&1; reverse|=((a>>i)&1)<<(width-1-i); }
            __bennett_rt_integer_op(&out,&a,0,0,width,BRT_INT_CTPOP,0); assert(out==count);
            __bennett_rt_integer_op(&out,&a,0,0,width,BRT_INT_BITREVERSE,0); assert(out==reverse);
            for(uint64_t b=1;b<=mask;++b) {
                __bennett_rt_integer_op(&out,&a,&b,0,width,BRT_INT_MUL,0); assert(out==((a*b)&mask));
                __bennett_rt_integer_op(&out,&a,&b,0,width,BRT_INT_UDIV,0); assert(out==a/b);
                __bennett_rt_integer_op(&out,&a,&b,0,width,BRT_INT_UREM,0); assert(out==a%b);
            }
        }
    }
    return 0;
}
