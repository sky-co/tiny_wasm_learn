.section .text
.global cal_fib
.type cal_fib, %function

cal_fib:
    mov x1, #0          // a = F(0)
    mov x2, #1          // b = F(1)
    mov x3, #0          // i = 0

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
