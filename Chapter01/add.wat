;; $ wat2wasm add.wat -o add.wasm
;; $ wat2wasm add.wat -v
;; RET -> C0035FD6 https://armconverter.com/?code=RET

(module
  (export "foo" (func $f))
  (func $f
    return
  )
)
