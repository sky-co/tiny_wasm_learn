.section .text
.global _start

_start:
/* syscall write(int fd, const void *buf, size_t count) */
    mov r0, #1
    ldr r1, =msg
    ldr r2, =len
    mov r7, #4
    svc #0

/* syscall exit(int status) */
    mov r0, #0
    mov r7, #1
    svc #0

msg:
.ascii "Hello, ARM32!\n"
len = . - msg
