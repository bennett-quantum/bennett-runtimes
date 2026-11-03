/* Ordinary libc device/descriptor dispatch. Only the named device operations
 * use tier-specific transports; filesystem state never reaches host Linux. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include "vfs.h"
extern _Noreturn void __bennett_nz_fault(uint64_t);
extern uint64_t __bennett_dev_block_nsect;
extern long __bennett_rt_transport_console_write(long,const void *,uint64_t);
extern void __bennett_rt_transport_clock_read(void *);
extern void __bennett_rt_transport_entropy_read(void *,uint64_t);
extern void __bennett_rt_transport_block_read(uint64_t,void *);
extern void __bennett_rt_transport_block_write(uint64_t,const void *);
extern void __bennett_rt_transport_exit(int);
struct { int active, selector; void *object; } __bennett_rt_eh;
void __bennett_rt_exit(int status) {
    __bennett_rt_eh.active=4;
    __bennett_rt_eh.selector=status;
    __bennett_rt_eh.object=0;
}
static uint64_t clock_value(void) {
    unsigned char bytes[8];
    __bennett_rt_transport_clock_read(bytes);
    uint64_t value=0;
    for(unsigned i=0;i<8;++i) value|=(uint64_t)bytes[i]<<(8*i);
    return value;
}
uint64_t __bennett_rt_cyclecounter(void) { return clock_value(); }
static long transfer(long fd,void *p,uint64_t n,int writing,uint64_t offset,int positional) {
    struct bennett_file *file=__bennett_rt_vfs_file(fd);
    if(!file) return -EBADF;
    if((file->flags&O_PATH) || (writing?(file->flags&O_ACCMODE)==O_RDONLY:(file->flags&O_ACCMODE)==O_WRONLY)) return -EBADF;
    if(n>LONG_MAX) return -EINVAL;
    if(n && !p) return -EFAULT;
    if(positional && file->kind!=BRT_REGULAR && file->kind!=BRT_BLOCK) return -ESPIPE;
    if(positional && offset>LONG_MAX) return -EINVAL;
    if(file->kind==BRT_CONSOLE)
        return __bennett_rt_transport_console_write(fd,p,n);
    if(file->kind==BRT_ENTROPY) {
        if(writing) __bennett_nz_fault(8);
        __bennett_rt_transport_entropy_read(p,n); return n;
    }
    if(file->kind==BRT_INPUT) {
        if(!n) return 0;
        __bennett_nz_fault(8);
    }
    if(file->kind==BRT_BLOCK) {
        uint64_t pos=positional?offset:file->pos;
        if(pos>LONG_MAX || n>(uint64_t)LONG_MAX-pos) return -EFBIG;
        unsigned char block[512], *bytes=p;
        for(uint64_t done=0;done<n;) {
            uint64_t sector=(pos+done)/512, index=(pos+done)%512, count=512-index;
            if(count>n-done) count=n-done;
            if(!writing || index || count!=512) __bennett_rt_transport_block_read(sector,block);
            if(writing) {
                for(uint64_t i=0;i<count;++i) block[index+i]=bytes[done+i];
                __bennett_rt_transport_block_write(sector,block);
            } else for(uint64_t i=0;i<count;++i) bytes[done+i]=block[index+i];
            done+=count;
        }
        if(!positional) file->pos=pos+n; return n;
    }
    return __bennett_rt_vfs_io(file,p,n,writing,offset,positional);
}
long __bennett_rt_syscall(long nr,long a,long b,long c,long d,long e,long f) {
    (void)f;
    if(nr==60 || nr==231) {
        __bennett_rt_transport_exit((int)a); return 0;
    }
    if(nr==228) {
        if(a<0 || a>11 || a==10) return -EINVAL;
        if(!b) return -EFAULT;
        uint64_t value=clock_value();
        long *ts=(void *)b; ts[0]=value/1000000000; ts[1]=value%1000000000; return 0;
    }
    if(nr==229) {
        if(a<0 || a>11 || a==10) return -EINVAL;
        if(b) { long *ts=(void *)b; ts[0]=0; ts[1]=1; }
        return 0;
    }
    if(nr==96) {
        if(a) {
            uint64_t value=clock_value();
            long *tv=(void *)a; tv[0]=value/1000000000; tv[1]=(value%1000000000)/1000;
        }
        if(b) { int *tz=(void *)b; tz[0]=tz[1]=0; }
        return 0;
    }
    if(nr==201) {
        long seconds=clock_value()/1000000000;
        if(a) *(long *)a=seconds;
        return seconds;
    }
    if(nr==318) {
        if((unsigned long)b>LONG_MAX || (c&~7L) || ((c&6)==6)) return -EINVAL;
        if(b && !a) return -EFAULT;
        __bennett_rt_transport_entropy_read((void *)a,b); return b;
    }
    if(nr==2 || nr==257) return nr==2?__bennett_rt_vfs_open((void *)a,b,c,AT_FDCWD):__bennett_rt_vfs_open((void *)b,c,d,a);
    if(nr==85) return __bennett_rt_vfs_open((void *)a,O_CREAT|O_WRONLY|O_TRUNC,b,AT_FDCWD);
    if(nr==3) return __bennett_rt_vfs_close(a);
    if(nr==32) return __bennett_rt_vfs_dup(a,0,0,0);
    if(nr==33) return __bennett_rt_vfs_dup(a,b,1,0);
    if(nr==292) {
        if(a==b || (c&~O_CLOEXEC)) return -EINVAL;
        return __bennett_rt_vfs_dup(a,b,1,!!(c&O_CLOEXEC));
    }
    if(nr==0 || nr==1 || nr==17 || nr==18) return transfer(a,(void *)b,c,nr==1 || nr==18,d,nr==17 || nr==18);
    if(nr==19 || nr==20) {
        if(!__bennett_rt_vfs_file(a)) return -EBADF;
        if((unsigned long)c>1024) return -EINVAL;
        if(c && !b) return -EFAULT;
        struct iovec *v=(void *)b; uint64_t total=0;
        for(long i=0;i<c;++i) {
            if(v[i].iov_len>LONG_MAX-total) return -EINVAL;
            total+=v[i].iov_len;
        }
        uint64_t done=0;
        for(long i=0;i<c;++i) if(v[i].iov_len) {
            long n=transfer(a,v[i].iov_base,v[i].iov_len,nr==20,0,0);
            if(n<0) return done?(long)done:n;
            done+=n; if((uint64_t)n<v[i].iov_len) break;
        }
        return done;
    }
    if(nr==8 || nr==16) {
        struct bennett_file *file=__bennett_rt_vfs_file(a); if(!file) return -EBADF;
        if(nr==8) {
            uint64_t size=__bennett_dev_block_nsect;
            if(size>(uint64_t)LONG_MAX/512) return -EOVERFLOW;
            return __bennett_rt_vfs_seek(file,b,c,size*512);
        }
        return -ENOTTY;
    }
    if(nr==4 || nr==5 || nr==6 || nr==21 || nr==72 || nr==74 || nr==75 || nr==76 || nr==77 ||
       nr==79 || nr==80 || nr==81 || nr==82 || nr==83 || nr==84 || nr==87 || nr==90 ||
       nr==258 || nr==262 || nr==263 || nr==269) return __bennett_rt_vfs_meta(nr,a,b,c,d,e);
    __bennett_nz_fault(8);
}
