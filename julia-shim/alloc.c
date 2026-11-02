/* Julia 1.13 allocation entry ABIs (§5.1). The nonmoving, nonreclaiming
 * device heap keeps objects live; collector scheduling is not needed to
 * recover storage from that heap. One ordinary allocation includes the
 * payload and its immediately preceding 8-byte type header. An extra 8-byte
 * prefix keeps returned payloads 16-byte aligned. All bytes/cost are visible
 * to the same materialisation and memory checks as any C allocation.
 *
 * small/big sizes include the 8-byte Julia header, but the frontend also
 * promises dereferenceable(size) on the returned pointer. Preserve that full
 * forward access window as well as the preceding header; this explicit extra
 * storage is charged by malloc. typed/gc_alloc_obj sizes are payload sizes.
 * Julia's emitted ijl_* ABI carries the tag as i64, even
 * though the public source headers spell it as a pointer. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
extern void __bennett_nz_fault(uint64_t);

static void *object(size_t n,uintptr_t tag) {
    if (n>SIZE_MAX-16) { __bennett_nz_fault(2); __builtin_unreachable(); }
    unsigned char *base=malloc(n+16), *p=base+16;
    ((uintptr_t *)p)[-1]=tag;
    return p;
}
void *jl_gc_alloc(void *ptls,size_t n,void *tag) {
    (void)ptls; return object(n,(uintptr_t)tag);
}
void *ijl_gc_alloc_typed(void *ptls,size_t n,uintptr_t tag) {
    (void)ptls; return object(n,tag);
}
void *gc_alloc_obj(void *,size_t,void *) __asm__("julia.gc_alloc_obj");
void *gc_alloc_obj(void *ptls,size_t n,void *tag) {
    return jl_gc_alloc(ptls,n,tag);
}
void *ijl_gc_big_alloc(void *ptls,size_t n,uintptr_t tag) {
    (void)ptls;
    if (n<8) { __bennett_nz_fault(4); __builtin_unreachable(); }
    return object(n,tag);
}
void *ijl_gc_small_alloc(void *ptls,int offset,int n,uintptr_t tag) {
    (void)offset;
    if (n<8) { __bennett_nz_fault(4); __builtin_unreachable(); }
    return ijl_gc_big_alloc(ptls,(size_t)n,tag);
}
