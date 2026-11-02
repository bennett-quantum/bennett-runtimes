#include <stdarg.h>
#include <stdint.h>
static int sum(int n,...) {
    va_list ap; va_start(ap,n); int total=0;
    while (n--) total+=va_arg(ap,int);
    va_end(ap); return total;
}
static int forwarded(va_list ap) {
    va_list copy; va_copy(copy,ap);
    int a=va_arg(copy,int), b=va_arg(copy,int);
    va_end(copy); return a*10+b;
}
static int forward(int n,...) {
    va_list ap; va_start(ap,n);
    int x=forwarded(ap), y=va_arg(ap,int);
    va_end(ap); return x+y;
}
struct Large { uint64_t words[5]; };
static int mixed(int ignored,...) {
    (void)ignored;
    va_list ap; va_start(ap,ignored);
    int a=va_arg(ap,int); const int *p=va_arg(ap,const int *);
    uint64_t b=va_arg(ap,uint64_t); double c=va_arg(ap,double);
    long double d=va_arg(ap,long double);
    struct Large large=va_arg(ap,struct Large);
    va_end(ap);
    return a==17 && *p==29 && b==UINT64_C(0xfedcba9876543210) && c==1.25 && d==2.5L &&
           large.words[0]==101 && large.words[4]==505;
}
int main(void) {
    int v=29; struct Large large={{101,202,303,404,505}};
    if (sum(0)!=0 || sum(6,17,18,19,20,21,22)!=117) return 91;
    if (forward(2,3,4)!=37) return 92;
    if (!mixed(0,17,&v,UINT64_C(0xfedcba9876543210),1.25,2.5L,large)) return 93;
    return 0;
}
