// $ aarch64-linux-gnu-as -g fibonacci64.s -o fibonacci64.o
// $ aarch64-linux-gnu-ld fibonacci64.o -o fibonacci64
// $ qemu-aarch64 -g 2345 ./fibonacci64

// $ qemu-aarch64 ./fibonacci64
//echo $?

.global _start
.section .text

_start:
    mov x0, #4
    mov x1, #0          // a = F(0)
    mov x2, #1          // b = F(1)
    mov x3, #0          // i = 0

    bl cal_fib

    // Exit syscall
    mov x8, #93        // 93 is the syscall number for exit
    svc #0             // Supervisor call to invoke the syscall

cal_fib:
loop:
    cmp x3, x0
    b.ge end

    add x4, x1, x2      // next = a + b
    mov x1, x2          // a = b
    mov x2, x4          // b = next
    add x3, x3, #1      // i++
    b loop

end:
    mov x0, x2          // return the next Fibonacci value, F(n + 1)
    ret
