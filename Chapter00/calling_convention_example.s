// $ aarch64-linux-gnu-as -g calling_convention_example.s -o calling_convention_example.o
// $ aarch64-linux-gnu-ld calling_convention_example.o -o calling_convention_example
// $ qemu-aarch64 -g 2345 ./calling_convention_example

.global _start
.section .text

_start:
    // Prepare arguments
    mov x0, #5
    mov x1, #3

    // Call add_numbers function
    bl add_numbers

    // Exit (result is in x0)
    mov x8, #93
    mov x0, #0
    svc #0

add_numbers:
    // Add the two numbers
    add x0, x0, x1

    // Return (result is already in x0)
    ret
