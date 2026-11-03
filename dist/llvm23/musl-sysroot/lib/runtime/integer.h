#ifndef BENNETT_INTEGER_H
#define BENNETT_INTEGER_H
#include <stdint.h>
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
uint32_t __bennett_rt_integer_op(uint64_t *,const uint64_t *,const uint64_t *,const uint64_t *,uint32_t,uint32_t,uint32_t);
#endif
