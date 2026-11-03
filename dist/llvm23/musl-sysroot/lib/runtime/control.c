/* Ordinary type-ID metadata. Exception matching/rewind descriptors require
 * their complete published caller ABI; no host unwinder is used here. */
#include <stdint.h>
#include <stdlib.h>
extern _Noreturn void __bennett_nz_fault(uint64_t);
uintptr_t __stack_chk_guard=UINT64_C(0x42454e4e45545400);
_Noreturn void __stack_chk_fail(void) { __bennett_nz_fault(4); }
extern __typeof(__stack_chk_fail) __stack_chk_fail_local __attribute__((alias("__stack_chk_fail")));
struct type_id { const void *info; uint32_t id; struct type_id *next; };
static struct type_id *types;
static uint32_t next_id=1;
int32_t __bennett_rt_eh_typeid(const void *info) {
    for(struct type_id *t=types;t;t=t->next) if(t->info==info) return (int32_t)t->id;
    if(!next_id || next_id==UINT32_MAX) __bennett_nz_fault(2);
    struct type_id *t=malloc(sizeof *t);
    t->info=info; t->id=next_id++; t->next=types; types=t;
    return (int32_t)t->id;
}
