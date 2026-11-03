/* Width-independent limb algorithms. Typed entry bodies are emitted by the
 * producer build; this internal operation selector is not a request ABI. */
#include "integer.h"
#include <stddef.h>
extern _Noreturn void __bennett_nz_fault(uint64_t);
static unsigned bit(const uint64_t *a,uint32_t i) {
    /* Preserve these actual operand-slot loads through consumer LTO. A bit
     * helper must not be refolded into the intrinsic it implements. */
    const volatile uint64_t *slot=a+i/64;
    return (*slot>>(i%64))&1;
}
static void set(uint64_t *a,uint32_t i,unsigned value) {
    a[i/64]|=(uint64_t)value<<(i%64);
}
uint32_t __bennett_rt_integer_op(uint64_t *dst,const uint64_t *a,const uint64_t *b,const uint64_t *c,
                               uint32_t width,uint32_t op,uint32_t flag) {
    if(!width) __bennett_nz_fault(4);
    if(op>=BRT_INT_MUL) return __bennett_rt_integer_arithmetic(dst,a,b,width,op,flag);
    uint64_t limbs=((uint64_t)width+63)/64;
    uint64_t result[limbs];
    for(uint64_t k=0;k<limbs;++k) result[k]=0;
    uint32_t count=0;
    switch(op) {
    case BRT_INT_CTPOP:
        for(uint32_t i=0;i<width;++i) count+=bit(a,i);
        result[0]=count; break;
    case BRT_INT_CTLZ:
    case BRT_INT_CTTZ:
        while(count<width && !bit(a,op==BRT_INT_CTLZ?width-1-count:count)) ++count;
        result[0]=count==width && flag?0:count; break;
    case BRT_INT_BITREVERSE:
        for(uint32_t i=0;i<width;++i) set(result,width-1-i,bit(a,i));
        break;
    case BRT_INT_BSWAP:
        if(width%16) __bennett_nz_fault(4);
        for(uint32_t i=0;i<width;++i) set(result,(width/8-1-i/8)*8+i%8,bit(a,i));
        break;
    case BRT_INT_PDEP:
        for(uint32_t i=0,j=0;i<width;++i) if(bit(b,i)) set(result,i,bit(a,j++));
        break;
    case BRT_INT_PEXT:
        for(uint32_t i=0,j=0;i<width;++i) if(bit(b,i)) set(result,j++,bit(a,i));
        break;
    case BRT_INT_CLMUL:
        for(uint32_t i=0;i<width;++i) if(bit(a,i))
            for(uint32_t j=0;j<width-i;++j) result[(i+j)/64]^=(uint64_t)bit(b,j)<<((i+j)%64);
        break;
    case BRT_INT_FSHL:
    case BRT_INT_FSHR:
        for(uint32_t i=width;i;--i) count=(count*2+bit(c,i-1))%width;
        for(uint32_t i=0;i<width;++i) {
            unsigned value=op==BRT_INT_FSHL ? (i>=count?bit(a,i-count):bit(b,width-count+i))
                                          : (i+count<width?bit(b,i+count):bit(a,i+count-width));
            set(result,i,value);
        }
        break;
    case BRT_INT_SHL:
    case BRT_INT_LSHR:
    case BRT_INT_ASHR:
    case BRT_INT_SSHL_SAT:
    case BRT_INT_USHL_SAT: {
        uint64_t shift=0;
        for(uint32_t i=0;i<width;++i) if(bit(b,i)) {
            if(i>=32) { shift=width; break; }
            shift|=UINT64_C(1)<<i;
        }
        if(shift>=width) break; /* N1/N14: source poison becomes zero. */
        count=shift;
        unsigned sign=bit(a,width-1), overflow=0;
        if(op==BRT_INT_SSHL_SAT || op==BRT_INT_USHL_SAT) {
            uint32_t first=width-count-(op==BRT_INT_SSHL_SAT);
            for(uint32_t i=first;i<width;++i) overflow|=bit(a,i)^(op==BRT_INT_SSHL_SAT?sign:0);
        }
        if(overflow) {
            for(uint32_t i=0;i<width;++i) set(result,i,op==BRT_INT_USHL_SAT?1:i==width-1?sign:!sign);
        } else for(uint32_t i=0;i<width;++i) {
            unsigned value;
            if(op==BRT_INT_LSHR || op==BRT_INT_ASHR)
                value=i+count<width?bit(a,i+count):op==BRT_INT_ASHR?sign:0;
            else value=i>=count?bit(a,i-count):0;
            set(result,i,value);
        }
        break;
    }
    default: __bennett_nz_fault(4);
    }
    if(width%64) result[limbs-1]&=(UINT64_C(1)<<(width%64))-1;
    for(uint64_t k=0;k<limbs;++k) dst[k]=result[k];
    return 0;
}
