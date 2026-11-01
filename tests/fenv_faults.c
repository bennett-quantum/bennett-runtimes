#include <setjmp.h>
#include <stdint.h>
extern uint64_t __bennett_rt_fenv_get_env(void);
extern void __bennett_rt_fenv_set_round(uint32_t);
extern void __bennett_rt_fenv_set_flags(uint32_t);
extern void __bennett_rt_fenv_set_env(uint64_t);
static jmp_buf target;
static uint64_t code;
_Noreturn void __bennett_nz_fault(uint64_t c) { code=c; longjmp(target,1); }
static void round_fault(void) { __bennett_rt_fenv_set_round(5); }
static void flags_fault(void) { __bennett_rt_fenv_set_flags(32); }
static void env_round_fault(void) { __bennett_rt_fenv_set_env((uint64_t)16<<32|5); }
static void env_flags_fault(void) { __bennett_rt_fenv_set_env((uint64_t)32<<32|2); }
static int probe(void (*operation)(void)) {
    const uint64_t before=(uint64_t)2<<32|3;
    __bennett_rt_fenv_set_env(before); code=0;
    if (!setjmp(target)) { operation(); return 1; }
    return code!=4 || __bennett_rt_fenv_get_env()!=before;
}
int main(void) {
    return probe(round_fault) || probe(flags_fault) || probe(env_round_fault) || probe(env_flags_fault);
}
