#include <stdarg.h>
typedef int vector __attribute__((vector_size(32)));
__attribute__((noinline)) int read_vector(int ignored,...) {
    va_list ap; va_start(ap,ignored);
    vector value=va_arg(ap,vector); va_end(ap);
    int sum=0; for (int i=0;i<8;++i) sum+=value[i];
    return sum;
}
