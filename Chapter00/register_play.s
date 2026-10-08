// $ aarch64-linux-gnu-as -g register_play.s -o register_play.o
// $ aarch64-linux-gnu-ld register_play.o -o register_play

.global _start
.section .text
_start:
    mov x0, #42        // Move the immediate value 42 into register x0
    mov x1, x0         // Copy the value from x0 to x1

    // Exit syscall
    mov x8, #93        // 93 is the syscall number for exit
    mov x0, #0         // 0 is the exit status
    svc #0             // Supervisor call to invoke the syscall
