(module
    (import "console" "log" (func $log (param i32)))

    (func $sum (param $num i32) (result i32)
        (local $i i32)
        (local $total i32)
        (block $exit
          (loop $my_loop
            ;; check if $i >= $num, exit loop
            local.get $i
            local.get $num
            i32.ge_s
            br_if $exit

            ;; $i = $i + 1
            local.get $i
            i32.const 1
            i32.add
            local.set $i

            ;; $total = $total + $i
            local.get $total
            local.get $i
            i32.add
            local.set $total

            ;; log current i
            local.get $i
            call $log

            br $my_loop
          )
        )
        local.get $total
    )

    (export "sum" (func $sum))
)
