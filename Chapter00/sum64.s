// $ aarch64-linux-gnu-as -g sum64.s -o sum64.o
// $ aarch64-linux-gnu-ld sum64.o -o sum64
// $ qemu-aarch64 -g 2345 ./sum64

// $ qemu-aarch64 ./sum64
//echo $?

.global _start
.section .text

_start:
    mov x0, #3
    mov x1, #0
    mov x2, #0
    bl cal_num

    // Exit syscall
    mov x8, #93        // 93 is the syscall number for exit
    mov x0, x2         // return the sum as the exit status
    svc #0             // Supervisor call to invoke the syscall

cal_num:
loop:
    cmp x0, x1

    b.le end

    add x1, x1, #1
    add x2, x1, x2
    b loop

end:
    ret
