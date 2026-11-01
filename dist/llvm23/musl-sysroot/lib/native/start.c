#include <stdlib.h>
#include "layout.h"
extern int main(int, char **, char **);
extern char **environ;
extern void __bennett_rt_prepare_process(int,char **,char **);
_Noreturn void __bennett_rt_start(long *stack) {
    int argc = stack[0];
    char **argv = (void *)(stack+1);
    char **envp=argv+argc+1;
    __bennett_rt_native_inputs(argc,argv,envp);
    __bennett_rt_prepare_process(argc,argv,envp);
    exit(main(argc, argv, environ));
}

/* Naked entry preserves the kernel's original stack and SysV call alignment.
 * A bitcode definition retains _start through the native-only merge. */
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %ebp,%ebp\nmov %rsp,%rdi\nand $-16,%rsp\ncall __bennett_rt_start\nud2");
}
