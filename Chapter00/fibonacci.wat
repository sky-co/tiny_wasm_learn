;; $ wat2wasm fibonacci.wat -o fibonacci.wasm
;; $ node fibonacci.js
(module
    (import "console" "log" (func $log (param i32)))

    (func $fibo (param $num i32) (result i32)
        (local $a i32)
        (local $b i32)
        (local $next i32)
        (local $i i32)

        ;; Start with a = 0, b = 1. Negative n also returns 0.
        i32.const 0
        local.set $a
        i32.const 1
        local.set $b
        i32.const 0
        local.set $i

        (block $exit
            (loop $my_loop
                local.get $i
                local.get $num
                i32.gt_s
                br_if $exit

                local.get $a
                call $log

                local.get $a
                local.get $b
                i32.add
                local.set $next

                local.get $b
                local.set $a
                local.get $next
                local.set $b

                local.get $i
                i32.const 1
                i32.add
                local.set $i

                br $my_loop
            )
        )

        local.get $a
    )

    (export "fibo" (func $fibo))
)
