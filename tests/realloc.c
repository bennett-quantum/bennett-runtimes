#include <stdint.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void *volatile observed;
int main(int argc,char **argv) {
    unsigned char *p=malloc(17);
    for (unsigned i=0;i<17;i++) p[i]=(unsigned char)(i^0xa5);
    if (argc>1) {
        switch (argv[1][0]) {
        case 'u': free(p); observed=realloc(p,9); return 90;
        case 'i': observed=realloc(p+1,9); return 91;
        case 'o': observed=realloc(p,SIZE_MAX); return 92;
        }
    }
    unsigned char *q=realloc(p,65);
    CHECK(q && q!=p);
    for (unsigned i=0;i<17;i++) CHECK(q[i]==(unsigned char)(i^0xa5));
    for (unsigned i=17;i<65;i++) CHECK(q[i]==0);
    unsigned char *r=realloc(q,5);
    CHECK(r && r!=q);
    for (unsigned i=0;i<5;i++) CHECK(r[i]==(unsigned char)(i^0xa5));
    unsigned char *z=realloc(r,0);
    CHECK(z && z!=r); free(z);
    z=realloc(0,7);
    CHECK(z);
    for (unsigned i=0;i<7;i++) CHECK(z[i]==0);
    free(z); free(0);
    return 0;
}
