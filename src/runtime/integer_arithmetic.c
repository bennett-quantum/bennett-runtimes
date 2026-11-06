/* Limb multiplication and restoring division. All arithmetic is unsigned;
 * signed magnitudes and true-width two's complement avoid C signed overflow.
 * No host bigint, division library, FP, or integer intrinsic is used. */
#include "integer.h"
#include <stddef.h>
extern _Noreturn void __bennett_nz_fault(uint64_t);
static uint64_t mask(uint32_t width) {
    return width%64?(UINT64_C(1)<<(width%64))-1:UINT64_MAX;
}
static void negate(uint64_t *a,uint64_t limbs,uint32_t width) {
    uint64_t carry=1;
    for(uint64_t k=0;k<limbs;++k) {
        a[k]=~a[k]+carry; carry=carry && !a[k];
    }
    a[limbs-1]&=mask(width);
}
static unsigned sign(const uint64_t *a,uint32_t width) {
    return (a[(width-1)/64]>>((width-1)%64))&1;
}
static unsigned get(const uint64_t *a,uint32_t bit) { return (a[bit/64]>>(bit%64))&1; }
static void copy_operand(uint64_t *out,const uint64_t *in,uint64_t limbs,uint32_t width,int negative) {
    for(uint64_t k=0;k<limbs;++k) out[k]=((const volatile uint64_t *)in)[k];
    out[limbs-1]&=mask(width);
    if(negative) negate(out,limbs,width);
}
static uint64_t limit_word(uint64_t k,uint32_t width,int is_signed,int negative) {
    uint64_t top=(width-1)/64;
    if(k>top) return 0;
    uint64_t bit=UINT64_C(1)<<((width-1)%64);
    if(is_signed && negative) return k==top?bit:0;
    if(k<top) return UINT64_MAX;
    return mask(width)&(is_signed?~bit:UINT64_MAX);
}
static unsigned overflow(const uint64_t *a,uint64_t limbs,uint32_t width,int is_signed,int negative) {
    for(uint64_t k=limbs;k;--k) {
        uint64_t bound=limit_word(k-1,width,is_signed,negative);
        if(a[k-1]!=bound) return a[k-1]>bound;
    }
    return 0;
}
static void store(uint64_t *dst,const uint64_t *a,uint64_t limbs,uint32_t width,int is_signed,int negative,int saturating,unsigned excess) {
    if(saturating && excess) {
        for(uint64_t k=0;k<limbs;++k) dst[k]=limit_word(k,width,is_signed,negative);
    } else {
        for(uint64_t k=0;k<limbs;++k) dst[k]=a[k];
        dst[limbs-1]&=mask(width);
        if(negative) negate(dst,limbs,width);
    }
}
static void right(uint64_t *a,uint64_t limbs,uint32_t n) {
    if(!n) return;
    uint64_t words=n/64; unsigned bits=n%64;
    for(uint64_t k=0;k<limbs;++k) {
        uint64_t value=k+words<limbs?a[k+words]>>bits:0;
        if(bits && k+words+1<limbs) value|=a[k+words+1]<<(64-bits);
        a[k]=value;
    }
}
static void increment(uint64_t *a,uint64_t limbs) {
    for(uint64_t k=0;k<limbs;++k) if(++a[k]) break;
}
static int compare(const uint64_t *a,const uint64_t *b,uint64_t limbs) {
    for(uint64_t k=limbs;k;--k) if(a[k-1]!=b[k-1]) return a[k-1]>b[k-1]?1:-1;
    return 0;
}
static void subtract(uint64_t *a,const uint64_t *b,uint64_t limbs) {
    uint64_t borrow=0;
    for(uint64_t k=0;k<limbs;++k) {
        uint64_t t=a[k]-b[k], next=a[k]<b[k];
        a[k]=t-borrow; borrow=next || t<borrow;
    }
}
uint32_t __bennett_rt_integer_arithmetic(uint64_t *dst,const uint64_t *left,const uint64_t *right_operand,
                                       uint32_t width,uint32_t op,uint32_t scale) {
    if(!width || width>BRT_INTEGER_MAX_WIDTH) __bennett_nz_fault(4);
    uint64_t limbs=((uint64_t)width+63)/64;
    enum { SIGNED=1, FIXED=2, SATURATING=4, PRODUCT=8, REMAINDER=16 };
    static const unsigned properties[] = {
        [BRT_INT_MUL]=PRODUCT, [BRT_INT_UDIV]=0, [BRT_INT_SDIV]=SIGNED,
        [BRT_INT_UREM]=REMAINDER, [BRT_INT_SREM]=SIGNED|REMAINDER,
        [BRT_INT_SMUL_FIX]=SIGNED|FIXED|PRODUCT, [BRT_INT_UMUL_FIX]=FIXED|PRODUCT,
        [BRT_INT_SMUL_FIX_SAT]=SIGNED|FIXED|SATURATING|PRODUCT, [BRT_INT_UMUL_FIX_SAT]=FIXED|SATURATING|PRODUCT,
        [BRT_INT_SDIV_FIX]=SIGNED|FIXED, [BRT_INT_UDIV_FIX]=FIXED,
        [BRT_INT_SDIV_FIX_SAT]=SIGNED|FIXED|SATURATING, [BRT_INT_UDIV_FIX_SAT]=FIXED|SATURATING,
        [BRT_INT_SMUL_WITH_OVERFLOW]=SIGNED|PRODUCT, [BRT_INT_UMUL_WITH_OVERFLOW]=PRODUCT
    };
    if(op<BRT_INT_MUL || op>BRT_INT_UMUL_WITH_OVERFLOW) __bennett_nz_fault(4);
    unsigned traits=properties[op];
    int signed_op=!!(traits&SIGNED);
    int negative_left=signed_op && sign(left,width), negative_right=signed_op && sign(right_operand,width);
    int negative=negative_left!=negative_right;
    int fixed=!!(traits&FIXED), saturating=!!(traits&SATURATING);
    if(fixed && scale>width) __bennett_nz_fault(4);
    if(!fixed) scale=0;
    if(traits&PRODUCT) {
        uint64_t a[limbs], b[limbs], product[2*limbs];
        copy_operand(a,left,limbs,width,negative_left); copy_operand(b,right_operand,limbs,width,negative_right);
        for(uint64_t k=0;k<2*limbs;++k) product[k]=0;
        for(uint64_t i=0;i<limbs;++i) {
            uint64_t carry=0;
            for(uint64_t j=0;j<limbs;++j) {
                __uint128_t p=(__uint128_t)a[i]*b[j]+product[i+j]+carry;
                product[i+j]=(uint64_t)p; carry=(uint64_t)(p>>64);
            }
            product[i+limbs]=carry;
        }
        unsigned discarded=0;
        if(negative) for(uint32_t bit=0;bit<scale;++bit) discarded|=get(product,bit);
        right(product,2*limbs,scale);
        /* Signed fixed multiply chooses arithmetic-right-shift rounding. */
        if(discarded) increment(product,2*limbs);
        unsigned excess=overflow(product,2*limbs,width,signed_op,negative);
        store(dst,product,limbs,width,signed_op,negative,saturating,excess);
        return excess;
    }
    {
        uint64_t numerator[2*limbs], denominator[limbs+1], remainder[limbs+1], quotient[2*limbs];
        copy_operand(numerator,left,limbs,width,negative_left);
        copy_operand(denominator,right_operand,limbs,width,negative_right);
        uint64_t nonzero=0;
        for(uint64_t k=0;k<limbs;++k) nonzero|=denominator[k];
        if(!nonzero) __bennett_nz_fault(5);
        if((op==BRT_INT_SDIV || op==BRT_INT_SREM) && negative_left && negative_right) {
            unsigned minimum=1, minus_one=denominator[0]==1;
            for(uint64_t k=0;k<limbs;++k) {
                minimum&=numerator[k]==limit_word(k,width,1,1);
                if(k) minus_one&=denominator[k]==0;
            }
            if(minimum && minus_one) __bennett_nz_fault(6);
        }
        for(uint64_t k=limbs;k<2*limbs;++k) numerator[k]=0;
        if(scale) {
            unsigned bits=scale%64; uint64_t words=scale/64;
            for(uint64_t k=2*limbs;k;--k) {
                uint64_t index=k-1, value=index>=words?numerator[index-words]<<bits:0;
                if(bits && index>words) value|=numerator[index-words-1]>>(64-bits);
                numerator[index]=value;
            }
        }
        denominator[limbs]=0;
        for(uint64_t k=0;k<=limbs;++k) remainder[k]=0;
        for(uint64_t k=0;k<2*limbs;++k) quotient[k]=0;
        for(uint32_t bit=(uint32_t)(2*limbs*64);bit;--bit) {
            uint64_t carry=get(numerator,bit-1);
            for(uint64_t k=0;k<=limbs;++k) {
                uint64_t next=remainder[k]>>63; remainder[k]=(remainder[k]<<1)|carry; carry=next;
            }
            if(compare(remainder,denominator,limbs+1)>=0) {
                subtract(remainder,denominator,limbs+1);
                quotient[(bit-1)/64]|=UINT64_C(1)<<((bit-1)%64);
            }
        }
        if(traits&REMAINDER) {
            store(dst,remainder,limbs,width,signed_op,negative_left,0,0); return 0;
        }
        unsigned excess=overflow(quotient,2*limbs,width,signed_op,negative);
        if(fixed && !saturating && excess) __bennett_nz_fault(4); /* source UB, not a wrapped successful division */
        store(dst,quotient,limbs,width,signed_op,negative,saturating,excess); return excess;
    }
}
