#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
extern void *__bennett_rt_alloc(uint64_t);
extern void *__bennett_nz_alloc(uint64_t);
extern void *__rust_alloc(size_t,size_t);
extern void *__rust_alloc_zeroed(size_t,size_t);
extern void *__rust_realloc(void *,size_t,size_t,size_t);
extern void __rust_dealloc(void *,size_t,size_t);
extern void *jl_gc_alloc(void *,size_t,void *);
extern void *ijl_gc_alloc_typed(void *,size_t,uintptr_t);
extern void *ijl_gc_small_alloc(void *,int,int,uintptr_t);
extern void *ijl_gc_big_alloc(void *,size_t,uintptr_t);
extern void *gc_alloc_obj(void *,size_t,void *) __asm__("julia.gc_alloc_obj");
static void *volatile observed;
static _Alignas(16) unsigned char tag[16];
static int zero(void *p,size_t n) {
    unsigned char *s=p;
    for (size_t i=0;i<n;i++) if (s[i]) return 0;
    return 1;
}
int main(int argc,char **argv) {
    if (argc>1) {
        if (argv[1][0]=='s') {
            void *p=memalign(8192,19);
            CHECK(p && (uintptr_t)p%8192==0 && zero(p,19)); free(p); return 0;
        }
        if (argv[1][0]=='o') observed=memalign(64,SIZE_MAX);
        if (argv[1][0]=='r') observed=__rust_alloc(3,3);
        if (argv[1][0]=='j') observed=jl_gc_alloc(0,SIZE_MAX,tag);
        return 90;
    }
    __bennett_nz_alloc(3);
    void *after_raw=memalign(64,19);
    CHECK(after_raw && (uintptr_t)after_raw%64==0 && zero(after_raw,19)); free(after_raw);
    /* Force every residue against each requested alignment. No frontend
     * vectorisation is disabled; the comparison fixture is scalarised at N21. */
    for (size_t a=1;a<=256;a*=2) {
        for (size_t n=0;n<34;n++) {
            __bennett_rt_alloc(n%7+1);
            unsigned char *p=memalign(a,n);
            CHECK(p && (uintptr_t)p%a==0 && (uintptr_t)p%16==0 && zero(p,n));
            for (size_t i=0;i<n;i++) p[i]=(unsigned char)(i+1);
            /* C realloc only promises max_align_t, not the original alignment. */
            unsigned char *q=realloc(p,n+19);
            CHECK(q && (uintptr_t)q%16==0);
            for (size_t i=0;i<n;i++) CHECK(q[i]==(unsigned char)(i+1));
            CHECK(zero(q+n,19)); free(q);
        }
        unsigned char *p=aligned_alloc(a,a*2);
        CHECK(p && (uintptr_t)p%a==0 && zero(p,a*2)); free(p);
        void *out=(void *)(uintptr_t)0x1234;
        int e=posix_memalign(&out,a,19);
        if (a<sizeof(void *)) CHECK(e==EINVAL && out==(void *)(uintptr_t)0x1234);
        else { CHECK(e==0 && out && (uintptr_t)out%a==0 && zero(out,19)); free(out); }
    }
    for (size_t a=0;a<34;a++) if (!a || (a&(a-1))) {
        errno=0; CHECK(!memalign(a,13) && errno==EINVAL);
        errno=0; CHECK(!aligned_alloc(a,0) && errno==EINVAL);
        void *p=(void *)(uintptr_t)0x5678;
        CHECK(posix_memalign(&p,a,31)==EINVAL && p==(void *)(uintptr_t)0x5678);
    }
    errno=0; CHECK(!aligned_alloc(16,17) && errno==EINVAL);
    const char s[]="a short bounded string";
    for (size_t n=0;n<sizeof s+4;n++) {
        char *p=strndup(s,n); size_t len=n<sizeof s-1?n:sizeof s-1;
        CHECK(p && strlen(p)==len && !memcmp(p,s,len) && p[len]==0); free(p);
    }
    char *copy=strdup(s); CHECK(copy && !strcmp(copy,s)); free(copy);
    for (size_t a=1;a<=256;a*=2) for (size_t n=0;n<34;n++) {
        unsigned char *p=__rust_alloc(n,a), *z=__rust_alloc_zeroed(n,a);
        CHECK(p && z && (uintptr_t)p%a==0 && (uintptr_t)z%a==0);
        CHECK(zero(p,n) && zero(z,n));
        for (size_t i=0;i<n;i++) p[i]=(unsigned char)(i+3);
        unsigned char *q=__rust_realloc(p,n,a,n+19);
        CHECK(q && q!=p && (uintptr_t)q%a==0);
        for (size_t i=0;i<n;i++) CHECK(q[i]==(unsigned char)(i+3));
        CHECK(zero(q+n,19));
        unsigned char *r=__rust_realloc(q,n+19,a,2);
        CHECK(r && (uintptr_t)r%a==0);
        for (size_t i=0;i<2 && i<n;i++) CHECK(r[i]==(unsigned char)(i+3));
        __rust_dealloc(z,n,a); __rust_dealloc(r,2,a);
    }
    for (size_t n=0;n<65;n++) {
        void *objects[]={jl_gc_alloc(0,n,tag),ijl_gc_alloc_typed(0,n,(uintptr_t)tag),
                         gc_alloc_obj(0,n,tag),ijl_gc_big_alloc(0,n+8,(uintptr_t)tag),
                         ijl_gc_small_alloc(0,0,(int)n+8,(uintptr_t)tag)};
        for (unsigned i=0;i<5;i++) {
            unsigned char *p=objects[i];
            CHECK(p && (uintptr_t)p%16==0 && zero(p,n));
            /* Stock Julia annotates the returned pointer dereferenceable by
             * the full small/big size argument, including its header count. */
            if (i>=3) CHECK(zero(p,n+8));
            CHECK(((uintptr_t *)p)[-1]==(uintptr_t)tag);
            for (size_t j=0;j<n;j++) p[j]=(unsigned char)(j^0xa5);
        }
    }
    return 0;
}
