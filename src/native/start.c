#include <stdlib.h>
extern int main(int, char **, char **);
extern char **environ;
_Noreturn void __bennett_rt_start(long *stack) {
    int argc = stack[0];
    char **argv = (void *)(stack+1);
    environ = argv+argc+1;
    exit(main(argc, argv, environ));
}

/* Naked entry preserves the kernel's original stack and SysV call alignment.
 * A bitcode definition retains _start through the native-only merge. */
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %ebp,%ebp\nmov %rsp,%rdi\nand $-16,%rsp\ncall __bennett_rt_start\nud2");
}
