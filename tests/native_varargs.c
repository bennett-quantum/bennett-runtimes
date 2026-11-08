#include <stdarg.h>
#include <stdint.h>
extern void __bennett_rt_native_check(const void *, uint64_t);
#define NOINLINE __attribute__((noinline))
struct List { unsigned gp, fp; unsigned char *overflow, *save; };
static unsigned char *escaped;
static NOINLINE int sum(int n, ...) {
    va_list ap; va_start(ap,n); int total=0;
    while (n--) total+=va_arg(ap,int);
    va_end(ap); return total;
}
static int (*volatile indirect)(int, ...)=sum;
static NOINLINE int copy(va_list ap) {
    va_list other; va_copy(other,ap);
    int a=va_arg(other,int), b=va_arg(other,int);
    va_end(other); return a*10+b;
}
static NOINLINE int restart(int n, ...) {
    va_list ap; va_start(ap,n);
    int a=copy(ap), b=va_arg(ap,int);
    va_end(ap);
    if (sum(7,1,2,3,4,5,6,7)!=28) return 90;
    va_start(ap,n); int c=va_arg(ap,int); va_end(ap);
    return a+b+c;
}
struct Large { uint64_t words[5]; };
static NOINLINE int mixed(int n, ...) {
    va_list ap; va_start(ap,n);
    int a=va_arg(ap,int); const int *p=va_arg(ap,const int *);
    double d=va_arg(ap,double); long double ld=va_arg(ap,long double);
    struct Large large=va_arg(ap,struct Large);
    int b=va_arg(ap,int); va_end(ap);
    return a==17 && *p==29 && d==1.25 && ld==2.5L &&
        large.words[0]==101 && large.words[4]==505 && b==31;
}
static NOINLINE int floating(int n, ...) {
    va_list ap; va_start(ap,n); double total=0;
    while (n--) total+=va_arg(ap,double);
    va_end(ap); return total==55.0;
}
static NOINLINE int named(int a,int b,int c,int d,int e,int f,int g, ...) {
    va_list ap; va_start(ap,g);
    long double x=va_arg(ap,long double); int y=va_arg(ap,int);
    va_end(ap); return a+b+c+d+e+f+g==28 && x==2.5L && y==19;
}
static NOINLINE void bounds(int mode, ...) {
    va_list ap; va_start(ap,mode);
    struct List *list=(void *)ap;
    if (mode==1) __bennett_rt_native_check(list->save+175,2);
    /* Ten integers: five GP slots after the named mode, then five 8-byte
     * stack slots. The last byte plus its successor crosses that object. */
    if (mode==2) __bennett_rt_native_check(list->overflow+39,2);
    if (mode==3) escaped=list->save+8;
    if (mode==4) escaped=list->overflow;
    va_end(ap);
}
static NOINLINE void exhausted(int a,int b,int c,int d,int e,int f,
        double p,double q,double r,double s,double t,double u,double v,double w,...) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;
    (void)p;(void)q;(void)r;(void)s;(void)t;(void)u;(void)v;
    va_list ap; va_start(ap,w);
    __bennett_rt_native_check(0,1); /* No save object may authorize null. */
    va_end(ap);
}
static NOINLINE __attribute__((target("no-sse"))) int integer_only(int n,...) {
    va_list ap; va_start(ap,n); int total=0;
    if (n<0) {
        struct List *list=(void *)ap;
        __bennett_rt_native_check(list->save+47,2);
    }
    while (n-->0) total+=va_arg(ap,int);
    va_end(ap); return total;
}
int main(int argc,char **argv) {
    int mode=argc>1?argv[1][0]-'0':0;
    if (mode==6) { integer_only(-1,1,2,3,4,5,6); return 98; }
    if (mode==5) { exhausted(1,2,3,4,5,6,1.,2.,3.,4.,5.,6.,7.,8.); return 97; }
    if (mode) {
        bounds(mode,1,2,3,4,5,6,7,8,9,10);
        if (mode>=3) __bennett_rt_native_check(escaped,1);
        return 94;
    }
    int v=29; struct Large large={{101,202,303,404,505}};
    if (integer_only(7,1,2,3,4,5,6,7)!=28) return 99;
    if (sum(0)!=0 || sum(10,1,2,3,4,5,6,7,8,9,10)!=55) return 91;
    if (indirect(6,17,18,19,20,21,22)!=117) return 92;
    if (restart(2,3,4)!=40 || !mixed(0,17,&v,1.25,2.5L,large,31)) return 93;
    if (!floating(10,1.,2.,3.,4.,5.,6.,7.,8.,9.,10.)) return 95;
    if (!named(1,2,3,4,5,6,7,2.5L,19)) return 96;
    return 0;
}
