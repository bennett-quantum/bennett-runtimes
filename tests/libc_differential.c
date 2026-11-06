/* Defined C behaviour shared by the runtime and the host libc oracle. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) return __LINE__; } while(0)
int main(void) {
    static const size_t sizes[]={1,15,16,17,63,64,65,255,1025};
    for(size_t i=0;i<sizeof sizes/sizeof *sizes;++i) {
        size_t n=sizes[i];
        unsigned char *p=calloc(n,1);
        CHECK(p && (uintptr_t)p%_Alignof(max_align_t)==0);
        for(size_t k=0;k<n;++k) { CHECK(p[k]==0); p[k]=(unsigned char)(k*13+7); }
        unsigned char *q=realloc(p,n+37);
        CHECK(q && (uintptr_t)q%_Alignof(max_align_t)==0);
        for(size_t k=0;k<n;++k) CHECK(q[k]==(unsigned char)(k*13+7));
        q=realloc(q,n/2+1); CHECK(q);
        for(size_t k=0;k<n/2+1;++k) CHECK(q[k]==(unsigned char)(k*13+7));
        free(q);
    }
    free(NULL);
    for(int value=-17;value<=17;++value) {
        char buffer[128];
        int n=snprintf(buffer,sizeof buffer,"%+08d|%#12x|%-7.4s|%zu|%lld",value,
                       (unsigned)(value*value),"abcdef",(size_t)1025,(long long)value*123456789);
        CHECK(n>0 && (size_t)n<sizeof buffer);
        CHECK(fwrite(buffer,1,(size_t)n,stdout)==(size_t)n);
        CHECK(fputc('\n',stdout)=='\n');
        char short_buffer[4];
        CHECK(snprintf(short_buffer,sizeof short_buffer,"abcdef") == 6);
        CHECK(!memcmp(short_buffer,"abc",4));
    }
    CHECK(fprintf(stderr,"allocator and formatted I/O verified\n")==37);
    CHECK(fflush(stdout)==0 && fflush(stderr)==0);
    return 0;
}
