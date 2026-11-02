/* Comparison-only lowering: never link into production. Dialect §§17.1,
 * 17.5,19.5; ISA §7.1 byte bump max(n,1), no reclaim, preflight bounds.
 * The driver supplies layout.h's required resolved layout object. */
#include <stddef.h>
#include <stdint.h>
#include "layout.h"
_Static_assert(sizeof(void *) == 8, "native backend requires x86_64 LP64");
struct allocation { uintptr_t base; uint64_t size, live; };
static struct allocation *history;
static uint64_t cursor, entries;
static int ready;

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
    if (p>=__bennett_rt_native_layout.heap_base &&
        p-__bennett_rt_native_layout.heap_base<__bennett_rt_native_layout.heap_limit_max) {
        struct allocation *r=find(p);
        if (r && r->live && contains(r->base,r->size,p,n)) return;
        __bennett_nz_fault(7);
    }
    if (__bennett_rt_native_objects && &__bennett_rt_native_object_count) {
        for (uint64_t i=0;i<__bennett_rt_native_object_count;++i) {
            const struct bennett_native_object *r=__bennett_rt_native_objects+i;
            if (contains((uintptr_t)r->base,r->size,p,n)) return;
        }
    }
    for (struct object *r=stack_objects;r;r=r->next)
        if (contains(r->base,r->size,p,n)) return;
    for (struct object *r=inputs;r;r=r->next)
        if (contains(r->base,r->size,p,n)) return;
    __bennett_nz_fault(7);
}
