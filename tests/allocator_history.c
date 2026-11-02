/* Test-only store-history interpreter for the actual published C allocator. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
extern void *test_malloc(size_t);
extern void *test_calloc(size_t,size_t);
extern void *test_realloc(void *,size_t);
extern void test_free(void *);
extern void __bennett_rt_memset(void *,unsigned char,uint64_t);
static _Alignas(4096) unsigned char heap[512];
static uint64_t cursor,count;
static struct allocation { void *p; uint64_t n,live; } allocations[512];
static struct entry { void *p; size_t n; unsigned char old[128]; } history[4096];
static size_t history_n;
void test_record(void *p,uint64_t n) {
    assert(n<=128 && history_n<4096);
    struct entry *r=&history[history_n++]; r->p=p;r->n=n;
    memcpy(r->old,p,n);
}
_Noreturn void __bennett_nz_fault(uint64_t c) { (void)c; assert(!"unexpected allocator fault"); __builtin_trap(); }
void *__bennett_nz_alloc(uint64_t n) {
    uint64_t extent=n?n:1;
    assert(extent<=sizeof heap-cursor);
    void *p=heap+cursor;
    test_record(&cursor,sizeof cursor);test_record(&count,sizeof count);
    test_record(&allocations[count],sizeof allocations[count]);test_record(p,extent);
    allocations[count++]=(struct allocation){p,n,1}; cursor+=extent;memset(p,0,extent);
    return p;
}
void __bennett_nz_free(void *p) {
    if (!p) return;
    for (uint64_t i=0;i<count;i++) if (allocations[i].p==p && allocations[i].live) {
        test_record(&allocations[i].live,sizeof allocations[i].live);
        allocations[i].live=0;return;
    }
    __bennett_nz_fault(7);
}
static void run(void) {
    unsigned char *p=test_malloc(3), *q=test_calloc(5,3);
    assert(p==heap+16 && q==heap+80);
    __bennett_rt_memset(p,0x5a,3);
    unsigned char *r=test_realloc(p,35);
    assert(r==heap+144 && cursor==224 && count==12);
    for (unsigned i=0;i<3;i++) assert(r[i]==0x5a);
    for (unsigned i=3;i<35;i++) assert(r[i]==0);
    test_free(q);
    /* Leave r live: restoring only primitive counters would leave dangling
     * runtime list heads and fail the second execution after reversal. */
}
static void reverse(void) {
    while(history_n) { struct entry *r=&history[--history_n];memcpy(r->p,r->old,r->n); }
}
int main(void) {
    memset(heap,0xaa,sizeof heap);
    for (unsigned pass=0;pass<2;pass++) {
        run(); assert(history_n>30); reverse();
        assert(!cursor && !count);
        for (size_t i=0;i<sizeof heap;i++) assert(heap[i]==0xaa);
        static const struct allocation initial[512];
        assert(!memcmp(allocations,initial,sizeof allocations));
    }
    return 0;
}
