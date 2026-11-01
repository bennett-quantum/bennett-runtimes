#include <fenv.h>
#include <stddef.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#ifndef FE_TONEARESTAWAY
#define FE_TONEARESTAWAY 0x1000
#endif
extern struct { uint32_t round, flags; } __bennett_rt_fenv;
extern uint32_t __bennett_rt_fenv_get_round(void);
extern uint32_t __bennett_rt_fenv_get_flags(void);
extern uint64_t __bennett_rt_fenv_get_env(void);
extern void __bennett_rt_fenv_set_round(uint32_t);
extern void __bennett_rt_fenv_set_flags(uint32_t);
extern void __bennett_rt_fenv_set_env(uint64_t);
extern void __bennett_rt_fenv_reset(void);
extern void __bennett_rt_fenv_reset_round(void);
extern void __bennett_rt_fenv_reset_flags(void);
extern uint64_t __bennett_sf_add_f64_rm(uint64_t,uint64_t,int);
static unsigned cflags(unsigned f) { return (f&1)|((f&30)<<1); }
int main(void) {
    CHECK(sizeof __bennett_rt_fenv==8 && _Alignof(__bennett_rt_fenv)==4);
    CHECK(offsetof(__typeof__(__bennett_rt_fenv),round)==0);
    CHECK(offsetof(__typeof__(__bennett_rt_fenv),flags)==4);
    const unsigned char *bytes=(const void *)&__bennett_rt_fenv;
    for (unsigned i=0;i<8;i++) CHECK(bytes[i]==0);
    const int rounds[5]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO,FE_TONEARESTAWAY};
    CHECK(FE_ALL_EXCEPT==61);
    for (unsigned r=0;r<5;r++) {
        CHECK(fesetround(rounds[r])==0);
        CHECK(__bennett_rt_fenv_get_round()==r && __bennett_rt_fenv.round==r);
        CHECK(fegetround()==rounds[r]);
        __bennett_rt_fenv_set_round(r);
        CHECK(fegetround()==rounds[r]);
        for (unsigned f=0;f<32;f++) {
            __bennett_rt_fenv_set_flags(f);
            CHECK(__bennett_rt_fenv_get_env()==((uint64_t)f<<32|r));
            CHECK(fetestexcept(FE_ALL_EXCEPT)==(int)cflags(f));
            for (unsigned mask=0;mask<32;mask++) {
                fexcept_t copy=0;
                CHECK(fegetexceptflag(&copy,cflags(mask))==0 && copy==cflags(f&mask));
                __bennett_rt_fenv_set_flags(f);
                CHECK(feclearexcept(cflags(mask))==0);
                CHECK(__bennett_rt_fenv_get_flags()==(f&~mask));
                __bennett_rt_fenv_set_flags(f);
                CHECK(feraiseexcept(cflags(mask))==0);
                CHECK(__bennett_rt_fenv_get_flags()==(f|mask));
                __bennett_rt_fenv_set_flags(31);
                copy=cflags(f);
                CHECK(fesetexceptflag(&copy,cflags(mask))==0);
                CHECK(__bennett_rt_fenv_get_flags()==((31&~mask)|(f&mask)));
                __bennett_rt_fenv_set_flags(f);
            }
        }
    }
    __bennett_rt_fenv_set_env((uint64_t)2<<32|3);
    fenv_t saved;
    CHECK(fegetenv(&saved)==0);
    __bennett_rt_fenv_set_env((uint64_t)4<<32|2);
    CHECK(fesetenv(&saved)==0 && __bennett_rt_fenv_get_env()==((uint64_t)2<<32|3));
    CHECK(feholdexcept(&saved)==0 && __bennett_rt_fenv_get_env()==3);
    CHECK(feraiseexcept(FE_INVALID)==0);
    CHECK(feupdateenv(&saved)==0 && __bennett_rt_fenv_get_env()==((uint64_t)3<<32|3));
    CHECK(fesetround(-1)!=0 && __bennett_rt_fenv_get_round()==3);
    __bennett_rt_fenv_reset_round();
    CHECK(__bennett_rt_fenv_get_env()==(uint64_t)3<<32);
    __bennett_rt_fenv_set_round(4);
    __bennett_rt_fenv_reset_flags();
    CHECK(__bennett_rt_fenv_get_env()==4);
    CHECK(fesetenv(FE_DFL_ENV)==0 && __bennett_rt_fenv_get_env()==0);
    // An independent exact tie verifies that arith writes this same object.
    // This boundary witness does not establish arith's full exact-flag domain.
    for (unsigned r=0;r<5;r++) {
        __bennett_rt_fenv_set_round(r); __bennett_rt_fenv_set_flags(1);
        uint64_t sum=__bennett_sf_add_f64_rm(UINT64_C(0x3ff0000000000000),UINT64_C(0x3ca0000000000000),r);
        CHECK(sum==UINT64_C(0x3ff0000000000000)+(r==2 || r==4));
        CHECK(__bennett_rt_fenv_get_flags()==17 && fetestexcept(FE_ALL_EXCEPT)==(FE_INVALID|FE_INEXACT));
    }
    __bennett_rt_fenv_reset();
    CHECK(__bennett_rt_fenv_get_env()==0);
    return 0;
}
