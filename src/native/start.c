#include <stdlib.h>
extern int main(int, char **, char **);
extern char **environ;
_Noreturn void __bennett_rt_start(long *stack) {
    int argc = stack[0];
    char **argv = (void *)(stack+1);
    environ = argv+argc+1;
    exit(main(argc, argv, environ));
}
