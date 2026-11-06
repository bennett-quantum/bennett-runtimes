/* Exercise the comparison transport with real host errors and short writes. */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
const uint64_t __bennett_rt_native_layout[3]={1,0x20000000,4096};
static int scenario, calls;
long __bennett_rt_native_os(long n,long a,long b,long c,long d,long e,long f) {
    if (n==SYS_write && a==STDOUT_FILENO && scenario) {
        if (scenario==1) return 0;
        if (scenario==2) return c+1;
        if (!calls++) return -EINTR;
        if (c>1) c=1;
    }
    long r=syscall(n,a,b,c,d,e,f);
    return r<0?-errno:r;
}
extern long __bennett_rt_transport_console_write(long,const void *,uint64_t);
int main(int argc,char **argv) {
    scenario=argc>1?atoi(argv[1]):0;
    return __bennett_rt_transport_console_write(1,"hello",5)!=5;
}
