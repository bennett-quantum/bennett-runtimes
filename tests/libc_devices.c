#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/random.h>
#include <unistd.h>
#include <fcntl.h>
extern uint64_t __bennett_dev_clock_c, __bennett_dev_entropy_size, __bennett_dev_entropy_pos;
extern uint32_t __bennett_dev_clock_bound, __bennett_dev_entropy_bound;
extern const unsigned char *__bennett_dev_entropy_base;
extern void __bennett_dev_clock_read(void *);
extern void __bennett_dev_entropy_read(void *, uint64_t);
extern void __bennett_dev_block_read(uint64_t, void *);
extern void __bennett_dev_block_write(uint64_t, const void *);
/* Test-only materialisation. Production publications never define markers. */
void __bennett_effect_clock_read(void *p) { __bennett_dev_clock_read(p); }
void __bennett_effect_entropy_read(void *p, uint64_t n) { __bennett_dev_entropy_read(p,n); }
void __bennett_effect_block_read(uint64_t s, void *p) { __bennett_dev_block_read(s,p); }
void __bennett_effect_block_write(uint64_t s, const void *p) { __bennett_dev_block_write(s,p); }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static unsigned char tape[1024];
int main(void) {
    for (unsigned i=0;i<sizeof tape;++i) tape[i]=(unsigned char)(17*i+3);
    __bennett_dev_clock_bound=__bennett_dev_entropy_bound=1;
    __bennett_dev_clock_c=UINT64_C(1234567890123);
    __bennett_dev_entropy_base=tape; __bennett_dev_entropy_size=sizeof tape;
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_REALTIME,&ts)==0 && ts.tv_sec==1234 && ts.tv_nsec==567890123);
    CHECK(clock_gettime(CLOCK_MONOTONIC,&ts)==0 && ts.tv_sec==1234 && ts.tv_nsec==567890124);
    struct timeval tv; CHECK(gettimeofday(&tv,0)==0 && tv.tv_sec==1234 && tv.tv_usec==567890);
    time_t t; CHECK(time(&t)==1234 && t==1234);
    uint64_t c=__bennett_dev_clock_c;
    errno=0; CHECK(clock_gettime(99,&ts)==-1 && errno==EINVAL && __bennett_dev_clock_c==c);
    unsigned char bytes[256];
    CHECK(getrandom(bytes,7,0)==7);
    for(unsigned i=0;i<7;++i) CHECK(bytes[i]==tape[i]);
    CHECK(getrandom(bytes,0,0)==0 && __bennett_dev_entropy_pos==7);
    errno=0; CHECK(getrandom(bytes,1,8)==-1 && errno==EINVAL && __bennett_dev_entropy_pos==7);
    CHECK(getentropy(bytes,256)==0);
    for(unsigned i=0;i<256;++i) CHECK(bytes[i]==tape[i+7]);
    errno=0; CHECK(getentropy(bytes,257)==-1 && errno==EIO && __bennett_dev_entropy_pos==263);
    int fd=open("/dev/urandom",O_RDONLY); CHECK(fd>=0);
    CHECK(read(fd,bytes,9)==9 && close(fd)==0);
    for(unsigned i=0;i<9;++i) CHECK(bytes[i]==tape[i+263]);
    fd=open("/dev/random",O_RDONLY); CHECK(fd>=0 && read(fd,bytes,3)==3 && close(fd)==0);
    for(unsigned i=0;i<3;++i) CHECK(bytes[i]==tape[i+272]);
    CHECK(__bennett_dev_entropy_pos==275);
    return 0;
}
