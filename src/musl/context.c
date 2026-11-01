/* Static single-thread process context; no host TLS/auxv dependency. The
 * driver's §19.2 entry preparation must install argv/envp before calling main.
 * The stock musl stdio, errno, locale and lock algorithms use this context. */
#include "pthread_impl.h"
struct __libc __libc;
struct pthread __bennett_rt_thread = { .tid = 1, .locale = &__libc.global_locale };
void *__bennett_rt_thread_pointer(void) { return &__bennett_rt_thread; }
extern void *calloc(size_t, size_t);
void *__libc_calloc(size_t n, size_t m) { return calloc(n,m); }
char **__environ;
weak_alias(__environ, environ);
