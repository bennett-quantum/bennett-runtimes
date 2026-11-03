/* Test-only native journal, effect adapter and saved-tape validator. Production
 * bitcode defines neither these marker bodies nor this host history mechanism. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <limits.h>
#include "device_effects.c"
extern unsigned char *__bennett_dev_console_buffer;
extern uint64_t __bennett_dev_console_L, __bennett_dev_console_n;
extern uint32_t __bennett_dev_console_bound, __bennett_dev_console_action;
extern void __bennett_dev_console_write(const unsigned char *,uint64_t);
extern long __bennett_rt_syscall(long,long,long,long,long,long,long);
extern void __bennett_rt_exit(int);
extern struct { int active, selector; void *object; } __bennett_rt_eh;
static long os(long nr,long a,long b,long c) {
    long result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(nr),"D"(a),"S"(b),"d"(c) : "rcx","r11","memory");
    return result;
}
static void (*fault_check)(uint64_t);
void __bennett_nz_fault(uint64_t code) {
    if (fault_check) fault_check(code);
    os(60,100+code,0,0); __builtin_trap();
}
static void copy(void *d,const void *s,size_t n) {
    for(size_t i=0;i<n;++i) ((unsigned char *)d)[i]=((const unsigned char *)s)[i];
}
static int equal(const void *a,const void *b,size_t n) {
    for(size_t i=0;i<n;++i) if(((const unsigned char *)a)[i]!=((const unsigned char *)b)[i]) return 0;
    return 1;
}
struct undo { void *p; size_t n; unsigned char old[256]; };
static struct undo history[4096];
static size_t history_n;
static unsigned char *capacity_buffer;
static unsigned char capacity_before[64];
static void capacity_fault(uint64_t code) {
    if (code!=2 || __bennett_dev_console_n!=0 || history_n ||
        !equal(capacity_buffer,capacity_before,sizeof capacity_before)) os(60,99,0,0);
}
void test_record(void *p,uint64_t n) {
    if(n>256 || history_n==4096) __bennett_nz_fault(2);
    history[history_n].p=p; history[history_n].n=n;
    copy(history[history_n].old,p,n); ++history_n;
}
static void reverse(void) {
    while(history_n) { struct undo *u=&history[--history_n]; copy(u->p,u->old,u->n); }
}
struct event { uint64_t len; unsigned char bytes[128]; };
static struct event tape[32],saved[32];
static uint64_t tape_n,saved_n,replay_pos;
static int replay,mismatch;
void __bennett_effect_console_write(const void *p,uint64_t n) {
    if(n>128 || tape_n==32) __bennett_nz_fault(2);
    if(replay && (replay_pos==saved_n || saved[replay_pos].len!=n || !equal(saved[replay_pos].bytes,p,n))) {
        mismatch=1; return;
    }
    ++replay_pos;
    test_record(&tape[tape_n],sizeof tape[0]); test_record(&tape_n,sizeof tape_n);
    tape[tape_n].len=n; copy(tape[tape_n].bytes,p,n); ++tape_n;
    __bennett_dev_console_write(p,n);
}
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
int main(int argc,char **argv) {
    static unsigned char buffer[64],entry[64];
    for(unsigned i=0;i<64;++i) entry[i]=buffer[i]=0xa5;
    __bennett_dev_console_buffer=buffer;
    __bennett_dev_console_L=sizeof buffer;
    __bennett_dev_console_bound=1;
    CHECK(setvbuf(stdout,0,_IONBF,0)==0);
    if(argc>1) {
        if(argv[1][0]=='u') { __bennett_dev_console_bound=0; __bennett_dev_console_write(0,0); }
        if(argv[1][0]=='a') {
            capacity_buffer=buffer; copy(capacity_before,buffer,sizeof buffer);
            fault_check=capacity_fault;
            __bennett_dev_console_action=1; __bennett_dev_console_write((void *)1,65);
        }
        if(argv[1][0]=='m') __bennett_rt_syscall(999,0,0,0,0,0,0);
        return 99;
    }
    CHECK(puts("hello, world")>=0); CHECK(fflush(stdout)==0);
    CHECK(__bennett_dev_console_n==13 && equal(buffer,"hello, world\n",13));
    saved_n=tape_n; copy(saved,tape,sizeof tape);
    reverse(); CHECK(tape_n==0 && __bennett_dev_console_n==0 && equal(buffer,entry,64));
    static struct event empty[32]; CHECK(equal(tape,empty,sizeof tape));
    replay=1; replay_pos=0;
    CHECK(puts("hello, world")>=0); CHECK(fflush(stdout)==0);
    CHECK(!mismatch && replay_pos==saved_n && equal(tape,saved,sizeof tape));
    CHECK(__bennett_dev_console_n==13 && equal(buffer,"hello, world\n",13));
    reverse(); CHECK(tape_n==0 && __bennett_dev_console_n==0 && equal(buffer,entry,64));
    saved[0].bytes[0]^=1; replay_pos=0;
    CHECK(puts("hello, world")>=0); CHECK(fflush(stdout)==0);
    CHECK(mismatch && tape_n==0 && __bennett_dev_console_n==0 && equal(buffer,entry,64));
    replay=0;
    struct iovec v[3]={{"ab",2},{0,0},{"cd",2}};
    CHECK(writev(2,v,3)==4 && __bennett_dev_console_n==4 && equal(buffer,"abcd",4));
    reverse(); CHECK(equal(buffer,entry,64));
    CHECK(write(1,"!",1)==1 && buffer[0]=='!'); reverse();
    CHECK(write(1,"",0)==0 && history_n==0);
    errno=0; CHECK(writev(1,v,-1)==-1 && errno==EINVAL && history_n==0);
    CHECK(writev(1,(void *)1,1025)==-1 && errno==EINVAL && history_n==0);
    CHECK(writev(1,0,1)==-1 && errno==EFAULT && history_n==0);
    CHECK(writev(1,0,0)==0 && history_n==0);
    static struct iovec empty_vectors[1024];
    CHECK(writev(1,empty_vectors,1024)==0 && history_n==0);
    v[0].iov_len=LONG_MAX; v[2].iov_len=1;
    CHECK(writev(1,v,3)==-1 && errno==EINVAL && history_n==0);
    CHECK(__bennett_rt_syscall(16,1,0x5413,0,0,0,0)==-25 && history_n==0);
    CHECK(__bennett_rt_syscall(8,1,0,0,0,0,0)==-29 && history_n==0);
    CHECK(__bennett_rt_syscall(3,1,0,0,0,0,0)==0 && history_n>0);
    CHECK(__bennett_rt_syscall(3,1,0,0,0,0,0)==-9);
    CHECK(__bennett_rt_syscall(1,1,(long)"X",1,0,0,0)==-9 && !__bennett_dev_console_n);
    CHECK(__bennett_rt_syscall(20,1,(long)v,1,0,0,0)==-9);
    CHECK(__bennett_rt_syscall(1,2,(long)"Y",1,0,0,0)==1 && buffer[0]=='Y');
    reverse(); CHECK(!__bennett_dev_console_n && equal(buffer,entry,64));
    CHECK(__bennett_rt_syscall(1,1,(long)"Z",1,0,0,0)==1 && buffer[0]=='Z');
    reverse(); CHECK(!__bennett_dev_console_n && equal(buffer,entry,64));
    __bennett_dev_console_action=1;
    __bennett_dev_console_write((const unsigned char *)"action",6);
    CHECK(__bennett_dev_console_n==6 && equal(buffer,"action",6));
    reverse(); CHECK(equal(buffer,entry,64) && __bennett_dev_console_n==0);
    __bennett_dev_console_action=0;
    __bennett_dev_console_L=3;
    __bennett_dev_console_write((const unsigned char *)"abcdef",6);
    CHECK(__bennett_dev_console_n==3 && equal(buffer,"abc",3));
    __bennett_dev_console_write((const unsigned char *)"discarded",9);
    CHECK(__bennett_dev_console_n==3);
    reverse(); CHECK(__bennett_dev_console_n==0 && equal(buffer,entry,64));
    __bennett_dev_console_L=0; __bennett_dev_console_buffer=0;
    __bennett_dev_console_write(0,10); CHECK(__bennett_dev_console_n==0);
    reverse();
    __bennett_rt_exit(23); CHECK(__bennett_rt_eh.active==4 && __bennett_rt_eh.selector==23 && !__bennett_rt_eh.object);
    CHECK(os(1,1,(long)"hello, world\n",13)==13);
    return 0;
}
