#include <stddef.h>
#include <stdint.h>
namespace std {
    struct nothrow_t { explicit nothrow_t() = default; };
    const nothrow_t nothrow;
    enum class align_val_t : size_t {};
}
void *operator new(size_t);
void *operator new[](size_t);
void *operator new(size_t,const std::nothrow_t&) noexcept;
void *operator new[](size_t,const std::nothrow_t&) noexcept;
void *operator new(size_t,std::align_val_t);
void *operator new[](size_t,std::align_val_t);
void operator delete(void *) noexcept;
void operator delete[](void *) noexcept;
struct alignas(128) Aligned { unsigned char bytes[128]; };
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void *volatile keep;
extern "C" int main() {
    for (size_t n=0;n<65;n++) {
        unsigned char *p=new unsigned char[n], *q=new(std::nothrow) unsigned char[n];
        keep=p; keep=q;
        CHECK(p && q && (uintptr_t)p%16==0 && (uintptr_t)q%16==0);
        for (size_t i=0;i<n;i++) CHECK(p[i]==0 && q[i]==0);
        delete[] p; delete[] q;
    }
    unsigned long *p=new unsigned long; keep=p; CHECK(p && *p==0); delete p;
    p=new(std::nothrow) unsigned long; keep=p; CHECK(p && *p==0); delete p;
    Aligned *a=new Aligned, *b=new Aligned[3]; keep=a; keep=b;
    CHECK((uintptr_t)a%128==0 && (uintptr_t)b%128==0);
    for (size_t i=0;i<128;i++) CHECK(a->bytes[i]==0);
    for (size_t j=0;j<3;j++) for (size_t i=0;i<128;i++) CHECK(b[j].bytes[i]==0);
    delete a; delete[] b;
    return 0;
}
