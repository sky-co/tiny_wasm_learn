// $ aarch64-linux-gnu-as hello.s -o hello.o
// $ aarch64-linux-gnu-ld hello.o -o hello

.data
    message: .ascii "Hello World!\n"
    len = . - message

.text
.global _start

_start:
    // Write the message to stdout
    mov x0, #1
    ldr x1, =message
    mov x2, len
    mov x8, #64
    svc #0

    // Exit the program
    mov x0, #0
    mov x8, #93
    svc #0
