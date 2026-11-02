/* Dialect §§3.1,16.9,17.1: allocation is an ISA primitive, not
 * a private C arena. Its live table and nonreclaiming history belong to ISA.
 * The native comparison link supplies the same primitive semantics separately.
 */
#include <stddef.h>
#include <stdint.h>

extern void *__bennett_nz_alloc(uint64_t);
extern void __bennett_nz_free(void *);
extern void __bennett_nz_fault(uint64_t);
extern void __bennett_rt_memset(void *, unsigned char, uint64_t);
extern void __bennett_rt_memmove(void *, const void *, uint64_t);

/* Bookkeeping is ordinary, charged device memory, not an ISA-private query
 * or a capped native map. Pool slots stay live and are reused as data after
 * their C payload is freed. Payload storage/history are never reclaimed. */
struct c_allocation { void *base; uint64_t requested, live; struct c_allocation *next; };
_Static_assert(sizeof(struct c_allocation)==32,"C allocation record layout");
static struct c_allocation *live_allocations, *spare_records;

/* Tracks the result of an ordinary primitive request. This is a local cursor,
 * never an authoritative view of direct primitive users' allocation history.
 * It is ordinary device memory and reverses with the caller. */
static uintptr_t next_byte;
void *__bennett_rt_alloc(uint64_t n) {
    void *p = __bennett_nz_alloc(n);
    next_byte = (uintptr_t)p + (n ? n : 1);
    return p;
}

void *__bennett_rt_aligned_alloc(size_t a,size_t n) {
    if (!a || (a&(a-1))) {
        __bennett_nz_fault(4);
        __builtin_unreachable();
    }
    if (a<_Alignof(max_align_t)) a=_Alignof(max_align_t);
    /* Requested alignment controls the starting address. The allocation's
     * byte extent retains the C ABI's max_align_t rounding. */
    const size_t unit = _Alignof(max_align_t);
    size_t extent = n ? n : 1;
    if (extent > SIZE_MAX - (unit - 1)) {
        __bennett_nz_fault(2);
        __builtin_unreachable();
    }
    extent = (extent + unit - 1) / unit * unit;
    /* Direct primitive users can have advanced the heap since our last call,
     * and §19.5 only guarantees page alignment of the initial base. Refresh
     * through an ordinary charged zero allocation (physical extent one), then
     * request the minimal padding from that exact next byte. No private ISA
     * cursor/query or native-only state is used. */
    __bennett_rt_alloc(0);
    size_t padding = (-(uintptr_t)next_byte) % a;
    if (padding) __bennett_rt_alloc(padding);
    void *p = __bennett_rt_alloc(extent);
    /* The primitive guarantees zero fill; no redundant byte loop is needed. */
    struct c_allocation *r=spare_records;
    if (r) spare_records=r->next;
    else r=__bennett_rt_alloc(sizeof *r);
    r->base=p; r->requested=n; r->live=1; r->next=live_allocations;
    live_allocations=r;
    return p;
}

void *malloc(size_t n) { return __bennett_rt_aligned_alloc(_Alignof(max_align_t),n); }

void *calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        __bennett_nz_fault(2); /* FAULT_BUDGET_HEAP; no mutation on overflow */
        __builtin_unreachable();
    }
    return malloc(count * size);
}

void free(void *p) {
    if (!p) return;
    __bennett_nz_free(p); /* reject interior/dead/nonheap before C state changes */
    struct c_allocation **link=&live_allocations;
    while (*link) {
        struct c_allocation *r=*link;
        if (r->base==p) {
            *link=r->next; r->live=0; r->next=spare_records; spare_records=r;
            return;
        }
        link=&r->next;
    }
    /* Runtime raw allocations can be freed without a C allocation record. */
}

void *__bennett_rt_aligned_realloc(void *p,size_t n,size_t a) {
    if (!p) return __bennett_rt_aligned_alloc(a,n);
    struct c_allocation *r=live_allocations;
    while (r && r->base!=p) r=r->next;
    if (!r || !r->live) {
        __bennett_nz_fault(7);
        __builtin_unreachable();
    }
    uint64_t count=r->requested;
    if (count>n) count=n;
    /* Always move; zero uses malloc(0)'s unique allocation policy. Failed
     * growth has not freed the old payload. */
    void *q=__bennett_rt_aligned_alloc(a,n);
    __bennett_rt_memmove(q,p,count);
    free(p);
    return q;
}

void *realloc(void *p,size_t n) {
    return __bennett_rt_aligned_realloc(p,n,_Alignof(max_align_t));
}
