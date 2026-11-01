/* Shared deterministic environment, dialect §13.5. C's public musl encoding
 * is translated to the published round 0..4 and five IEEE sticky flag bits.
 * No host FP register, signal, TLS or trap state participates. */
#include <fenv.h>
#include <stddef.h>
#include <stdint.h>
struct bennett_fenv { uint32_t round, flags; };
/* The dedicated ordinary data section prevents LLVM from promoting this
 * externally specified alignment to its preferred 8-byte alignment. */
_Alignas(4) struct bennett_fenv __bennett_rt_fenv
    __attribute__((section(".data.__bennett_rt_fenv"))) = {0,0};
_Static_assert(sizeof(struct bennett_fenv)==8, "fenv size");
_Static_assert(_Alignof(struct bennett_fenv)==4, "fenv alignment");
_Static_assert(offsetof(struct bennett_fenv,flags)==4, "fenv field offsets");
extern _Noreturn void __bennett_nz_fault(uint64_t);
uint32_t __bennett_rt_fenv_get_round(void) { return __bennett_rt_fenv.round; }
uint32_t __bennett_rt_fenv_get_flags(void) { return __bennett_rt_fenv.flags; }
uint64_t __bennett_rt_fenv_get_env(void) {
    return (uint64_t)__bennett_rt_fenv.flags<<32 | __bennett_rt_fenv.round;
}
void __bennett_rt_fenv_set_round(uint32_t round) {
    if (round>4) __bennett_nz_fault(4);
    __bennett_rt_fenv.round=round;
}
void __bennett_rt_fenv_set_flags(uint32_t flags) {
    if (flags>31) __bennett_nz_fault(4);
    __bennett_rt_fenv.flags=flags;
}
void __bennett_rt_fenv_set_env(uint64_t env) {
    uint32_t round=(uint32_t)env, flags=(uint32_t)(env>>32);
    if (round>4 || flags>31) __bennett_nz_fault(4);
    __bennett_rt_fenv.round=round; __bennett_rt_fenv.flags=flags;
}
void __bennett_rt_fenv_reset_round(void) { __bennett_rt_fenv.round=0; }
void __bennett_rt_fenv_reset_flags(void) { __bennett_rt_fenv.flags=0; }
void __bennett_rt_fenv_reset(void) { __bennett_rt_fenv_set_env(0); }
static unsigned from_c(unsigned flags) { return (flags&1)|((flags>>1)&30); }
static unsigned to_c(unsigned flags) { return (flags&1)|((flags&30)<<1); }
int fegetround(void) {
    unsigned round=__bennett_rt_fenv.round;
    return round<=4 ? (int)(round<<10) : -1;
}
int fesetround(int round) {
    if (round<0 || round>FE_TONEARESTAWAY || (round&0x3ff)) return -1;
    __bennett_rt_fenv.round=(unsigned)round>>10; return 0;
}
int fetestexcept(int mask) { return to_c(__bennett_rt_fenv.flags)&mask&FE_ALL_EXCEPT; }
int feclearexcept(int mask) {
    __bennett_rt_fenv.flags&=~from_c(mask&FE_ALL_EXCEPT); return 0;
}
int feraiseexcept(int mask) {
    __bennett_rt_fenv.flags|=from_c(mask&FE_ALL_EXCEPT); return 0;
}
int fegetexceptflag(fexcept_t *out,int mask) { *out=fetestexcept(mask); return 0; }
int fesetexceptflag(const fexcept_t *in,int mask) {
    unsigned selected=from_c(mask&FE_ALL_EXCEPT);
    __bennett_rt_fenv.flags=(__bennett_rt_fenv.flags&~selected)|(from_c(*in)&selected);
    return 0;
}
int fegetenv(fenv_t *out) {
    *out=(fenv_t){0};
    out->__control_word=(unsigned short)(__bennett_rt_fenv.round<<10);
    out->__status_word=(unsigned short)to_c(__bennett_rt_fenv.flags);
    out->__mxcsr=(__bennett_rt_fenv.round<<13)|to_c(__bennett_rt_fenv.flags);
    return 0;
}
int fesetenv(const fenv_t *in) {
    if (in==FE_DFL_ENV) { __bennett_rt_fenv_reset(); return 0; }
    unsigned round=(in->__control_word>>10)&7;
    if (round>4) return -1;
    __bennett_rt_fenv.round=round;
    __bennett_rt_fenv.flags=from_c(in->__status_word&FE_ALL_EXCEPT);
    return 0;
}
int feholdexcept(fenv_t *out) { fegetenv(out); __bennett_rt_fenv_reset_flags(); return 0; }
int feupdateenv(const fenv_t *in) {
    unsigned flags=__bennett_rt_fenv.flags;
    int status=fesetenv(in);
    if (!status) __bennett_rt_fenv.flags|=flags;
    return status;
}
