#ifndef BENNETT_SHADOW_H
#define BENNETT_SHADOW_H
#include <stdint.h>
enum bennett_shadow_kind { BRT_FRAME, BRT_ALLOCA, BRT_SAVE };
struct bennett_shadow_entry {
    uint64_t begin, end, before;
    uint32_t live;
    enum bennett_shadow_kind kind;
    struct bennett_shadow_entry *parent, *next;
};
struct bennett_shadow_table {
    struct bennett_shadow_entry *first, *last, *frame;
};
extern unsigned char *__bennett_rt_shadow;
extern uint64_t __bennett_rt_ss_size, __bennett_rt_ss_sp;
extern uintptr_t __bennett_rt_reg_sp;
extern struct bennett_shadow_table __bennett_rt_ss_table;
void __bennett_rt_ss_check(const void *,uint64_t,const struct bennett_shadow_table *);
#endif
