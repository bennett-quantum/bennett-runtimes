/* Comparison-only device transports. Descriptor, syscall and filesystem
 * bodies retain their fixed, checked materialised definitions. */
#include "layout.h"
extern void __bennett_dev_clock_read(void *);
extern void __bennett_dev_entropy_read(void *,uint64_t);
extern void __bennett_dev_block_read(uint64_t,void *);
extern void __bennett_dev_block_write(uint64_t,const void *);
long __bennett_rt_transport_console_write(long fd,const void *p,uint64_t len) {
    if(__bennett_rt_native_objects) __bennett_rt_native_check(p,len);
    if(len && !p) __bennett_nz_fault(7);
    const unsigned char *bytes=p; uint64_t left=len;
    long target=fd==2 && !__bennett_rt_native_objects?2:1;
    while(left) {
        long n=__bennett_rt_native_os(1,target,(long)bytes,left,0,0,0);
        if(n==-4) continue;
        if(n<=0 || (uint64_t)n>left) __bennett_nz_fault(8);
        bytes+=n; left-=n;
    }
    return len;
}
void __bennett_rt_native_console_write(const void *p,uint64_t n) {
    (void)__bennett_rt_transport_console_write(1,p,n);
}
void __bennett_rt_transport_clock_read(void *p) {
    if(__bennett_rt_native_objects) __bennett_rt_native_check(p,8);
    __bennett_dev_clock_read(p);
}
void __bennett_rt_transport_entropy_read(void *p,uint64_t n) {
    if(__bennett_rt_native_objects) __bennett_rt_native_check(p,n);
    __bennett_dev_entropy_read(p,n);
}
void __bennett_rt_transport_block_read(uint64_t s,void *p) {
    if(__bennett_rt_native_objects) __bennett_rt_native_check(p,512);
    __bennett_dev_block_read(s,p);
}
void __bennett_rt_transport_block_write(uint64_t s,const void *p) {
    if(__bennett_rt_native_objects) __bennett_rt_native_check(p,512);
    __bennett_dev_block_write(s,p);
}
void __bennett_rt_transport_exit(int status) {
    __bennett_rt_native_os(231,status,0,0,0,0,0); __builtin_trap();
}
