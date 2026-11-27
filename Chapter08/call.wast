(module


  (func $fib (export "fib") (param i64) (result i64)
    (if (result i64) (i64.le_u (local.get 0) (i64.const 1))
      (then (i64.const 1))
      (else
        (i64.add
          (call $fib (i64.sub (local.get 0) (i64.const 2)))
          (call $fib (i64.sub (local.get 0) (i64.const 1)))
        )
      )
    )
  )

)



(assert_return (invoke "fib" (i64.const 0)) (i64.const 1))
(assert_return (invoke "fib" (i64.const 1)) (i64.const 1))
(assert_return (invoke "fib" (i64.const 2)) (i64.const 2))
(assert_return (invoke "fib" (i64.const 5)) (i64.const 8))
(assert_return (invoke "fib" (i64.const 20)) (i64.const 10946))
