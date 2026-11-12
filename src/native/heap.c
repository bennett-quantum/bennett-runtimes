/* Comparison-only lowering: never link into production. Dialect §§17.1,
 * 17.5,19.5; ISA §7.1 byte bump max(n,1), no reclaim, preflight bounds.
 * The driver supplies layout.h's required resolved layout object. */
#include <stddef.h>
#include <stdint.h>
#include "layout.h"
#include "../runtime/shadow.h"
extern unsigned char *__bennett_rt_shadow __attribute__((weak));
extern uint64_t __bennett_rt_ss_size __attribute__((weak));
extern struct bennett_shadow_table __bennett_rt_ss_table __attribute__((weak));
extern void __bennett_rt_ss_check(const void *,uint64_t,const struct bennett_shadow_table *) __attribute__((weak));
_Static_assert(sizeof(void *) == 8, "native backend requires x86_64 LP64");
struct allocation { uintptr_t base; uint64_t size, live; };
static struct allocation *history;
static uint64_t cursor, entries;
static int ready;
struct global_range { uintptr_t base, end, prefix_end; };
static struct global_range *global_ranges;
static uint64_t global_count;

static void swap_globals(uint64_t a,uint64_t b) {
    uintptr_t base=global_ranges[a].base,end=global_ranges[a].end;
    global_ranges[a].base=global_ranges[b].base;
    global_ranges[a].end=global_ranges[b].end;
    global_ranges[b].base=base; global_ranges[b].end=end;
}
static void sift_globals(uint64_t root,uint64_t count) {
    while (root<count/2) {
        uint64_t child=2*root+1;
        if (child+1<count && global_ranges[child].base<global_ranges[child+1].base) ++child;
        if (global_ranges[child].base<=global_ranges[root].base) return;
        swap_globals(root,child); root=child;
    }
}
static void index_globals(void) {
    if (!__bennett_rt_native_objects || !&__bennett_rt_native_object_count) return;
    uint64_t count=__bennett_rt_native_object_count;
    if (!count) return;
    if (count>SIZE_MAX/sizeof(*global_ranges))
        __bennett_rt_native_infrastructure("global object metadata size");
    long result=__bennett_rt_native_os(9,0,count*sizeof(*global_ranges),3,0x22,-1,0);
    if ((unsigned long)result>=(unsigned long)-4095)
        __bennett_rt_native_infrastructure("global object mapping");
    global_ranges=(void *)result; global_count=count;
    for (uint64_t i=0;i<count;++i) {
        uintptr_t base=(uintptr_t)__bennett_rt_native_objects[i].base;
        uint64_t size=__bennett_rt_native_objects[i].size;
        global_ranges[i].base=base;
        global_ranges[i].end=size>UINTPTR_MAX-base?UINTPTR_MAX:base+size;
    }
    for (uint64_t i=count/2;i;--i) sift_globals(i-1,count);
    for (uint64_t end=count;end>1;--end) { swap_globals(0,end-1); sift_globals(0,end-1); }
    uintptr_t end=0;
    for (uint64_t i=0;i<count;++i) {
        if (global_ranges[i].end>end) end=global_ranges[i].end;
        global_ranges[i].prefix_end=end;
    }
}

/* Exit status 125 and this distinct prefix identify host setup failure. Such
 * a result is never a dialect fault or a successful comparison outcome. */
_Noreturn void __bennett_rt_native_infrastructure(const char *message) {
    static const char prefix[]="bennett infrastructure failure: ";
    size_t n=0; while (message[n]) ++n;
    __bennett_rt_native_os(1,2,(long)prefix,sizeof(prefix)-1,0,0,0);
    __bennett_rt_native_os(1,2,(long)message,n,0,0,0);
    __bennett_rt_native_os(1,2,(long)"\n",1,0,0,0);
    __bennett_rt_native_os(231,125,0,0,0,0,0);
    __builtin_trap();
}
/* Preserve arbitrary fault codes in a stable decimal diagnostic, then trap.
 * Neither musl nor the console model is called on a failing path. */
_Noreturn void __bennett_nz_fault(uint64_t code) {
    if (!code) code=4; /* ISA effective cause; zero never denotes a fault code */
    static const char prefix[]="bennett runtime fault ";
    /* O0 codegen must not lower a stack-array initializer to the checked
     * device memcpy: this private diagnostic stack is intentionally outside
     * device-object metadata. Volatile byte stores prevent that recursion. */
    volatile char text[64];
    for (size_t i=0;i<sizeof prefix-1;++i) text[i]=prefix[i];
    char digits[20]; size_t n=0, len=sizeof prefix-1;
    do { digits[n++]=(char)('0'+code%10); code/=10; } while(code);
    /* prefix is 22 bytes, excluding its terminating NUL. */
    while(n) text[len++]=digits[--n];
    text[len++]='\n';
    __bennett_rt_native_os(1,2,(long)text,len,0,0,0);
    __builtin_trap();
}

static void initialise(void) {
    const struct bennett_native_layout *l=&__bennett_rt_native_layout;
    if (ready) return;
    if (l->abi_version!=1 || l->heap_base<4096 || l->heap_base%4096 ||
        l->heap_limit_max>UINTPTR_MAX-l->heap_base ||
        l->heap_limit_max>SIZE_MAX/sizeof(*history))
        __bennett_rt_native_infrastructure("invalid resolved layout");
    if (l->heap_limit_max) {
        /* MAP_FIXED_NOREPLACE: never overwrite globals/text, never relocate.
         * Linux may reject low addresses; that is an infrastructure failure.
         * Anonymous private mapping guarantees initial zero fill. */
        long p=__bennett_rt_native_os(9,l->heap_base,l->heap_limit_max,3,0x100022,-1,0);
        if ((uint64_t)p!=l->heap_base) __bennett_rt_native_infrastructure("heap mapping");
        /* max(n,1) consumes at least one byte. One record per heap byte covers
         * EVERY possible history, including zero-sized/dead allocations. This
         * private native bookkeeping is never a device-visible allocation. */
        p=__bennett_rt_native_os(9,0,l->heap_limit_max*sizeof(*history),3,0x22,-1,0);
        if ((unsigned long)p>=(unsigned long)-4095)
            __bennett_rt_native_infrastructure("allocation history mapping");
        history=(void *)p;
    }
    index_globals();
    ready=1;
}
void *__bennett_nz_alloc(uint64_t n) {
    initialise();
    uint64_t extent=n?n:1, limit=__bennett_rt_native_layout.heap_limit_max;
    if (extent>limit-cursor) __bennett_nz_fault(2);
    unsigned char *p=(void *)(uintptr_t)(__bennett_rt_native_layout.heap_base+cursor);
    history[entries].base=(uintptr_t)p;
    history[entries].size=n;
    history[entries].live=1;
    ++entries; cursor+=extent;
    for (uint64_t i=0;i<n;++i) ((volatile unsigned char *)p)[i]=0;
    return p;
}
static struct allocation *find(uintptr_t p) {
    uint64_t lo=0,hi=entries;
    while (lo<hi) {
        uint64_t mid=lo+(hi-lo)/2;
        if (history[mid].base<=p) lo=mid+1; else hi=mid;
    }
    return lo ? history+lo-1 : 0;
}
void __bennett_nz_free(void *p) {
    if (!p) return;
    struct allocation *r=find((uintptr_t)p);
    if (r && r->base==(uintptr_t)p && r->live) { r->live=0; return; }
    __bennett_nz_fault(7);
}

/* Native input/stack bookkeeping is private, allocated with Linux transport,
 * never malloc or device heap. No fixed record cap: failure is infrastructure.
 * Materialised device frames normally use the ordinary heap history above.
 * These records additionally cover comparison-side argv/envp and native
 * allocas and implicit byval copies in the freestanding closure. */
struct object { uintptr_t base; uint64_t size; struct object *next; };
static struct object *inputs, *stack_objects;
static struct object *object(void *p,uint64_t n,struct object *next) {
    long result=__bennett_rt_native_os(9,0,4096,3,0x22,-1,0);
    if ((unsigned long)result>=(unsigned long)-4095)
        __bennett_rt_native_infrastructure("checked object mapping");
    struct object *r=(void *)result;
    r->base=(uintptr_t)p; r->size=n; r->next=next; return r;
}
void *__bennett_rt_native_stack_enter(void) { return stack_objects; }
void __bennett_rt_native_stack_add(void *p,uint64_t n) {
    stack_objects=object(p,n,stack_objects);
}
void __bennett_rt_native_stack_leave(void *mark) {
    while (stack_objects!=mark) {
        if (!stack_objects) __bennett_rt_native_infrastructure("invalid checked frame");
        struct object *r=stack_objects; stack_objects=r->next;
        if (__bennett_rt_native_os(11,(long)r,4096,0,0,0,0))
            __bennett_rt_native_infrastructure("checked object unmapping");
    }
}
/* Exact overflow extents for variadic calls, published by each checked
 * caller and consumed by the callee's va_start registration. The stack
 * discipline follows the call stack; the caller pops after return, which
 * also covers callees that never call va_start. */
static struct object *va_pending;
void __bennett_rt_native_va_push(uint64_t n) { va_pending=object(0,n,va_pending); }
void __bennett_rt_native_va_pop(void) {
    if (!va_pending) __bennett_rt_native_infrastructure("unbalanced variadic call");
    struct object *r=va_pending; va_pending=r->next;
    if (__bennett_rt_native_os(11,(long)r,4096,0,0,0,0))
        __bennett_rt_native_infrastructure("checked object unmapping");
}
uint64_t __bennett_rt_native_va_overflow_size(void) {
    /* A variadic callee entered from outside the checked module has no
     * published extent: zero registers nothing, so its overflow reads
     * fault 7 loud instead of silently passing. */
    return va_pending?va_pending->size:0;
}
void __bennett_rt_native_inputs(int argc,char **argv,char **envp) {
    if (!__bennett_rt_native_objects) return; /* plain native convenience link */
    inputs=object(argv,((uint64_t)argc+1)*sizeof(*argv),inputs);
    for (int i=0;i<argc;++i) {
        size_t n=0; while (argv[i][n]) ++n;
        inputs=object(argv[i],n+1,inputs);
    }
    size_t count=0;
    while (envp[count]) {
        size_t n=0; while (envp[count][n]) ++n;
        inputs=object(envp[count],n+1,inputs); ++count;
    }
    inputs=object(envp,(count+1)*sizeof(*envp),inputs);
}
static int contains(uintptr_t base,uint64_t size,uintptr_t p,uint64_t n) {
    return p>=base && p-base<=size && n<=size-(p-base);
}
__attribute__((noinline)) void __bennett_rt_native_check(const void *address,uint64_t n) {
    if (!n) return; /* a zero-length memory operation touches no allocation */
    uintptr_t p=(uintptr_t)address;
    if (n>UINTPTR_MAX-p) __bennett_nz_fault(7);
    initialise();
    if(&__bennett_rt_shadow && &__bennett_rt_ss_size && &__bennett_rt_ss_table && __bennett_rt_ss_check) {
        uintptr_t base=(uintptr_t)__bennett_rt_shadow;
        if(base && p>=base && p-base<__bennett_rt_ss_size)
            __bennett_rt_ss_check(address,n,&__bennett_rt_ss_table);
    }
    if (p>=__bennett_rt_native_layout.heap_base &&
        p-__bennett_rt_native_layout.heap_base<__bennett_rt_native_layout.heap_limit_max) {
        struct allocation *r=find(p);
        if (r && r->live && contains(r->base,r->size,p,n)) return;
        __bennett_nz_fault(7);
    }
    /* Among objects beginning at/before p, the greatest end belongs to one
     * actual object. Requiring that end to cover p+n is exactly the original
     * single-object predicate, including overlapping/duplicate globals; it
     * never joins adjacent objects into a larger allowed interval. */
    uint64_t lo=0,hi=global_count;
    while (lo<hi) {
        uint64_t mid=lo+(hi-lo)/2;
        if (global_ranges[mid].base<=p) lo=mid+1; else hi=mid;
    }
    if (lo && p+n<=global_ranges[lo-1].prefix_end) return;
    for (struct object *r=stack_objects;r;r=r->next)
        if (contains(r->base,r->size,p,n)) return;
    for (struct object *r=inputs;r;r=r->next)
        if (contains(r->base,r->size,p,n)) return;
    __bennett_nz_fault(7);
}
