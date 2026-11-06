#ifndef BENNETT_INTEGER_H
#define BENNETT_INTEGER_H
#include <stdint.h>
/* LLVM Language Reference: Integer Type admits widths 1 through 2^23.
 * Internal limb storage must never be sized by an unchecked caller width. */
#define BRT_INTEGER_MAX_WIDTH UINT32_C(8388608)
#define BRT_INT_CTPOP 0
#define BRT_INT_BITREVERSE 1
#define BRT_INT_BSWAP 2
#define BRT_INT_CTLZ 3
#define BRT_INT_CTTZ 4
#define BRT_INT_PDEP 5
#define BRT_INT_PEXT 6
#define BRT_INT_CLMUL 7
#define BRT_INT_FSHL 8
#define BRT_INT_FSHR 9
#define BRT_INT_SHL 10
#define BRT_INT_LSHR 11
#define BRT_INT_ASHR 12
#define BRT_INT_SSHL_SAT 13
#define BRT_INT_USHL_SAT 14
#define BRT_INT_MUL 15
#define BRT_INT_UDIV 16
#define BRT_INT_SDIV 17
#define BRT_INT_UREM 18
#define BRT_INT_SREM 19
#define BRT_INT_SMUL_FIX 20
#define BRT_INT_UMUL_FIX 21
#define BRT_INT_SMUL_FIX_SAT 22
#define BRT_INT_UMUL_FIX_SAT 23
#define BRT_INT_SDIV_FIX 24
#define BRT_INT_UDIV_FIX 25
#define BRT_INT_SDIV_FIX_SAT 26
#define BRT_INT_UDIV_FIX_SAT 27
#define BRT_INT_SMUL_WITH_OVERFLOW 28
#define BRT_INT_UMUL_WITH_OVERFLOW 29
uint32_t __bennett_rt_integer_op(uint64_t *,const uint64_t *,const uint64_t *,const uint64_t *,uint32_t,uint32_t,uint32_t);
uint32_t __bennett_rt_integer_arithmetic(uint64_t *,const uint64_t *,const uint64_t *,uint32_t,uint32_t,uint32_t);
#endif
