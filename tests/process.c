#define _GNU_SOURCE
#include <errno.h>
#include <fenv.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern char *__progname, *__progname_full;
extern void *__bennett_rt_thread_pointer(void);
extern void __bennett_rt_prepare_process(int,char **,char **);
extern uint64_t __bennett_rt_fenv_get_env(void);
extern void __bennett_rt_fenv_set_env(uint64_t);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int eq(const char *a,const char *b) {
    while (*a && *a==*b) { ++a; ++b; } return *a==*b;
}
static void first(void) { if (write(1,"1",1)!=1) _Exit(91); }
static void second(void) { if (write(1,"2",1)!=1) _Exit(92); }
int main(int argc,char **argv,char **envp) {
    CHECK(argc==2 && argv && !argv[argc]);
    CHECK(environ==envp && envp && envp[0] && !envp[1]);
    CHECK(eq(getenv("BRT_CTX"),"value"));
    CHECK(program_invocation_name==__progname_full && eq(__progname_full,argv[0]));
    const char *shortname=argv[0];
    for (const char *p=argv[0];*p;p++) if (*p=='/') shortname=p+1;
    CHECK(program_invocation_short_name==__progname && eq(__progname,shortname));
    CHECK(__bennett_rt_thread_pointer()==(void *)(uintptr_t)pthread_self());
    CHECK(errno==0 && fegetround()==FE_TONEAREST && __bennett_rt_fenv_get_env()==0);
    if (argv[1][0]=='p') {
        errno=42; __bennett_rt_fenv_set_env((uint64_t)31<<32|4);
        __bennett_rt_prepare_process(argc,argv,envp);
        CHECK(errno==0 && __bennett_rt_fenv_get_env()==0 && environ==envp);
        CHECK(eq(__progname_full,argv[0]) && eq(__progname,shortname));
    }
    if (argv[1][0]=='z') {
        static char *empty[]={0};
        __bennett_rt_prepare_process(0,empty,empty);
        CHECK(!*__progname && !*__progname_full && environ==empty);
        return 0;
    }
    if (argv[1][0]=='b') {
        __bennett_rt_prepare_process(-1,argv,envp);
        return 98;
    }
    static char buffer[128];
    CHECK(setvbuf(stdout,buffer,_IOFBF,sizeof buffer)==0);
    CHECK(atexit(first)==0 && atexit(second)==0);
    CHECK(fputs("buffered",stdout)>=0);
    if (argv[1][0]=='e') exit(23);
    if (argv[1][0]=='x') _Exit(29);
    return 17;
}
