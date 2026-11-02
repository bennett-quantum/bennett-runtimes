/* Independent primitive history, budget and reversal checks for C wrappers. */
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern void *runtime_malloc(size_t);
extern void *runtime_calloc(size_t, size_t);
extern void runtime_free(void *);
extern void *__bennett_rt_alloc(uint64_t);
extern void *__bennett_rt_aligned_alloc(size_t,size_t);
static _Alignas(8192) unsigned char storage[32768];
static unsigned char *const heap=storage+4096;
enum { HEAP_SIZE=16384 };
static size_t bump, count, limit = HEAP_SIZE;
static struct record { void *base; uint64_t requested; int live; } records[512];
static jmp_buf fault;
static uint64_t code;
void __bennett_nz_fault(uint64_t c) { code=c; longjmp(fault,1); }
void *__bennett_nz_alloc(uint64_t n) {
    size_t extent=n?n:1;
    if (extent>limit-bump) __bennett_nz_fault(2);
    void *p=heap+bump;
    records[count++]=(struct record){p,n,1};
    memset(p,0,extent); bump+=extent; return p;
}
void __bennett_nz_free(void *p) {
    for (size_t i=0;i<count;i++) if (records[i].base==p && records[i].live) {
        records[i].live=0; return;
    }
    __bennett_nz_fault(7);
}
void __bennett_rt_memset(void *p, unsigned char c, uint64_t n) { memset(p,c,n); }
void __bennett_rt_memmove(void *p, const void *s, uint64_t n) { memmove(p,s,n); }
int main(int argc, char **argv) {
    assert(argc==2);
    memset(heap,0xaa,HEAP_SIZE);
    if (argv[1][0]=='f') {
        void *p=__bennett_rt_aligned_alloc(8192,19);
        assert(p==heap+4096 && (uintptr_t)p%8192==0);
        assert(count==4 && bump==4160);
        assert(records[0].requested==0 && records[1].requested==4095);
        assert(records[2].requested==32 && records[3].requested==32);
        for (size_t i=0;i<19;i++) assert(((unsigned char *)p)[i]==0);
    } else if (argv[1][0]=='o') {
        /* Overflow must precede even a padding allocation. */
        assert(__bennett_rt_alloc(3)==heap);
        if (!setjmp(fault)) { runtime_malloc(SIZE_MAX); abort(); }
        assert(code==2 && count==1 && bump==3);
        if (!setjmp(fault)) { runtime_calloc(SIZE_MAX/2+1,2); abort(); }
        assert(code==2 && count==1 && bump==3);
    } else if (argv[1][0]=='p' || argv[1][0]=='e' || argv[1][0]=='m') {
        limit=argv[1][0]=='p'?3:argv[1][0]=='m'?32:31;
        __bennett_rt_alloc(3);
        if (!setjmp(fault)) { runtime_malloc(1); abort(); }
        assert(code==2);
        if (limit==3) assert(count==1 && bump==3);
        else if (limit==31) assert(count==3 && bump==16 && records[1].requested==0 && records[2].requested==12);
        else assert(count==4 && bump==32 && records[3].requested==16);
    } else {
        const size_t a=_Alignof(max_align_t);
        assert(a==16);
        assert(__bennett_rt_alloc(3)==heap);
        void *p=runtime_malloc(1);
        assert(p==heap+16 && count==5 && bump==64);
        assert(records[0].requested==3 && records[1].requested==0 && records[2].requested==12 && records[3].requested==16);
        assert(records[4].requested==32); /* explicit ordinary C bookkeeping */
        for (size_t i=0;i<32;i++) assert(heap[i]==0);
        runtime_free(p);
        assert(!records[3].live && bump==64);
        assert(runtime_malloc(17)==heap+80 && records[5].requested==0 && records[6].requested==15 && records[7].requested==32);
        assert(count==8 && bump==112); /* reuse the live bookkeeping slot */
        assert(runtime_malloc(0)==heap+128 && records[10].requested==16 && records[11].requested==32);
        assert(runtime_calloc(0,SIZE_MAX)==heap+192 && records[14].requested==16 && records[15].requested==32);
        /* Undo ordinary free/alloc history; padding has its own record. */
        records[3].live=1;
    }
    while (count) {
        struct record *r=&records[--count];
        size_t offset=(unsigned char *)r->base-heap;
        memset(heap+offset,0xaa,bump-offset); bump=offset;
        memset(r,0,sizeof *r);
    }
    assert(bump==0 && count==0);
    for (size_t i=0;i<HEAP_SIZE;i++) assert(heap[i]==0xaa);
    return 0;
}
