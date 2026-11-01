/* Comparison-only Linux process entry. No host CRT or libc. */
.text
.global _start
.type _start,@function
_start:
    xor %ebp,%ebp
    mov %rsp,%rdi
    and $-16,%rsp
    call __bennett_rt_start
    ud2
.section .note.GNU-stack,"",@progbits
