#include <stdint.h>
#include "../src/runtime/shadow.h"
extern void __bennett_rt_ss_init(uint64_t);
extern void *__bennett_rt_ss_push(uint64_t,uint64_t);
extern void *__bennett_rt_ss_alloca(uint64_t,uint64_t);
extern void __bennett_rt_ss_pop(void *);
extern void *__bennett_rt_ss_save(void);
extern void __bennett_rt_ss_restore(void *);
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
int main(int argc,char **argv) {
    __bennett_rt_ss_init(64);
    unsigned char *base=__bennett_rt_shadow, *p=__bennett_rt_ss_push(8,8);
    CHECK(p==base && __bennett_rt_ss_sp==8);
    p[0]=42;
    __bennett_rt_ss_init(4096);
    CHECK(__bennett_rt_ss_size==64 && __bennett_rt_ss_sp==8 && p[0]==42);
    unsigned char *q=__bennett_rt_ss_push(8,16);
    CHECK(q==base+16 && __bennett_rt_ss_sp==24);
    if(argc>1) {
        switch(argv[1][0]) {
        case 'g': return *(volatile unsigned char *)(base+8);
        case 'o': return *(volatile uint64_t *)(p+4);
        case 'd': __bennett_rt_ss_pop(q); return *(volatile unsigned char *)q;
        case 'b': __bennett_rt_ss_push(41,1); return 99;
        case 'a': __bennett_rt_ss_push(1,3); return 99;
        case 'w': __bennett_rt_ss_check((void *)(uintptr_t)-4,8,&__bennett_rt_ss_table); return 99;
        case 't': {
            void *saved=__bennett_rt_ss_save();
            __bennett_rt_ss_pop(q); __bennett_rt_ss_restore(saved); return 99;
        }
        }
    }
    unsigned char *dynamic=__bennett_rt_ss_alloca(3,8);
    CHECK(dynamic==base+24);
    for(unsigned i=0;i<3;++i) CHECK(dynamic[i]==0);
    dynamic[0]=77;
    void *saved=__bennett_rt_ss_save();
    unsigned char *later=__bennett_rt_ss_alloca(5,4);
    CHECK(later==base+28);
    for(unsigned i=0;i<5;++i) CHECK(later[i]==0);
    later[0]=99;
    __bennett_rt_ss_restore(saved);
    CHECK(__bennett_rt_ss_sp==27 && dynamic[0]==77 && p[0]==42);
    if(argc>1 && argv[1][0]=='r') return *(volatile unsigned char *)later;
    unsigned char *reused=__bennett_rt_ss_alloca(5,4);
    CHECK(reused==later && reused[0]==0);
    __bennett_rt_ss_check(reused,5,&__bennett_rt_ss_table);
    __bennett_rt_ss_restore(saved); CHECK(__bennett_rt_ss_sp==27);
    __bennett_rt_ss_pop(p); CHECK(__bennett_rt_ss_sp==0 && !__bennett_rt_ss_table.frame);
    unsigned char *whole=__bennett_rt_ss_push(64,1);
    CHECK(whole==base);
    for(unsigned i=0;i<64;++i) CHECK(!whole[i]);
    __bennett_rt_ss_check(whole,64,&__bennett_rt_ss_table);
    __bennett_rt_ss_pop(whole);
    void *empty=__bennett_rt_ss_push(0,1);
    __bennett_rt_ss_check(empty,0,&__bennett_rt_ss_table);
    __bennett_rt_ss_pop(empty);
    return 0;
}
