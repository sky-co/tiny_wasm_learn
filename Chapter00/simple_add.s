// $aarch64-linux-gnu-as -g simple_add.s -o simple_add.o
// $aarch64-linux-gnu-ld simple_add.o -o simple_add
// $qemu-aarch64 -g 2345 ./simple_add

.global _start
.section .text
_start:
    // Load values into registers
    mov x0, #5          // Put the number 5 into register x0
    mov x1, #3          // Put the number 3 into register x1

    // Add the values
    add x2, x0, x1      // Add x0 and x1, put the result in x2

    // Exit syscall
    mov x8, #93         // 93 is the syscall number for exit
    mov x0, x2
    svc #0              // Supervisor call to invoke the syscall
