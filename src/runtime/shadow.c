/* Published §17.3: backing bytes and logical objects have separate lifetimes.
 * Metadata remains ordinary reversible memory, in creation order, without a
 * fixed frame/save/allocation count. Native support is not used here. */
#include "shadow.h"
#include <stddef.h>
#include <stdlib.h>
extern void *__bennett_nz_alloc(uint64_t);
extern _Noreturn void __bennett_nz_fault(uint64_t);
unsigned char *__bennett_rt_shadow;
uint64_t __bennett_rt_ss_size, __bennett_rt_ss_sp;
uintptr_t __bennett_rt_reg_sp;
extern uintptr_t __bennett_rt_reg_rsp __attribute__((alias("__bennett_rt_reg_sp")));
extern uintptr_t __bennett_rt_reg_x2 __attribute__((alias("__bennett_rt_reg_sp")));
struct bennett_shadow_table __bennett_rt_ss_table;

void __bennett_rt_ss_init(uint64_t size) {
    if(__bennett_rt_shadow) return;
    __bennett_rt_shadow=__bennett_nz_alloc(size);
    __bennett_rt_ss_size=size;
    __bennett_rt_reg_sp=(uintptr_t)__bennett_rt_shadow;
}
static struct bennett_shadow_entry *append(enum bennett_shadow_kind kind,uint64_t begin,uint64_t end) {
    struct bennett_shadow_entry *entry=malloc(sizeof *entry);
    *entry=(struct bennett_shadow_entry){begin,end,__bennett_rt_ss_sp,1,kind,__bennett_rt_ss_table.frame,0};
    if(__bennett_rt_ss_table.last) __bennett_rt_ss_table.last->next=entry;
    else __bennett_rt_ss_table.first=entry;
    __bennett_rt_ss_table.last=entry;
    return entry;
}
static void *allocate(uint64_t size,uint64_t align,enum bennett_shadow_kind kind) {
    if(!__bennett_rt_shadow || __bennett_rt_ss_sp>__bennett_rt_ss_size) __bennett_nz_fault(7);
    if(!align || (align&(align-1))) __bennett_nz_fault(4);
    if(kind==BRT_ALLOCA && !__bennett_rt_ss_table.frame) __bennett_nz_fault(7);
    uint64_t pos=__bennett_rt_ss_sp, space=__bennett_rt_ss_size-pos;
    uint64_t padding=(0-((uintptr_t)__bennett_rt_shadow+pos))&(align-1);
    if(padding>space || size>space-padding) __bennett_nz_fault(2);
    uint64_t begin=pos+padding, end=begin+size;
    struct bennett_shadow_entry *entry=append(kind,begin,end);
    if(kind==BRT_FRAME) __bennett_rt_ss_table.frame=entry;
    __bennett_rt_ss_sp=end;
    __bennett_rt_reg_sp=(uintptr_t)__bennett_rt_shadow+end;
    unsigned char *p=__bennett_rt_shadow+begin;
    /* Register before filling: these writes address exactly the new logical
     * object, never unused capacity or a name-based checker exemption. */
    for(uint64_t i=0;i<size;++i) p[i]=0;
    return p;
}
void *__bennett_rt_ss_push(uint64_t size,uint64_t align) { return allocate(size,align,BRT_FRAME); }
void *__bennett_rt_ss_alloca(uint64_t size,uint64_t align) { return allocate(size,align,BRT_ALLOCA); }
void __bennett_rt_ss_pop(void *fp) {
    struct bennett_shadow_entry *frame=0;
    for(struct bennett_shadow_entry *e=__bennett_rt_ss_table.first;e;e=e->next)
        if(e->live && e->kind==BRT_FRAME && __bennett_rt_shadow+e->begin==fp) frame=e;
    if(!frame) __bennett_nz_fault(7);
    for(struct bennett_shadow_entry *e=frame;e;e=e->next) e->live=0;
    __bennett_rt_ss_sp=frame->before;
    __bennett_rt_reg_sp=(uintptr_t)__bennett_rt_shadow+frame->before;
    __bennett_rt_ss_table.frame=frame->parent;
}
void *__bennett_rt_ss_save(void) {
    if(!__bennett_rt_shadow || !__bennett_rt_ss_table.frame) __bennett_nz_fault(7);
    return append(BRT_SAVE,__bennett_rt_ss_sp,__bennett_rt_ss_sp);
}
void __bennett_rt_ss_restore(void *token) {
    struct bennett_shadow_entry *saved=0;
    for(struct bennett_shadow_entry *e=__bennett_rt_ss_table.first;e;e=e->next)
        if(e==token && e->live && e->kind==BRT_SAVE) saved=e;
    if(!saved || !saved->parent || !saved->parent->live) __bennett_nz_fault(7);
    for(struct bennett_shadow_entry *e=saved->next;e;e=e->next) e->live=0;
    for(struct bennett_shadow_entry *e=__bennett_rt_ss_table.first;e!=saved;e=e->next)
        if(e->live && e->kind==BRT_ALLOCA && e->begin<=saved->begin && e->end>saved->begin) e->end=saved->begin;
    __bennett_rt_ss_sp=saved->begin;
    __bennett_rt_reg_sp=(uintptr_t)__bennett_rt_shadow+saved->begin;
    __bennett_rt_ss_table.frame=saved->parent;
}
void __bennett_rt_ss_check(const void *address,uint64_t n,const struct bennett_shadow_table *table) {
    uintptr_t p=(uintptr_t)address, base=(uintptr_t)__bennett_rt_shadow;
    if(!table || !base || n>UINTPTR_MAX-p) __bennett_nz_fault(7);
    unsigned found=0;
    /* Do not stop at the first match: materialised rows visit creation order
     * and OR the live single-object predicates on every logical entry. */
    for(const struct bennett_shadow_entry *e=table->first;e;e=e->next) {
        uintptr_t start=base+e->begin;
        found|=e->live && e->kind!=BRT_SAVE && p>=start && p-start<=e->end-e->begin && n<=e->end-e->begin-(p-start);
    }
    if(!found) __bennett_nz_fault(7);
}
