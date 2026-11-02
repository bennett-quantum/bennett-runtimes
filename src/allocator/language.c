/* Scalar allocation ABIs emitted by the C++ and Rust frontends. Payloads,
 * alignment padding and bookkeeping all use the same ordinary C allocator. */
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
extern void *__bennett_rt_aligned_alloc(size_t,size_t);
extern void *__bennett_rt_aligned_realloc(void *,size_t,size_t);

void *memalign(size_t a,size_t n) {
    if (!a || (a&(a-1))) { errno=EINVAL; return 0; }
    return __bennett_rt_aligned_alloc(a,n);
}
void *aligned_alloc(size_t a,size_t n) {
    if (!a || (a&(a-1)) || n%a) { errno=EINVAL; return 0; }
    return __bennett_rt_aligned_alloc(a,n);
}
int posix_memalign(void **out,size_t a,size_t n) {
    if (a<sizeof(void *) || (a&(a-1))) return EINVAL;
    *out=__bennett_rt_aligned_alloc(a,n);
    return 0;
}

void *__rust_alloc(size_t n,size_t a) { return __bennett_rt_aligned_alloc(a,n); }
void *__rust_alloc_zeroed(size_t n,size_t a) { return __bennett_rt_aligned_alloc(a,n); }
void *__rust_realloc(void *p,size_t old,size_t a,size_t n) {
    (void)old; /* The ordinary live record supplies the exact old requested size. */
    return __bennett_rt_aligned_realloc(p,n,a);
}
void __rust_dealloc(void *p,size_t n,size_t a) { (void)n; (void)a; free(p); }

#define NEW(name,symbol,params,body) void *name params __asm__(symbol); void *name params body
#define DELETE(name,symbol,params,body) void name params __asm__(symbol); void name params body
NEW(new_scalar,"_Znwm",(size_t n),{ return malloc(n); })
NEW(new_array,"_Znam",(size_t n),{ return malloc(n); })
NEW(new_nothrow,"_ZnwmRKSt9nothrow_t",(size_t n,const void *tag),{ (void)tag; return malloc(n); })
NEW(new_array_nothrow,"_ZnamRKSt9nothrow_t",(size_t n,const void *tag),{ (void)tag; return malloc(n); })
NEW(new_aligned,"_ZnwmSt11align_val_t",(size_t n,size_t a),{ return __bennett_rt_aligned_alloc(a,n); })
NEW(new_array_aligned,"_ZnamSt11align_val_t",(size_t n,size_t a),{ return __bennett_rt_aligned_alloc(a,n); })
NEW(new_aligned_nothrow,"_ZnwmSt11align_val_tRKSt9nothrow_t",(size_t n,size_t a,const void *tag),{ (void)tag; return __bennett_rt_aligned_alloc(a,n); })
NEW(new_array_aligned_nothrow,"_ZnamSt11align_val_tRKSt9nothrow_t",(size_t n,size_t a,const void *tag),{ (void)tag; return __bennett_rt_aligned_alloc(a,n); })
DELETE(delete_scalar,"_ZdlPv",(void *p),{ free(p); })
DELETE(delete_array,"_ZdaPv",(void *p),{ free(p); })
DELETE(delete_sized,"_ZdlPvm",(void *p,size_t n),{ (void)n; free(p); })
DELETE(delete_array_sized,"_ZdaPvm",(void *p,size_t n),{ (void)n; free(p); })
DELETE(delete_aligned,"_ZdlPvSt11align_val_t",(void *p,size_t a),{ (void)a; free(p); })
DELETE(delete_array_aligned,"_ZdaPvSt11align_val_t",(void *p,size_t a),{ (void)a; free(p); })
DELETE(delete_sized_aligned,"_ZdlPvmSt11align_val_t",(void *p,size_t n,size_t a),{ (void)n; (void)a; free(p); })
DELETE(delete_array_sized_aligned,"_ZdaPvmSt11align_val_t",(void *p,size_t n,size_t a),{ (void)n; (void)a; free(p); })
DELETE(delete_nothrow,"_ZdlPvRKSt9nothrow_t",(void *p,const void *tag),{ (void)tag; free(p); })
DELETE(delete_array_nothrow,"_ZdaPvRKSt9nothrow_t",(void *p,const void *tag),{ (void)tag; free(p); })
DELETE(delete_aligned_nothrow,"_ZdlPvSt11align_val_tRKSt9nothrow_t",(void *p,size_t a,const void *tag),{ (void)a; (void)tag; free(p); })
DELETE(delete_array_aligned_nothrow,"_ZdaPvSt11align_val_tRKSt9nothrow_t",(void *p,size_t a,const void *tag),{ (void)a; (void)tag; free(p); })
