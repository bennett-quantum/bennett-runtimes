/* Static single-thread process context; no host TLS/auxv dependency. The
 * driver's §19.2 entry preparation calls this helper on fresh static images.
 * The stock musl stdio, errno, locale and lock algorithms use this context. */
#include "pthread_impl.h"
#include <stddef.h>
#include <stdint.h>
struct __libc __libc = { .page_size = 4096 };
#define INITIAL_THREAD { .self=&__bennett_rt_thread, \
    .prev=&__bennett_rt_thread, .next=&__bennett_rt_thread, \
    .tid=1, .detach_state=DT_JOINABLE, .locale=&__libc.global_locale, \
    .robust_list={ .head=&__bennett_rt_thread.robust_list.head, \
        .off=(long)offsetof(pthread_mutex_t,_m_lock)-(long)offsetof(pthread_mutex_t,_m_next) } }
struct pthread __bennett_rt_thread = INITIAL_THREAD;
void *__bennett_rt_thread_pointer(void) { return &__bennett_rt_thread; }
extern void *calloc(size_t, size_t);
void *__libc_calloc(size_t n, size_t m) { return calloc(n,m); }
char **__environ;
weak_alias(__environ, environ);
static char empty_program[]="";
char *__progname=empty_program, *__progname_full=empty_program;
weak_alias(__progname, program_invocation_short_name);
weak_alias(__progname_full, program_invocation_name);
extern _Noreturn void __bennett_nz_fault(uint64_t);
extern void __bennett_rt_fenv_reset(void);

void __bennett_rt_prepare_process(int argc,char **argv,char **envp) {
    /* Reject a malformed supplied vector before altering libc state. String
     * bytes and their allocation bounds are prepared by the driver; all
     * ordinary reads retain the materialised memory checks. */
    if (argc<0 || !argv || !envp) __bennett_nz_fault(4);
    for (int i=0;i<argc;++i) if (!argv[i]) __bennett_nz_fault(4);
    if (argv[argc]) __bennett_nz_fault(4);
    __libc=(struct __libc){ .page_size=4096 };
    __bennett_rt_thread=(struct pthread)INITIAL_THREAD;
    __environ=envp;
    __progname_full=argc ? argv[0] : empty_program;
    __progname=__progname_full;
    for (char *p=__progname_full;*p;++p) if (*p=='/') __progname=p+1;
    __bennett_rt_fenv_reset();
}
